// DFSClient driver: inotify watcher threads and CLI command dispatch.
#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <vector>
#include <thread>

#include "src/common/Inotify.h"
#include "src/client/ClientNode.h"

class DFSClient {

protected:
    int deadline_timeout;
    std::string mount_path;
    InotifyCallback callback;
    DFSClientNode client_node;
    std::vector<NotifyStruct> events;
    std::thread thread_async;

public:
    DFSClient();
    ~DFSClient();

    void InitializeClientNode(const std::string& server_address);
    void ProcessCommand(const std::string& command, const std::string& filename);
    void SetMountPath(const std::string& path);
    void SetDeadlineTimeout(int deadline);
    void Mount(const std::string& filepath);
    void Unmount();

    static void InotifyEventCallback(uint event_type, const std::string& filename, void* instance);
    static void InotifyWatcher(InotifyCallback callback, uint event_type,
                               FileDescriptor fd, DFSClientBase* node);
};

#endif
