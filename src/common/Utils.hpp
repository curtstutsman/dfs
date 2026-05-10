// Common utilities: constants, logging, CRC32, path normalization, inotify types.
#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <sys/inotify.h>
#include <sys/stat.h>

#define CRCPP_USE_CPP11
#include "src/vendor/CRC.h"

// ── Constants ────────────────────────────────────────────────────────────────

#define DFS_RESET_TIMEOUT  4000
#define DFS_I_EVENT_SIZE   (sizeof(struct inotify_event))
#define DFS_I_BUFFER_SIZE  (1024 * (DFS_I_EVENT_SIZE + 16))
#define CHUNK_SIZE         4096

// ── Logging ──────────────────────────────────────────────────────────────────

enum dfs_log_level_e { LL_SYSINFO, LL_ERROR, LL_DEBUG, LL_DEBUG2, LL_DEBUG3 };

inline dfs_log_level_e DFS_LOG_LEVEL = LL_ERROR;

class DFSLog {
    std::ostringstream buffer;
public:
    DFSLog(dfs_log_level_e level = LL_ERROR) {
        std::string desc = level == LL_SYSINFO ? "-- SYSINFO"
                         : level == LL_ERROR   ? "!! ERROR"
                                               : ">> DEBUG";
        buffer << desc << ((level > 1) ? std::to_string(level - 1) : "") << ": ";
    }
    template <typename T>
    DFSLog& operator<<(T const& value) { buffer << value; return *this; }
    ~DFSLog() { buffer << std::endl; std::cerr << buffer.str(); }
};

#define dfs_log(level) if (level > DFS_LOG_LEVEL) ; else DFSLog(level)

// ── CRC32 checksum ───────────────────────────────────────────────────────────

#define DFS_BUFFERSIZE 2048

inline std::uint32_t dfs_file_checksum(const std::string& filepath,
                                       CRC::Table<std::uint32_t, 32>* table) {
    struct stat st;
    std::uint32_t crc = 0;
    std::ifstream stream;
    uint32_t chunk_count = 0;
    uint32_t chunk_sequence = 0;
    std::ifstream::pos_type current_position = 0;

    stream.seekg(0, std::ios::beg);
    if (lstat(filepath.c_str(), &st) != 0) return 0;

    size_t file_size = st.st_size;
    std::uint32_t buffer_size = DFS_BUFFERSIZE;

    if (file_size < DFS_BUFFERSIZE) {
        buffer_size = static_cast<uint32_t>(file_size / 2);
        if (buffer_size <= 0) buffer_size = 1;
    }

    char buffer[buffer_size];
    chunk_count = static_cast<uint32_t>(file_size / buffer_size) +
                  static_cast<uint32_t>(static_cast<bool>(file_size % buffer_size));

    stream.open(filepath, std::ios::in | std::ios::binary);
    if (!stream.is_open()) return 0;

    while (chunk_count != chunk_sequence) {
        size_t read_size = (file_size - current_position < buffer_size)
            ? file_size - current_position
            : buffer_size;
        if (!stream.read(buffer, read_size)) return crc;
        crc = CRC::Calculate(buffer, sizeof(char) * read_size, *table, crc);
        chunk_sequence++;
        current_position = stream.tellg();
    }
    return crc;
}

// ── Path normalization ───────────────────────────────────────────────────────

inline std::string dfs_clean_path(const std::string& path) {
    return (!path.empty() && path.back() != '/') ? path + '/' : path;
}

#endif
