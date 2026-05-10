// FileStore: filesystem operations and replication extension points, decoupled from gRPC.
#ifndef SERVER_FILE_STORE_H
#define SERVER_FILE_STORE_H

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "src/common/Utils.hpp"

class FileStore {
protected:
    std::string mount_path;
    mutable CRC::Table<std::uint32_t, 32> crc_table;  // mutable: precomputed table, not logical state

    std::string WrapPath(const std::string& filename) const;

public:
    explicit FileStore(const std::string& mount_path);
    virtual ~FileStore() = default;

    bool IsUnchanged(const std::string& filename, uint32_t crc) const;
    std::ofstream OpenWrite(const std::string& filename) const;
    std::ifstream OpenRead(const std::string& filename, int64_t& mtime_out) const;
    bool Remove(const std::string& filename);  // calls AfterDelete on success

    struct FileInfo    { std::string name; int64_t mtime; };
    struct FileStat    { int64_t size;  int64_t mtime; };
    struct FileDetails { std::string name; int64_t size; int64_t mtime; uint32_t crc; };

    std::vector<FileInfo>    List() const;
    std::optional<FileStat>  Stat(const std::string& filename) const;
    std::vector<FileDetails> ListDetails() const;

    // Override in subclasses to add replication (e.g. ReplicatedFileStore)
    virtual void AfterWrite(const std::string& filename) {}
    virtual void AfterDelete(const std::string& filename) {}
};

#endif
