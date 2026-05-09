// DFSClientBase: stub creation, path/deadline/id accessors, client_id generation.
#include <string>
#include <thread>
#include <sstream>
#include <unistd.h>
#include <limits.h>
#include <grpcpp/grpcpp.h>

#include "src/client/ClientBase.h"

using grpc::Channel;

DFSClientBase::DFSClientBase() : mount_path("mnt/client/"), unmounting(false), crc_table(CRC::CRC_32()) {
    char host[HOST_NAME_MAX];
    std::ostringstream ss_id;
    gethostname(host, HOST_NAME_MAX);
    auto t_id = std::this_thread::get_id();
    ss_id << "T" << t_id;
    client_id = std::string(host + ss_id.str());
}

DFSClientBase::~DFSClientBase() noexcept {}

void DFSClientBase::Unmount()                          { this->unmounting = true; }
bool DFSClientBase::Unmounting()                       { return this->unmounting; }
const std::string DFSClientBase::ClientId()            { return this->client_id; }
void DFSClientBase::SetMountPath(const std::string& p) { this->mount_path = p; }
void DFSClientBase::SetDeadlineTimeout(int deadline)   { this->deadline_timeout = deadline; }
void DFSClientBase::SetClientId(const std::string& id) { this->client_id = id; }
const std::string DFSClientBase::MountPath()           { return this->mount_path; }

void DFSClientBase::CreateStub(std::shared_ptr<Channel> channel) {
    this->service_stub = dfs_service::DFSService::NewStub(channel);
}

std::string DFSClientBase::WrapPath(const std::string& filepath) {
    return this->mount_path + filepath;
}
