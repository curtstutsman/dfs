// Mount path normalization: ensures paths end with '/'.
#ifndef COMMON_PATH_H
#define COMMON_PATH_H

#include <string>

inline std::string dfs_clean_path(const std::string& path) {
    std::string sep = "/";
    std::string mount_path = path;
    if (mount_path.length() >= 1 &&
        mount_path.compare(mount_path.length() - sep.length(), sep.length(), sep) != 0) {
        mount_path += "/";
    }
    return mount_path;
}

#endif
