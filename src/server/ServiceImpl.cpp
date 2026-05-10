// DFSServiceImpl: gRPC protocol handling; delegates all file I/O to FileStore.
#include <string>
#include <grpcpp/grpcpp.h>

#include "src/common/Utils.hpp"
#include "src/server/ServiceImpl.hpp"

using grpc::Status;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;

DFSServiceImpl::DFSServiceImpl(const std::string& mount_path,
                               const std::string& server_address,
                               int num_async_threads)
    : file_store(std::make_unique<FileStore>(mount_path)) {
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

    if (file_store->IsUnchanged(request.filename(), request.crc())) {
        lock_manager.release(request.filename(), request.clientid());
        return Status(StatusCode::ALREADY_EXISTS, "File unchanged");
    }

    const std::string filename = request.filename();
    const std::string clientid = request.clientid();
    {
        auto out = file_store->OpenWrite(filename);
        out.write(request.chunk().data(), request.chunk().size());
        while (reader->Read(&request)) {
            if (context->IsCancelled()) {
                lock_manager.release(filename, clientid);
                return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
            }
            out.write(request.chunk().data(), request.chunk().size());
        }
    }  // out flushed and closed here

    file_store->AfterWrite(filename);
    lock_manager.release(filename, clientid);
    updated.notify_all();
    return Status::OK;
}

Status DFSServiceImpl::FetchFile(ServerContext* context,
                                  const dfs_service::FetchRequest* request,
                                  ServerWriter<dfs_service::FetchResponse>* writer) {
    int64_t mtime;
    auto in = file_store->OpenRead(request->filename(), mtime);
    if (!in.is_open()) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }

    if (file_store->IsUnchanged(request->filename(), request->crc())) {
        return Status(StatusCode::ALREADY_EXISTS, "File unchanged");
    }

    dfs_service::FetchResponse response;
    response.set_mtime(mtime);

    char buf[CHUNK_SIZE];
    while (in.read(buf, sizeof(buf)) || in.gcount()) {
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        response.set_chunk(buf, in.gcount());
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
    if (!file_store->Remove(request->filename())) {
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
    for (const auto& file : file_store->List()) {
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        response.set_filename(file.name);
        response.set_mtime(file.mtime);
        writer->Write(response);
    }
    return Status::OK;
}

Status DFSServiceImpl::StatFile(ServerContext* context,
                                 const dfs_service::StatRequest* request,
                                 dfs_service::StatResponse* response) {
    if (context->IsCancelled()) {
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
    }
    auto stat = file_store->Stat(request->filename());
    if (!stat) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }
    response->set_size(stat->size);
    response->set_mtime(stat->mtime);
    return Status::OK;
}

Status DFSServiceImpl::CallbackList(ServerContext* context,
                                     const dfs_service::CallbackListRequest* request,
                                     dfs_service::CallbackListResponse* response) {
    auto files = file_store->ListDetails();
    response->set_filecount(static_cast<int64_t>(files.size()));
    for (const auto& f : files) {
        auto* stats = response->add_stat();
        stats->set_filename(f.name);
        stats->set_mtime(f.mtime);
        stats->set_size(f.size);
        stats->set_crc(f.crc);
    }
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
