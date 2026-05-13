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
    // Path to observe for mount command
    std::string mount_path;

    // ClientNode implements gRPC service
    ClientNode client_node;

    // For tracking watched directories
    std::vector<NotifyStruct> events;

    // Watches mount directory for inotify events calls grpc methods
    void InotifyWatcher(uint event_type, FileDescriptor inotify_descriptor);

public:
    Client();
    ~Client();

    // Reads inotify fd for events and generates corresponding gRPC calls
    void InitializeClientNode(const std::string& server_address);

    // Handles command given from cl
    void ProcessCommand(const std::string& command, const std::string& filename);

    void SetMountPath(const std::string& path);
    void SetDeadlineTimeout(int deadline);

    // Spawn Inotify and HandleCallback thread and block till cleanup
    void Mount(const std::string& filepath);
    
    // Close Inotify fds and terminate inotify thread
    void Unmount();
};

#endif
