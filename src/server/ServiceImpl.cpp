// DFSServiceImpl: Store, Fetch, Delete, List, Stat, WriteLock, CallbackList handlers.
#include <string>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>

#include "src/common/Log.h"
#include "src/common/Config.h"
#include "src/server/ServiceImpl.h"

using grpc::Status;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;

DFSServiceImpl::DFSServiceImpl(const std::string& mount_path,
                               const std::string& server_address,
                               int num_async_threads)
    : mount_path(mount_path), crc_table(CRC::CRC_32()) {
    this->runner.SetService(this);
    this->runner.SetAddress(server_address);
    this->runner.SetNumThreads(num_async_threads);
    this->runner.SetQueuedRequestsCallback([&]{ this->ProcessQueuedRequests(); });
}

DFSServiceImpl::~DFSServiceImpl() {
    this->runner.Shutdown();
}

void DFSServiceImpl::Run() {
    this->runner.Run();
}

void DFSServiceImpl::RequestCallback(grpc::ServerContext* context,
                                     FileRequestType* request,
                                     grpc::ServerAsyncResponseWriter<FileListResponseType>* response,
                                     grpc::ServerCompletionQueue* cq,
                                     void* tag) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    this->queued_tags.emplace_back(context, request, response, cq, tag);
}

void DFSServiceImpl::ProcessCallback(ServerContext* context,
                                     FileRequestType* request,
                                     FileListResponseType* response) {
    CallbackList(context, request, response);
}

void DFSServiceImpl::ProcessQueuedRequests() {
    while (true) {
        std::unique_lock<std::mutex> lock(updated_mutex);
        updated.wait(lock);

        {
            std::lock_guard<std::mutex> qlock(queue_mutex);
            for (auto& queue_request : this->queued_tags) {
                this->RequestCallbackList(queue_request.context, queue_request.request,
                    queue_request.response, queue_request.cq, queue_request.cq, queue_request.tag);
                queue_request.finished = true;
            }
            this->queued_tags.erase(
                std::remove_if(this->queued_tags.begin(), this->queued_tags.end(),
                    [](const QueueRequest<FileRequestType, FileListResponseType>& r) { return r.finished; }),
                this->queued_tags.end());
        }
    }
}

Status DFSServiceImpl::StoreFile(ServerContext* context,
                                  ServerReader<dfs_service::StoreRequest>* reader,
                                  dfs_service::StoreResponse* response) {
    dfs_service::StoreRequest request;
    reader->Read(&request);

    if (!lock_manager.isHolding(request.filename(), request.clientid())) {
        return Status(StatusCode::CANCELLED, "Write lock not held");
    }

    std::string full_path = WrapPath(request.filename());
    if (request.crc() == dfs_file_checksum(full_path, &crc_table)) {
        lock_manager.release(request.filename(), request.clientid());
        return Status(StatusCode::ALREADY_EXISTS, "File unchanged");
    }

    std::string captured_filename = request.filename();
    std::string captured_clientid = request.clientid();
    std::ofstream file(full_path, std::ios::binary | std::ios::trunc);
    file.write(request.chunk().data(), request.chunk().size());

    while (reader->Read(&request)) {
        if (context->IsCancelled()) {
            lock_manager.release(captured_filename, captured_clientid);
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        file.write(request.chunk().data(), request.chunk().size());
    }
    file.close();

    lock_manager.release(captured_filename, captured_clientid);
    updated.notify_all();
    return Status::OK;
}

Status DFSServiceImpl::FetchFile(ServerContext* context,
                                  const dfs_service::FetchRequest* request,
                                  ServerWriter<dfs_service::FetchResponse>* writer) {
    std::string full_path = WrapPath(request->filename());
    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }

    if (request->crc() == dfs_file_checksum(full_path, &crc_table)) {
        return Status(StatusCode::ALREADY_EXISTS, "File unchanged");
    }

    dfs_service::FetchResponse response;
    struct stat status;
    stat(full_path.c_str(), &status);
    response.set_mtime(status.st_mtime);

    char buf[CHUNK_SIZE];
    while (file.read(buf, sizeof(buf)) || file.gcount()) {
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        response.set_chunk(buf, file.gcount());
        writer->Write(response);
    }
    return Status::OK;
}

Status DFSServiceImpl::DeleteFile(ServerContext* context,
                                   const dfs_service::DeleteRequest* request,
                                   dfs_service::DeleteResponse* response) {
    if (!lock_manager.isHolding(request->filename(), request->clientid())) {
        return Status(StatusCode::CANCELLED, "Write lock not held");
    }
    if (context->IsCancelled()) {
        lock_manager.release(request->filename(), request->clientid());
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
    }
    if (std::remove(WrapPath(request->filename()).c_str()) != 0) {
        lock_manager.release(request->filename(), request->clientid());
        return Status(StatusCode::NOT_FOUND, "File not found");
    }
    lock_manager.release(request->filename(), request->clientid());
    updated.notify_all();
    return Status::OK;
}

Status DFSServiceImpl::ListFile(ServerContext* context,
                                 const dfs_service::ListRequest* request,
                                 ServerWriter<dfs_service::ListResponse>* writer) {
    dfs_service::ListResponse response;
    struct stat status;
    for (const auto& entry : std::filesystem::directory_iterator(mount_path)) {
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        response.set_filename(entry.path().filename());
        stat(entry.path().c_str(), &status);
        response.set_mtime(status.st_mtime);
        writer->Write(response);
    }
    return Status::OK;
}

Status DFSServiceImpl::StatFile(ServerContext* context,
                                 const dfs_service::StatRequest* request,
                                 dfs_service::StatResponse* response) {
    struct stat status;
    if (stat(WrapPath(request->filename()).c_str(), &status) != 0) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }
    if (context->IsCancelled()) {
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
    }
    response->set_size(status.st_size);
    response->set_mtime(status.st_mtime);
    return Status::OK;
}

Status DFSServiceImpl::CallbackList(ServerContext* context,
                                     const dfs_service::CallbackListRequest* request,
                                     dfs_service::CallbackListResponse* response) {
    struct stat status;
    int file_count = 0;
    for (const auto& file : std::filesystem::directory_iterator(mount_path)) {
        if (stat(file.path().c_str(), &status) == 0) {
            auto* stats = response->add_stat();
            stats->set_filename(file.path().filename());
            stats->set_mtime(status.st_mtime);
            stats->set_size(status.st_size);
            stats->set_crc(dfs_file_checksum(file.path(), &crc_table));
            file_count++;
        }
    }
    response->set_filecount(file_count);
    return Status::OK;
}

Status DFSServiceImpl::WriteLock(ServerContext* context,
                                  const dfs_service::WriteLockRequest* request,
                                  dfs_service::WriteLockResponse* response) {
    if (context->IsCancelled()) {
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
    }
    if (lock_manager.acquire(request->filename(), request->clientid())) {
        return Status::OK;
    }
    return Status(StatusCode::RESOURCE_EXHAUSTED, "Lock held by another client");
}
