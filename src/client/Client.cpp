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
    } else if (command == "fetch") {
        client_node.Fetch(filename);
    } else if (command == "store") {
        client_node.Store(filename);
    } else if (command == "delete") {
        client_node.Delete(filename);
    } else if (command == "list") {
        std::map<std::string,int> file_map;
        client_node.List(&file_map, true);
    } else if (command == "stat") {
        client_node.Stat(filename);
    } else {
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
    this->deadline_timeout = deadline;
    client_node.SetDeadlineTimeout(deadline);
}

void Client::Mount(const std::string& filepath) {
    dfs_log(LL_SYSINFO) << "Mounting on " << this->mount_path;
    std::vector<std::thread> threads;

    const FileDescriptor inotify_descriptor = inotify_init();
    if (inotify_descriptor < 0) {
        std::cerr << "inotify_init failed: " << strerror(errno) << std::endl;
        exit(-1);
    }
    uint event_flags = IN_CREATE | IN_MODIFY | IN_DELETE;
    const WatchDescriptor wd = inotify_add_watch(inotify_descriptor, filepath.c_str(), event_flags | IN_ONLYDIR);
    if (wd < 0) {
        std::cerr << "inotify_add_watch failed: " << strerror(errno) << std::endl;
        exit(-1);
    }

    // Need to pass 'this' so InotifyWatcher knows which Client obj called it to access correct client_node
    std::thread thread_watcher(&Client::InotifyWatcher, this, event_flags, inotify_descriptor);
    events.emplace_back(NotifyStruct{inotify_descriptor, wd, event_flags, &thread_watcher});
    threads.push_back(std::move(thread_watcher));

    thread_async = std::thread(&ClientNode::HandleCallbackList, &client_node);
    threads.push_back(std::move(thread_async));
    client_node.InitCallbackList();

    for (std::thread& t : threads) {
        if (t.joinable()) { t.join(); }
    }
}

void Client::Unmount() {
    std::vector<FileDescriptor> descriptors;

    client_node.Unmount();
    for (NotifyStruct& e : events) {
        if (e.thread->joinable()) { e.thread->detach(); }
        e.thread->~thread();
        inotify_rm_watch(e.wd, e.inotify_descriptor);
        descriptors.push_back(e.inotify_descriptor);
    }

    auto tail = std::ranges::unique(descriptors);
    for (auto it = descriptors.begin(); it != tail.begin(); ++it) {
        if (close(*it) != 0) {
            std::cerr << "Unable to close file descriptor" << std::endl;
        }
    }
    events.clear();

    if (thread_async.joinable()) {
        thread_async.detach();
        thread_async.~thread();
    }
}

void Client::InotifyWatcher(uint event_type, FileDescriptor inotify_descriptor) {
    int len;
    std::allocator<char> allocator;
    std::unique_ptr<char> handle(allocator.allocate(DFS_I_BUFFER_SIZE));
    char* events_buffer = handle.get();

    while (true) {
        len = read(inotify_descriptor, events_buffer, DFS_I_BUFFER_SIZE);
        int index = 0;

        client_node.Synchronized([&] {
            while (index < len) {
                inotify_event* event = reinterpret_cast<inotify_event*>(&events_buffer[index]);
                if ((event_type & event->mask) && event->name[0] != '.') {
                    if (event->mask & IN_CREATE || event->mask & IN_MODIFY) {
                        client_node.Store(event->name);
                    } else if (event->mask & IN_DELETE) {
                        client_node.Delete(event->name);
                    }
                }
                index += DFS_I_EVENT_SIZE + event->len;
            }
            if (errno == EINTR) {
                dfs_log(LL_ERROR) << "inotify interrupted";
            }
        });
    }
}

