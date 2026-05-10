// gRPC service: all 7 RPC handlers, async CallbackList queue, and LockManager.
#ifndef SERVER_SERVICE_IMPL_H
#define SERVER_SERVICE_IMPL_H

#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <condition_variable>
#include <grpcpp/grpcpp.h>

#include "src/server/FileStore.hpp"
#include "src/server/LockManager.hpp"
#include "src/server/async/CallData.hpp"
#include "src/server/async/ServiceRunner.hpp"
#include "proto-src/dfs-service.grpc.pb.h"

using FileRequestType      = dfs_service::CallbackListRequest;
using FileListResponseType = dfs_service::CallbackListResponse;

class DFSServiceImpl final :
    public dfs_service::DFSService::WithAsyncMethod_CallbackList<dfs_service::DFSService::Service>,
    public DFSCallDataManager<FileRequestType, FileListResponseType> {

private:
    DFSServiceRunner<FileRequestType, FileListResponseType> runner;
    std::unique_ptr<FileStore> file_store;
    std::mutex queue_mutex;
    std::vector<QueueRequest<FileRequestType, FileListResponseType>> queued_tags;
    std::condition_variable updated;
    std::mutex updated_mutex;
    LockManager lock_manager;

public:
    DFSServiceImpl(const std::string& mount_path, const std::string& server_address, int num_async_threads);
    ~DFSServiceImpl();

    void Run();
    void ProcessQueuedRequests();

    void RequestCallback(grpc::ServerContext* context,
                         FileRequestType* request,
                         grpc::ServerAsyncResponseWriter<FileListResponseType>* response,
                         grpc::ServerCompletionQueue* cq,
                         void* tag) override;

    void ProcessCallback(grpc::ServerContext* context,
                         FileRequestType* request,
                         FileListResponseType* response) override;

    grpc::Status StoreFile(grpc::ServerContext* context,
                           grpc::ServerReader<dfs_service::StoreRequest>* reader,
                           dfs_service::StoreResponse* response) override;

    grpc::Status FetchFile(grpc::ServerContext* context,
                           const dfs_service::FetchRequest* request,
                           grpc::ServerWriter<dfs_service::FetchResponse>* writer) override;

    grpc::Status DeleteFile(grpc::ServerContext* context,
                            const dfs_service::DeleteRequest* request,
                            dfs_service::DeleteResponse* response) override;

    grpc::Status ListFile(grpc::ServerContext* context,
                          const dfs_service::ListRequest* request,
                          grpc::ServerWriter<dfs_service::ListResponse>* writer) override;

    grpc::Status StatFile(grpc::ServerContext* context,
                          const dfs_service::StatRequest* request,
                          dfs_service::StatResponse* response) override;

    grpc::Status CallbackList(grpc::ServerContext* context,
                              const dfs_service::CallbackListRequest* request,
                              dfs_service::CallbackListResponse* response) override;

    grpc::Status WriteLock(grpc::ServerContext* context,
                           const dfs_service::WriteLockRequest* request,
                           dfs_service::WriteLockResponse* response) override;
};

#endif
