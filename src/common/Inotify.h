// Inotify types: file/watch descriptors, callback signature, event structs.
#ifndef COMMON_INOTIFY_H
#define COMMON_INOTIFY_H

#include <string>
#include <thread>

typedef int FileDescriptor;
typedef int WatchDescriptor;
typedef void (*InotifyCallback)(uint, const std::string&, void*);

struct NotifyStruct {
    FileDescriptor fd;
    WatchDescriptor wd;
    uint event_type;
    std::thread* thread;
    InotifyCallback callback;
};

struct EventStruct {
    void* event;
    void* instance;
};

#endif
