// ClientNode: stub, accessors, all RPCs, and async callback loop.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <grpcpp/grpcpp.h>

#include "src/common/Utils.hpp"
#include "src/client/ClientNode.hpp"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Channel;
using grpc::Status;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

ClientNode::ClientNode() 
        : mount_path("mnt/client/"), 
          crc_table(CRC::CRC_32()), 
          completion_queue(std::make_unique<grpc::CompletionQueue>()) 
{
    char host[HOST_NAME_MAX];
    std::ostringstream ss_id;
    gethostname(host, HOST_NAME_MAX);
    ss_id << "T" << std::this_thread::get_id();
    client_id = std::string(host) + ss_id.str();
}

ClientNode::~ClientNode() noexcept {}

const std::string ClientNode::ClientId(){ 
    return this->client_id; 
}

void ClientNode::SetMountPath(const std::string& p) { 
    this->mount_path = p; 
}

void ClientNode::SetDeadlineTimeout(int deadline) { 
    this->deadline_timeout = deadline;
}

void ClientNode::SetClientId(const std::string& id) { 
    this->client_id = id; 
}

const std::string ClientNode::MountPath(){ 
    return this->mount_path; 
}

void ClientNode::SyncFromServer() {
    std::map<std::string, int64_t> file_map;
    if (List(&file_map) != StatusCode::OK) return;
    for (const auto& [filename, server_mtime] : file_map) {
        struct stat local_stat;
        if (stat(WrapPath(filename).c_str(), &local_stat) == 0 &&
            local_stat.st_mtime >= server_mtime) {
            continue;
        }
        Fetch(filename);
    }
}

void ClientNode::Shutdown() {
    completion_queue->Shutdown();
}

void ClientNode::CreateStub(std::shared_ptr<Channel> channel) {
    this->service_stub = dfs_service::DFSService::NewStub(channel);
}

std::string ClientNode::WrapPath(const std::string& filepath) {
    return this->mount_path + filepath;
}

void ClientNode::CreateDeadline(ClientContext& context){
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout));
}

grpc::StatusCode ClientNode::RequestWriteAccess(const std::string& filename) {
    ClientContext context;
    CreateDeadline(context);
    dfs_service::WriteLockRequest request;
    dfs_service::WriteLockResponse response;

    request.set_filename(filename);
    request.set_clientid(ClientId());

    Status status = service_stub->WriteLock(&context, request, &response);
    return status.error_code();
}

grpc::StatusCode ClientNode::Store(const std::string& filename) {
    // Verify file existence
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
    CreateDeadline(context);
    dfs_service::StoreResponse response;
    dfs_service::StoreRequest request;

    request.set_filename(filename);
    request.set_clientid(ClientId());
    request.set_crc(dfs_file_checksum(full_path, &crc_table));
    char buf[kChunkSize];

    auto writer = service_stub->StoreFile(&context, &response);
    while (file.read(buf, kChunkSize) || file.gcount()) {
        request.set_chunk(buf, file.gcount());
        writer->Write(request);
    }
    writer->WritesDone();
    return writer->Finish().error_code();
}

grpc::StatusCode ClientNode::Fetch(const std::string& filename) {
    ClientContext context;
    CreateDeadline(context);
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

    // Write to a temp file first; rename atomically on success so a mid-transfer
    // failure never leaves the local file truncated.
    std::string tmp_path = full_path + ".dfs_tmp";
    {
        std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
        file.write(response.chunk().data(), response.chunk().size());
        while (reader->Read(&response)) {
            file.write(response.chunk().data(), response.chunk().size());
        }
    }

    StatusCode retval = reader->Finish().error_code();
    if (retval == StatusCode::OK) {
        std::rename(tmp_path.c_str(), full_path.c_str());
        struct utimbuf times;
        times.modtime = response.mtime();
        utime(full_path.c_str(), &times);
    } else {
        std::remove(tmp_path.c_str());
    }
    return retval;
}

grpc::StatusCode ClientNode::Delete(const std::string& filename) {
    grpc::StatusCode lock_status = RequestWriteAccess(filename);
    if (lock_status != grpc::StatusCode::OK) {
        return lock_status;
    }

    ClientContext context;
    CreateDeadline(context);
    dfs_service::DeleteRequest request;
    dfs_service::DeleteResponse response;

    request.set_filename(filename);
    request.set_clientid(ClientId());
    Status status = service_stub->DeleteFile(&context, request, &response);
    return status.error_code();
}

grpc::StatusCode ClientNode::List(std::map<std::string,int64_t>* file_map, bool display) {
    ClientContext context;
    CreateDeadline(context);
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

grpc::StatusCode ClientNode::Stat(const std::string& filename, dfs_service::StatResponse& response) {
    ClientContext context;
    CreateDeadline(context);
    dfs_service::StatRequest request;
    request.set_filename(filename);
    Status status = service_stub->StatFile(&context, request, &response);
    return status.error_code();
}

void ClientNode::Synchronized(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(server_lock);
    callback();
}

void ClientNode::HandleCallbackList() {
    void* tag;
    bool ok = false;

    while (completion_queue->Next(&tag, &ok)) {
        {
            std::lock_guard<std::mutex> lock(server_lock);
            AsyncClientData<dfs_service::CallbackListResponse>* call_data =
                static_cast<AsyncClientData<dfs_service::CallbackListResponse>*>(tag);

            if (!ok) {
                dfs_log(LL_ERROR) << "Completion queue callback not ok.";
            }

            if (ok && call_data->status.ok()) {
                const auto& response = call_data->reply;
                struct stat client_stats;
                for (int i = 0; i < response.filecount(); ++i) {
                    const auto& server_stats = response.stat(i);
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
                    << ". Retrying in " << kResetTimeoutMs << "ms.";
                std::this_thread::sleep_for(std::chrono::milliseconds(kResetTimeoutMs));
            }

            delete call_data;
        }

        InitCallbackList();
    }
}

void ClientNode::InitCallbackList() {
    dfs_service::CallbackListRequest request;
    request.set_clientid(ClientId());
    AsyncClientData<dfs_service::CallbackListResponse>* call_data = new AsyncClientData<dfs_service::CallbackListResponse>;
    call_data->response_reader = service_stub->PrepareAsyncCallbackList(&call_data->context, request, completion_queue.get());
    call_data->response_reader->StartCall();
    call_data->response_reader->Finish(&call_data->reply, &call_data->status, (void*)call_data);
}
