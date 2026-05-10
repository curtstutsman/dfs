// DFSClient: inotify watcher, async thread management, CLI command routing.
#include <map>
#include <string>
#include <thread>
#include <fstream>
#include <errno.h>
#include <iostream>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>

#include "src/common/Log.h"
#include "src/common/Path.h"
#include "src/common/Config.h"
#include "src/common/Inotify.h"
#include "src/client/Client.h"
#include "src/client/ClientBase.h"
#include "src/client/ClientNode.h"

DFSClient::DFSClient() {}

DFSClient::~DFSClient() noexcept { this->Unmount(); }

void DFSClient::ProcessCommand(const std::string& command, const std::string& filename) {
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

void DFSClient::InitializeClientNode(const std::string& server_address) {
    this->client_node.CreateStub(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));
}

void DFSClient::SetMountPath(const std::string& path) {
    this->mount_path = dfs_clean_path(path);
    this->client_node.SetMountPath(this->mount_path);
}

void DFSClient::SetDeadlineTimeout(int deadline) {
    this->deadline_timeout = deadline;
    this->client_node.SetDeadlineTimeout(deadline);
}

void DFSClient::Mount(const std::string& filepath) {
    this->mount_path = filepath;
    if (this->mount_path.back() != '/') {
        this->mount_path.append("/");
    }

    dfs_log(LL_SYSINFO) << "Mounting on " << this->mount_path;

    std::vector<std::thread> threads;
    uint event_flags = IN_CREATE | IN_MODIFY | IN_DELETE;

    const FileDescriptor fd = inotify_init();
    if (fd < 0) {
        std::cerr << "inotify_init failed: " << strerror(errno) << std::endl;
        exit(-1);
    }

    const WatchDescriptor wd = inotify_add_watch(fd, filepath.c_str(), event_flags | IN_ONLYDIR);

    std::thread thread_watcher(DFSClient::InotifyWatcher, DFSClient::InotifyEventCallback,
                               event_flags, fd, &this->client_node);
    NotifyStruct n_event = {fd, wd, event_flags, &thread_watcher, DFSClient::InotifyEventCallback};
    events.emplace_back(n_event);
    threads.push_back(std::move(thread_watcher));

    thread_async = std::thread(&DFSClientNode::HandleCallbackList, &this->client_node);
    threads.push_back(std::move(thread_async));

    this->client_node.InitCallbackList();

    for (std::thread& t : threads) {
        if (t.joinable()) { t.join(); }
    }
}

void DFSClient::Unmount() {
    std::vector<FileDescriptor> descriptors;

    this->client_node.Unmount();
    for (NotifyStruct& e : events) {
        if (e.thread->joinable()) { e.thread->detach(); }
        e.thread->~thread();
        inotify_rm_watch(e.wd, e.fd);
        descriptors.push_back(e.fd);
    }

    auto tail = std::ranges::unique(descriptors);
    for (auto it = descriptors.begin(); it != tail.begin(); ++it){
        FileDescriptor fd = *it;
        if (close(fd) != 0) {
            std::cerr << "Unable to close file descriptor" << std::endl;
        }
    }
    events.clear();

    if (thread_async.joinable()) {
        thread_async.detach();
        thread_async.~thread();
    }
}

void DFSClient::InotifyWatcher(InotifyCallback callback, uint event_type,
                                FileDescriptor fd, DFSClientBase* node) {
    int len;
    std::allocator<char> allocator;
    std::unique_ptr<char> handle(allocator.allocate(DFS_I_BUFFER_SIZE));
    char* events_buffer = handle.get();

    while (true) {
        len = read(fd, events_buffer, DFS_I_BUFFER_SIZE);
        int index = 0;

        node->InotifyWatcherCallback([&]{
            while (index < len) {
                inotify_event* event = reinterpret_cast<inotify_event*>(&(events_buffer[index]));
                EventStruct event_data;
                event_data.event = event;
                event_data.instance = node;

                if ((event_type & event->mask) && event->name[0] != '.') {
                    callback(event_type, std::string{node->MountPath() + event->name}, &event_data);
                }

                size_t used = DFS_I_EVENT_SIZE + event->len;
                index += (used / sizeof(char));
            }

            if (errno == EINTR) {
                dfs_log(LL_ERROR) << "inotify interrupted";
            }
        });
    }
}

void DFSClient::InotifyEventCallback(uint event_type, const std::string& filename, void* data) {
    std::string basename = filename.substr(filename.find_last_of("/") + 1);

    auto event_data = reinterpret_cast<EventStruct*>(data);
    inotify_event* event = reinterpret_cast<inotify_event*>(event_data->event);
    DFSClientBase* node = reinterpret_cast<DFSClientBase*>(event_data->instance);

    if (event->mask & IN_CREATE) {
        node->Store(basename);
    } else if (event->mask & IN_MODIFY) {
        node->Store(basename);
    } else if (event->mask & IN_DELETE) {
        node->Delete(basename);
    }
}
