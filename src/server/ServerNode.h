// Server entry point: constructs DFSServiceImpl and starts the service runner.
#ifndef SERVER_NODE_H
#define SERVER_NODE_H

#include <string>

class DFSServerNode {

private:
    std::string server_address;
    std::string mount_path;
    int num_async_threads;

public:
    DFSServerNode(const std::string& server_address,
                  const std::string& mount_path,
                  int num_async_threads);
    ~DFSServerNode();
    void Start();
};

#endif
