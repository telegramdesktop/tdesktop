#pragma once
#include <sqlite3.h>
#include <vector>
#include <string>
#include <QByteArray>

namespace local {

class KeyManager {
  public:
    static KeyManager& getInstance();

    static void setPath(const char* path);

    KeyManager(const KeyManager&) = delete;
    KeyManager& operator=(const KeyManager&) = delete;

    void setKey(size_t id, const QByteArray& key);
    QByteArray getKey(size_t id);

    bool hasKey(size_t id);

    void clear();

    ~KeyManager();

  private:
    KeyManager();

    constexpr static const char* kCreateTableSql =
        "CREATE TABLE IF NOT EXISTS keys (key_id INTEGER PRIMARY KEY, key TEXT)";
    constexpr static const char* kInsertKeySql = "INSERT INTO keys (key_id, key) VALUES (?, ?)";
    constexpr static const char* kSelectKeySql = "SELECT key FROM keys WHERE key_id = ?";
    constexpr static const char* kSelectKeyIdSql = "SELECT key_id FROM keys WHERE key_id = ?";

    static inline const char* path_ = "keys.sqlite3";

    sqlite3* db_;
};

}  // namespace local