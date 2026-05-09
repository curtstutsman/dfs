// Level-filtered logging: DFSLog stream class and dfs_log() macro.
#ifndef COMMON_LOG_H
#define COMMON_LOG_H

#include <string>
#include <sstream>
#include <iostream>

enum dfs_log_level_e { LL_SYSINFO, LL_ERROR, LL_DEBUG, LL_DEBUG2, LL_DEBUG3 };

class DFSLog {
private:
    std::ostringstream buffer;

public:
    DFSLog(dfs_log_level_e level = LL_ERROR) {
        std::string desc = level == LL_SYSINFO ? "-- SYSINFO"
                         : level == LL_ERROR   ? "!! ERROR"
                                               : ">> DEBUG";
        buffer << desc << ((level > 1) ? std::to_string(level - 1) : "") << ": ";
    }

    template <typename T>
    DFSLog& operator<<(T const& value) {
        buffer << value;
        return *this;
    }

    ~DFSLog() {
        buffer << std::endl;
        std::cerr << buffer.str();
    }
};

extern dfs_log_level_e DFS_LOG_LEVEL;

#define dfs_log(level) if (level > DFS_LOG_LEVEL) ; else DFSLog(level)

#endif
