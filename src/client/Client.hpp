// Client: inotify watcher threads and CLI command dispatch.
#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <vector>
#include <thread>
#include <sys/inotify.h>

#include "src/common/Utils.hpp"
#include "src/client/Inotify.hpp"
#include "src/client/ClientNode.hpp"

class Client {
    int deadline_timeout;
    std::string mount_path;
    ClientNode client_node;
    std::vector<NotifyStruct> events;
    std::thread thread_async;

    void InotifyWatcher(uint event_type, FileDescriptor inotify_descriptor);

public:
    Client();
    ~Client();

    void InitializeClientNode(const std::string& server_address);
    void ProcessCommand(const std::string& command, const std::string& filename);
    void SetMountPath(const std::string& path);
    void SetDeadlineTimeout(int deadline);
    void Mount(const std::string& filepath);
    void Unmount();
};

#endif
