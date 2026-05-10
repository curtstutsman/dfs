// Async gRPC call state machine: CREATE -> PROCESS -> FINISH lifecycle.
#ifndef SERVER_ASYNC_CALL_DATA_H
#define SERVER_ASYNC_CALL_DATA_H

#include <grpcpp/grpcpp.h>
#include "src/common/Utils.hpp"
#include "proto-src/dfs-service.grpc.pb.h"

template <typename RequestT, typename ResponseT>
class DFSCallDataManager {
public:
    virtual void RequestCallback(grpc::ServerContext* context,
                                 RequestT* request,
                                 grpc::ServerAsyncResponseWriter<ResponseT>* responder,
                                 grpc::ServerCompletionQueue* cq,
                                 void* tag) {}
    virtual void ProcessCallback(grpc::ServerContext* context, RequestT* request, ResponseT* response) {}
};

template <typename RequestT, typename ResponseT>
class DFSCallData {

private:
    dfs_service::DFSService::AsyncService* service;
    DFSCallDataManager<RequestT, ResponseT>* manager;
    grpc::ServerCompletionQueue* cq;
    grpc::ServerContext ctx_;
    RequestT request_;
    ResponseT reply_;
    grpc::ServerAsyncResponseWriter<ResponseT> responder;

    enum CallStatus { CREATE, PROCESS, FINISH };
    CallStatus status;

public:
    DFSCallData(dfs_service::DFSService::AsyncService* service,
                DFSCallDataManager<RequestT, ResponseT>* manager,
                grpc::ServerCompletionQueue* cq)
        : service(service), manager(manager), cq(cq), responder(&ctx_), status(CREATE) {
        Proceed();
    }

    void Proceed() {
        if (status == CREATE) {
            status = PROCESS;
            manager->RequestCallback(&ctx_, &request_, &responder, cq, this);
        } else if (status == PROCESS) {
            new DFSCallData<RequestT, ResponseT>(service, manager, cq);
            manager->ProcessCallback(&ctx_, &request_, &reply_);
            status = FINISH;
            responder.Finish(reply_, grpc::Status::OK, this);
        } else {
            delete this;
        }
    }
};

#endif
