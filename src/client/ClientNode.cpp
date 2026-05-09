// DFSClientNode: Store/Fetch/Delete/List/Stat/WriteLock RPCs and callback loop.
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>
#include <utime.h>

#include "src/common/Log.h"
#include "src/common/Config.h"
#include "src/common/Checksum.h"
#include "src/client/ClientNode.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

using FileRequestType    = dfs_service::CallbackListRequest;
using FileListResponseType = dfs_service::CallbackListResponse;

DFSClientNode::DFSClientNode() : DFSClientBase() {}
DFSClientNode::~DFSClientNode() {}

grpc::StatusCode DFSClientNode::RequestWriteAccess(const std::string& filename) {
    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout));
    dfs_service::WriteLockRequest request;
    dfs_service::WriteLockResponse response;
    request.set_filename(filename);
    request.set_clientid(ClientId());
    Status status = service_stub->WriteLock(&context, request, &response);
    return status.error_code();
}

grpc::StatusCode DFSClientNode::Store(const std::string& filename) {
    std::string full_path = WrapPath(filename);
    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        return StatusCode::CANCELLED;
    }

    grpc::StatusCode lock_status = RequestWriteAccess(filename);
    if (lock_status != grpc::StatusCode::OK) {
        return lock_status;
    }

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout));
    dfs_service::StoreResponse response;
    dfs_service::StoreRequest request;

    request.set_filename(filename);
    request.set_clientid(ClientId());
    request.set_crc(dfs_file_checksum(full_path, &crc_table));
    auto writer = service_stub->StoreFile(&context, &response);

    char buf[CHUNK_SIZE];
    while (file.read(buf, CHUNK_SIZE) || file.gcount()) {
        request.set_chunk(buf, file.gcount());
        writer->Write(request);
    }
    writer->WritesDone();
    return writer->Finish().error_code();
}

grpc::StatusCode DFSClientNode::Fetch(const std::string& filename) {
    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout));
    dfs_service::FetchRequest request;
    dfs_service::FetchResponse response;

    std::string full_path = WrapPath(filename);
    request.set_filename(filename);
    request.set_clientid(ClientId());
    request.set_crc(dfs_file_checksum(full_path, &crc_table));

    auto reader = service_stub->FetchFile(&context, request);

    bool has_data = reader->Read(&response);
    if (!has_data) {
        return reader->Finish().error_code();
    }
    std::ofstream file(full_path, std::ios::binary | std::ios::trunc);
    file.write(response.chunk().data(), response.chunk().size());
    while (reader->Read(&response)) {
        file.write(response.chunk().data(), response.chunk().size());
    }
    file.close();

    StatusCode retval = reader->Finish().error_code();
    if (retval == StatusCode::OK) {
        struct utimbuf times;
        times.modtime = response.mtime();
        utime(full_path.c_str(), &times);
    }
    return retval;
}

grpc::StatusCode DFSClientNode::Delete(const std::string& filename) {
    grpc::StatusCode lock_status = RequestWriteAccess(filename);
    if (lock_status != grpc::StatusCode::OK) {
        return lock_status;
    }

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout));
    dfs_service::DeleteRequest request;
    dfs_service::DeleteResponse response;
    request.set_filename(filename);
    request.set_clientid(ClientId());
    Status status = service_stub->DeleteFile(&context, request, &response);
    return status.error_code();
}

grpc::StatusCode DFSClientNode::List(std::map<std::string,int>* file_map, bool display) {
    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout));
    dfs_service::ListRequest request;
    dfs_service::ListResponse response;

    auto reader = service_stub->ListFile(&context, request);
    while (reader->Read(&response)) {
        if (file_map) {
            (*file_map)[response.filename()] = response.mtime();
        }
        if (display) {
            std::cout << response.filename() << ": " << response.mtime() << std::endl;
        }
    }
    return reader->Finish().error_code();
}

grpc::StatusCode DFSClientNode::Stat(const std::string& filename, void* file_status) {
    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout));
    dfs_service::StatRequest request;
    request.set_filename(filename);
    auto* response = static_cast<dfs_service::StatResponse*>(file_status);
    Status status = service_stub->StatFile(&context, request, response);
    return status.error_code();
}

void DFSClientNode::InotifyWatcherCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(server_lock);
    callback();
}

void DFSClientNode::HandleCallbackList() {
    void* tag;
    bool ok = false;

    while (completion_queue.Next(&tag, &ok)) {
        {
            std::lock_guard<std::mutex> lock(server_lock);
            AsyncClientData<FileListResponseType>* call_data =
                static_cast<AsyncClientData<FileListResponseType>*>(tag);

            if (!ok) {
                dfs_log(LL_ERROR) << "Completion queue callback not ok.";
            }

            if (ok && call_data->status.ok()) {
                auto response = call_data->reply;
                struct stat client_stats;
                for (int i = 0; i < response.filecount(); ++i) {
                    auto server_stats = response.stat(i);
                    if (server_stats.crc() == dfs_file_checksum(WrapPath(server_stats.filename()), &crc_table)) {
                        continue;
                    }
                    if (stat(WrapPath(server_stats.filename()).c_str(), &client_stats) == 0) {
                        if (client_stats.st_mtime < server_stats.mtime()) {
                            Fetch(server_stats.filename());
                        }
                    } else {
                        Fetch(server_stats.filename());
                    }
                }
            } else {
                dfs_log(LL_ERROR) << "Async callback failed: " << call_data->status.error_message()
                    << ". Retrying in " << DFS_RESET_TIMEOUT << "ms.";
                std::this_thread::sleep_for(std::chrono::milliseconds(DFS_RESET_TIMEOUT));
            }

            delete call_data;
        }

        InitCallbackList();
    }
}

void DFSClientNode::InitCallbackList() {
    CallbackList<FileRequestType, FileListResponseType>();
}
