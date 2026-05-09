// CRC32 file checksum used to detect unchanged files before transfer.
#ifndef COMMON_CHECKSUM_H
#define COMMON_CHECKSUM_H

#include <string>
#include <fstream>
#include <sys/stat.h>

#define CRCPP_USE_CPP11
#include "src/vendor/CRC.h"

#define DFS_BUFFERSIZE 2048

inline std::uint32_t dfs_file_checksum(const std::string& filepath, CRC::Table<std::uint32_t, 32>* table) {
    struct stat st;
    std::uint32_t crc = 0;
    std::ifstream stream;
    uint32_t chunk_count = 0;
    uint32_t chunk_sequence = 0;
    std::ifstream::pos_type current_position = 0;

    stream.seekg(0, std::ios::beg);
    if (lstat(filepath.c_str(), &st) != 0) {
        return 0;
    }

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
        crc = CRC::Calculate(buffer, sizeof(char) * buffer_size, *table, crc);
        chunk_sequence++;
        current_position = stream.tellg();
    }

    return crc;
}

#endif
