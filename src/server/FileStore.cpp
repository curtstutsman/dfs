// FileStore: local filesystem operations with virtual hooks for replication subclasses.
#include <filesystem>
#include <sys/stat.h>

#include "src/server/FileStore.hpp"

FileStore::FileStore(const std::string& mount_path)
    : mount_path(mount_path), crc_table(CRC::CRC_32()) {}

std::string FileStore::WrapPath(const std::string& filename) const {
    return mount_path + filename;
}

bool FileStore::IsUnchanged(const std::string& filename, uint32_t crc) const {
    return crc == dfs_file_checksum(WrapPath(filename), &crc_table);
}

std::ofstream FileStore::OpenWrite(const std::string& filename) const {
    return std::ofstream(WrapPath(filename), std::ios::binary | std::ios::trunc);
}

std::ifstream FileStore::OpenRead(const std::string& filename, int64_t& mtime_out) const {
    std::ifstream in(WrapPath(filename), std::ios::binary);
    if (in.is_open()) {
        struct stat st;
        stat(WrapPath(filename).c_str(), &st);
        mtime_out = st.st_mtime;
    }
    return in;
}

bool FileStore::Remove(const std::string& filename) {
    if (std::remove(WrapPath(filename).c_str()) != 0) return false;
    return true;
}

std::vector<FileStore::FileInfo> FileStore::List() const {
    std::vector<FileInfo> result;
    struct stat st;
    for (const auto& entry : std::filesystem::directory_iterator(mount_path)) {
        if (stat(entry.path().c_str(), &st) == 0) {
            result.push_back({entry.path().filename(), st.st_mtime});
        }
    }
    return result;
}

std::optional<FileStore::FileStat> FileStore::Stat(const std::string& filename) const {
    struct stat st;
    if (stat(WrapPath(filename).c_str(), &st) != 0) return std::nullopt;
    return FileStat{st.st_size, st.st_mtime};
}

std::vector<FileStore::FileDetails> FileStore::ListDetails() const {
    std::vector<FileDetails> result;
    struct stat st;
    for (const auto& file : std::filesystem::directory_iterator(mount_path)) {
        if (stat(file.path().c_str(), &st) == 0) {
            result.push_back({
                file.path().filename(),
                st.st_size,
                st.st_mtime,
                dfs_file_checksum(file.path(), &crc_table)
            });
        }
    }
    return result;
}
