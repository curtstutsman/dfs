// Per-file write locks: maps filename -> client_id; enforced before Store/Delete.
#ifndef SERVER_LOCK_MANAGER_H
#define SERVER_LOCK_MANAGER_H

#include <mutex>
#include <string>
#include <unordered_map>

class LockManager {
private:
    std::unordered_map<std::string, std::string> lock_table;
    std::mutex mutex;

public:
    bool acquire(const std::string& filename, const std::string& client_id) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = lock_table.find(filename);
        if (it == lock_table.end() || it->second == client_id) {
            lock_table[filename] = client_id;
            return true;
        }
        return false;
    }

    bool release(const std::string& filename, const std::string& client_id) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = lock_table.find(filename);
        if (it != lock_table.end() && it->second == client_id) {
            lock_table.erase(it);
            return true;
        }
        return false;
    }

    bool isHolding(const std::string& filename, const std::string& client_id) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = lock_table.find(filename);
        return it != lock_table.end() && it->second == client_id;
    }
};

#endif
