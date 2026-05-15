// Common utilities: constants, logging, CRC32, path normalization.
#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/inotify.h>
#include <sys/stat.h>

#define CRCPP_USE_CPP11
#include "src/vendor/CRC.h"

// ── Constants ────────────────────────────────────────────────────────────────

inline constexpr int         kResetTimeoutMs  = 4000;
inline constexpr int         kDefaultDeadline = 1000;
inline constexpr std::size_t kIEventSize      = sizeof(inotify_event);
inline constexpr std::size_t kIBufferSize     = 1024 * (kIEventSize + 16);
inline constexpr std::size_t kChunkSize       = 4096;
inline constexpr std::size_t kCrcBufSize      = 2048;

// ── Logging ──────────────────────────────────────────────────────────────────

enum class dfs_log_level_e { LL_SYSINFO = 0, LL_ERROR = 1, LL_DEBUG = 2, LL_DEBUG2 = 3, LL_DEBUG3 = 4 };
using enum dfs_log_level_e;

inline constexpr bool operator>(dfs_log_level_e a, dfs_log_level_e b) noexcept {
    return std::to_underlying(a) > std::to_underlying(b);
}

inline dfs_log_level_e DFS_LOG_LEVEL = LL_ERROR;

class DFSLog {
    std::ostringstream buffer;
public:
    DFSLog(dfs_log_level_e level = LL_ERROR) {
        auto ul = std::to_underlying(level);
        std::string desc = level == LL_SYSINFO ? "-- SYSINFO"
                         : level == LL_ERROR   ? "!! ERROR"
                                               : ">> DEBUG";
        buffer << desc << (ul > 1 ? std::to_string(ul - 1) : "") << ": ";
    }
    template <typename T>
    DFSLog& operator<<(T const& value) { buffer << value; return *this; }
    ~DFSLog() { buffer << std::endl; std::cerr << buffer.str(); }
};

#define dfs_log(level) if (level > DFS_LOG_LEVEL) ; else DFSLog(level)

// ── CRC32 checksum ───────────────────────────────────────────────────────────

inline std::uint32_t dfs_file_checksum(const std::string& filepath,
                                       CRC::Table<std::uint32_t, 32>* table) {
    struct stat st;
    if (lstat(filepath.c_str(), &st) != 0) {
        return 0;
    }

    std::size_t file_size   = static_cast<std::size_t>(st.st_size);
    std::size_t buffer_size = (file_size < kCrcBufSize) ? std::max(file_size / 2, std::size_t{1}) : kCrcBufSize;
    std::vector<char> buf(buffer_size);
    std::ifstream stream(filepath, std::ios::in | std::ios::binary);

    std::uint32_t crc = 0;
    while (stream.read(buf.data(), static_cast<std::streamsize>(buffer_size)) || stream.gcount()) {
        crc = CRC::Calculate(buf.data(), static_cast<std::size_t>(stream.gcount()), *table, crc);
    }
    return crc;
}

// ── Path normalization ───────────────────────────────────────────────────────

inline std::string dfs_clean_path(const std::string& path) {
    return (!path.empty() && path.back() != '/') ? path + '/' : path;
}

#endif
