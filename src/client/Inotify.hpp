// Inotify type aliases and watch descriptor structs used by Client.
#ifndef INOTIFY_H
#define INOTIFY_H

#include <thread>

using FileDescriptor  = int;
using WatchDescriptor = int;

struct NotifyStruct {
    FileDescriptor  inotify_descriptor;
    WatchDescriptor wd;
    uint            event_type;
    std::thread*    thread;
};

#endif
