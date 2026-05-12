// ClientNode: gRPC stub, CRC table, async callback queue, and all RPC implementations.
#ifndef CLIENT_NODE_H
#define CLIENT_NODE_H

#include <map>
#include <mutex>
#include <string>
#include <limits.h>
#include <functional>

#include <grpcpp/grpcpp.h>

#include "src/common/Utils.hpp"
#include "proto-src/dfs-service.grpc.pb.h"

template<typename ResponseT>
struct AsyncClientData {
    ResponseT reply;
    grpc::ClientContext context;
    grpc::Status status;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseT>> response_reader;
};

class ClientNode {
private:
    int deadline_timeout;
    std::string client_id;
    std::string mount_path;
    bool unmounting;
    CRC::Table<std::uint32_t, 32> crc_table;
    std::unique_ptr<dfs_service::DFSService::Stub> service_stub;
    std::unique_ptr<grpc::CompletionQueue> completion_queue;
    std::mutex server_lock;

    std::string WrapPath(const std::string& filepath);

public:
    ClientNode();
    ~ClientNode() noexcept;

    void SetMountPath(const std::string& path);
    void SetDeadlineTimeout(int deadline);
    void SetClientId(const std::string& id);
    const std::string MountPath();
    void Reset();
    void SyncFromServer();
    void Unmount();
    bool Unmounting();
    const std::string ClientId();
    void CreateStub(std::shared_ptr<grpc::Channel> channel);

    grpc::StatusCode RequestWriteAccess(const std::string& filename);
    grpc::StatusCode Store(const std::string& filename);
    grpc::StatusCode Fetch(const std::string& filename);
    grpc::StatusCode Delete(const std::string& filename);
    grpc::StatusCode List(std::map<std::string,int>* file_map = nullptr, bool display = false);
    grpc::StatusCode Stat(const std::string& filename, void* file_status = nullptr);

    void HandleCallbackList();
    void InitCallbackList();
    void Synchronized(std::function<void()> callback);
};

#endif
