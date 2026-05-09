// Abstract client base: gRPC stub, CRC table, and async CallbackList<> template.
#ifndef CLIENT_BASE_H
#define CLIENT_BASE_H

#include <string>
#include <vector>
#include <map>
#include <limits.h>
#include <chrono>
#include <mutex>
#include <functional>

#include <grpcpp/grpcpp.h>

#include "src/common/Checksum.h"
#include "proto-src/dfs-service.grpc.pb.h"

template<typename ResponseT>
struct AsyncClientData {
    ResponseT reply;
    grpc::ClientContext context;
    grpc::Status status;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseT>> response_reader;
};

class DFSClientBase {

protected:
    int deadline_timeout;
    std::string client_id;
    std::string mount_path;
    bool unmounting;
    CRC::Table<std::uint32_t, 32> crc_table;
    std::unique_ptr<dfs_service::DFSService::Stub> service_stub;
    grpc::CompletionQueue completion_queue;

    std::string WrapPath(const std::string& filepath);

public:
    DFSClientBase();
    ~DFSClientBase();

    void SetMountPath(const std::string& path);
    void SetDeadlineTimeout(int deadline);
    void SetClientId(const std::string& id);
    const std::string MountPath();
    void Unmount();
    bool Unmounting();
    const std::string ClientId();
    void CreateStub(std::shared_ptr<grpc::Channel> channel);

    virtual grpc::StatusCode RequestWriteAccess(const std::string& filename) = 0;
    virtual grpc::StatusCode Store(const std::string& filename) = 0;
    virtual grpc::StatusCode Fetch(const std::string& filename) = 0;
    virtual grpc::StatusCode Delete(const std::string& filename) = 0;
    virtual grpc::StatusCode List(std::map<std::string,int>* file_map = nullptr, bool display = false) = 0;
    virtual grpc::StatusCode Stat(const std::string& filename, void* file_status = nullptr) = 0;
    virtual void InotifyWatcherCallback(std::function<void()> callback) = 0;
    virtual void InitCallbackList() = 0;

    template<typename RequestT, typename ResponseT>
    void CallbackList() {
        RequestT request;
        request.set_name("");
        AsyncClientData<ResponseT>* call_data = new AsyncClientData<ResponseT>;
        call_data->response_reader =
            service_stub->PrepareAsyncCallbackList(&call_data->context, request, &completion_queue);
        call_data->response_reader->StartCall();
        call_data->response_reader->Finish(&call_data->reply, &call_data->status, (void*)call_data);
    }
};

#endif
