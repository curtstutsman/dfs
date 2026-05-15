// Client: inotify watcher, async thread management, CLI command routing.
#include <map>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>

#include "src/common/Utils.hpp"
#include "src/client/Client.hpp"

Client::Client() {}

Client::~Client() noexcept { this->Unmount(); }

void Client::ProcessCommand(const std::string& command, const std::string& filename) {
    if (command == "mount") {
        Mount(this->mount_path);
    } 
    else if (command == "fetch") {
        if (client_node.Fetch(filename) != grpc::StatusCode::OK) {
            dfs_log(LL_ERROR) << "fetch failed: " << filename;
        }
    } 
    else if (command == "store") {
        if (client_node.Store(filename) != grpc::StatusCode::OK) {
            dfs_log(LL_ERROR) << "store failed: " << filename;
        }
    } 
    else if (command == "delete") {
        if (client_node.Delete(filename) != grpc::StatusCode::OK) {
            dfs_log(LL_ERROR) << "delete failed: " << filename;
        }
    } 
    else if (command == "list") {
        std::map<std::string,int64_t> file_map;
        if (client_node.List(&file_map, true) != grpc::StatusCode::OK) {
            dfs_log(LL_ERROR) << "list failed";
        }
    } 
    else if (command == "stat") {
        if (client_node.Stat(filename) != grpc::StatusCode::OK) {
            dfs_log(LL_ERROR) << "stat failed: " << filename;
        }
    } 
    else {
        dfs_log(LL_ERROR) << "Unknown command: " << command;
    }
}

void Client::InitializeClientNode(const std::string& server_address) {
    client_node.CreateStub(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));
}

void Client::SetMountPath(const std::string& path) {
    this->mount_path = dfs_clean_path(path);
    client_node.SetMountPath(this->mount_path);
}

void Client::SetDeadlineTimeout(int deadline) {
    client_node.SetDeadlineTimeout(deadline);
}

void Client::Mount(const std::string& filepath) {
    client_node.SyncFromServer();
    dfs_log(LL_SYSINFO) << "Mounting on " << this->mount_path;

    const FileDescriptor inotify_descriptor = inotify_init();
    if (inotify_descriptor < 0) {
        std::cerr << "inotify_init failed: " << strerror(errno) << std::endl;
        exit(-1);
    }
    unsigned event_flags = IN_CREATE | IN_MODIFY | IN_DELETE;
    const WatchDescriptor wd = inotify_add_watch(inotify_descriptor, filepath.c_str(), event_flags | IN_ONLYDIR);
    if (wd < 0) {
        std::cerr << "inotify_add_watch failed: " << strerror(errno) << std::endl;
        exit(-1);
    }
    // Need to pass 'this' so InotifyWatcher knows which Client obj called it to access correct client_node
    std::thread thread_watcher(&Client::InotifyWatcher, this, event_flags, inotify_descriptor);
    events.emplace_back(NotifyStruct{inotify_descriptor, wd, event_flags, std::move(thread_watcher)});

    std::thread thread_async(&ClientNode::HandleCallbackList, &client_node);
    client_node.InitCallbackList();
    thread_async.join();        // Block until async thread finishes 
}

void Client::Unmount() {
    client_node.Shutdown();
    for (NotifyStruct& e : events) {
        inotify_rm_watch(e.inotify_descriptor, e.wd);
        close(e.inotify_descriptor);
        if (e.thread.joinable()) { 
            e.thread.join(); 
        }
    }
    events.clear();
}

void Client::InotifyWatcher(unsigned event_flags, FileDescriptor inotify_descriptor) {
    ssize_t bytes_read;
    std::unique_ptr<char[]> handle = std::make_unique<char[]>(kIBufferSize);
    char* events_buffer = handle.get();

    while (true) {
        bytes_read = read(inotify_descriptor, events_buffer, kIBufferSize);
        if (bytes_read <= 0) break;
        int event_index = 0;
        /// \todo Instead of obtaining a global server lock in the client node, look into
        /// a queue workload approach. Inotify and HandleCallback can both add to queue
        /// and consumer can get execute the gRPC services. Eliminates file data race issue
        /// if we only have one consumer
        client_node.Synchronized([&] {
            while (event_index < bytes_read) {
                inotify_event* event = reinterpret_cast<inotify_event*>(&events_buffer[event_index]);
                // Verify event time and filename
                if ((event_flags & event->mask) && event->name[0] != '.') {
                    if (event->mask & IN_CREATE || event->mask & IN_MODIFY) {
                        client_node.Store(event->name);
                    } 
                    else if (event->mask & IN_DELETE) {
                        client_node.Delete(event->name);
                    }
                }
                event_index += static_cast<int>(kIEventSize) + event->len;
            }
            if (errno == EINTR) {
                dfs_log(LL_ERROR) << "inotify interrupted";
            }
        });
    }
}

