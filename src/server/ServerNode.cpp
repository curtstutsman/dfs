// DFSServerNode: creates DFSServiceImpl and calls Run() to block until shutdown.
#include "src/common/Log.h"
#include "src/server/ServerNode.h"
#include "src/server/ServiceImpl.h"

DFSServerNode::DFSServerNode(const std::string& server_address,
                             const std::string& mount_path,
                             int num_async_threads)
    : server_address(server_address),
      mount_path(mount_path),
      num_async_threads(num_async_threads) {}

DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
}

void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path, this->server_address, this->num_async_threads);
    service.Run();
}
