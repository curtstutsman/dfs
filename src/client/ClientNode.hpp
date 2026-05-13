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
    int deadline_timeout = kDefaultDeadline;

    // Client's hostname + tid
    std::string client_id;

    // Directory client process mounts to
    std::string mount_path;

    // CRC Table for caching checksum calculations
    CRC::Table<std::uint32_t, 32> crc_table;

    // Ptr to gRPC client stub (handles rpc marshalling and transmission)
    std::unique_ptr<dfs_service::DFSService::Stub> service_stub;

    // Completion queue that holds async callbacks from server
    std::unique_ptr<grpc::CompletionQueue> completion_queue;

    // Enforces file sync between inotify and callback threads
    std::mutex server_lock;

    // Prepend the mount path to the filename
    std::string WrapPath(const std::string& filepath);

    // Sets deadline for an RPC call with the given context using member deadlin
    void CreateDeadline(grpc::ClientContext& context);

public:
    ClientNode();
    ~ClientNode() noexcept;

    // Get/Set utils
    void SetMountPath(const std::string& path);
    void SetDeadlineTimeout(int deadline);
    void SetClientId(const std::string& id);
    const std::string ClientId();
    const std::string MountPath();

    // Pulls files from the server that are newer than the local copy; called on mount.
    void SyncFromServer();

    // Shutdown the completion queue
    void Shutdown();

    // Creates the gRPC client stub
    void CreateStub(std::shared_ptr<grpc::Channel> channel);

    grpc::StatusCode RequestWriteAccess(const std::string& filename);
    grpc::StatusCode Store(const std::string& filename);
    grpc::StatusCode Fetch(const std::string& filename);
    grpc::StatusCode Delete(const std::string& filename);
    grpc::StatusCode List(std::map<std::string,int64_t>* file_map = nullptr, bool display = false);
    grpc::StatusCode Stat(const std::string& filename, dfs_service::StatResponse& response);

    // Acquires server_lock then runs callback; serializes inotify events against the callback loop.
    void Synchronized(std::function<void()> callback);

    // Register Async callback with server
    void InitCallbackList();

    // Spins on completion queue processing async callbacks for file updates from server
    void HandleCallbackList();
};

#endif
