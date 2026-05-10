// Server builder and thread pool for async RPC, sync wait, and queued callbacks.
#ifndef SERVER_ASYNC_SERVICE_RUNNER_H
#define SERVER_ASYNC_SERVICE_RUNNER_H

#include <vector>
#include <string>
#include <thread>
#include <functional>
#include <grpcpp/grpcpp.h>

#include "src/common/Utils.hpp"
#include "src/server/async/CallData.hpp"
#include "proto-src/dfs-service.grpc.pb.h"

template <typename RequestT, typename ResponseT>
struct QueueRequest {
    grpc::ServerContext* context;
    RequestT* request;
    grpc::ServerAsyncResponseWriter<ResponseT>* response;
    grpc::ServerCompletionQueue* cq;
    void* tag;
    bool finished;
    QueueRequest(grpc::ServerContext* context,
                 RequestT* request,
                 grpc::ServerAsyncResponseWriter<ResponseT>* response,
                 grpc::ServerCompletionQueue* cq,
                 void* tag)
        : context(context), request(request), response(response), cq(cq), tag(tag), finished(false) {}
};

template <typename RequestT, typename ResponseT>
static void HandleAsyncRPC(dfs_service::DFSService::AsyncService* service,
                            DFSCallDataManager<RequestT, ResponseT>* manager,
                            std::shared_ptr<grpc::ServerCompletionQueue> cq) {
    new DFSCallData<RequestT, ResponseT>(service, manager, cq.get());
    void* tag;
    bool ok;

    while (true) {
        if (!cq->Next(&tag, &ok) || !ok) {
            dfs_log(LL_ERROR) << "Async completion queue error";
            continue;
        }
        static_cast<DFSCallData<RequestT, ResponseT>*>(tag)->Proceed();
    }
}

template <typename RequestT, typename ResponseT>
static void HandleSyncRPC(std::shared_ptr<grpc::Server> server) {
    server->Wait();
}

template <typename RequestT, typename ResponseT>
class DFSServiceRunner {

protected:
    std::string server_address;
    int num_async_threads;
    grpc::Service* service;
    std::shared_ptr<grpc::Server> server;
    std::shared_ptr<grpc::ServerCompletionQueue> completion_queue;
    dfs_service::DFSService::AsyncService async_service;
    std::function<void()> queued_requests_callback;

public:
    DFSServiceRunner() {}

    void SetService(grpc::Service* svc)                       { this->service = svc; }
    void SetAddress(const std::string& addr)                  { this->server_address = addr; }
    void SetNumThreads(int n)                                 { this->num_async_threads = n; }
    void SetQueuedRequestsCallback(std::function<void()> cb)  { this->queued_requests_callback = cb; }
    void Shutdown() noexcept                                  { this->server->Shutdown(); }

    void Run() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort(this->server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(this->service);
        this->completion_queue = builder.AddCompletionQueue();
        this->server = builder.BuildAndStart();
        dfs_log(LL_SYSINFO) << "Server listening on " << this->server_address;

        std::vector<std::thread> threads;

        for (int i = this->num_async_threads; i > 0; i--) {
            threads.emplace_back(HandleAsyncRPC<RequestT, ResponseT>,
                                 &this->async_service,
                                 dynamic_cast<DFSCallDataManager<RequestT, ResponseT>*>(this->service),
                                 this->completion_queue);
        }

        threads.emplace_back(HandleSyncRPC<RequestT, ResponseT>, this->server);
        threads.emplace_back(queued_requests_callback);

        for (std::thread& t : threads) {
            if (t.joinable()) { t.join(); }
        }
    }
};

#endif
