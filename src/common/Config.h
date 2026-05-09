// Compile-time constants shared across client and server.
#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include <sys/inotify.h>

#define DFS_RESET_TIMEOUT  4000
#define DFS_I_EVENT_SIZE   (sizeof(struct inotify_event))
#define DFS_I_BUFFER_SIZE  (1024 * (DFS_I_EVENT_SIZE + 16))
#define CHUNK_SIZE         4096

#endif
