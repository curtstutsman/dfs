// Concrete gRPC client: implements all RPCs and the async callback handler.
#ifndef CLIENT_NODE_H
#define CLIENT_NODE_H

#include <string>
#include <map>
#include <mutex>

#include <grpcpp/grpcpp.h>

#include "src/client/ClientBase.h"

class DFSClientNode : public DFSClientBase {

private:
    std::mutex server_lock;

public:
    DFSClientNode();
    ~DFSClientNode();

    grpc::StatusCode RequestWriteAccess(const std::string& filename) override;
    grpc::StatusCode Store(const std::string& filename) override;
    grpc::StatusCode Fetch(const std::string& filename) override;
    grpc::StatusCode Delete(const std::string& filename) override;
    grpc::StatusCode List(std::map<std::string,int>* file_map = nullptr, bool display = false) override;
    grpc::StatusCode Stat(const std::string& filename, void* file_status = nullptr) override;

    void HandleCallbackList();
    void InitCallbackList() override;
    void InotifyWatcherCallback(std::function<void()> callback) override;
};

#endif
