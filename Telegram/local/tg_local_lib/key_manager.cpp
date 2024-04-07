#include "key_manager.h"
#include <iostream>

namespace local {

KeyManager& KeyManager::getInstance() {
    static KeyManager instance;
    return instance;
}

void KeyManager::setPath(const char* path) { path_ = path; }

KeyManager::KeyManager() {
    int rc = sqlite3_open(path_, &db_);
    if (rc != SQLITE_OK) {
        // HANDLE ERROR
        return;
    }
    rc = sqlite3_exec(db_, kCreateTableSql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        // HANDLE ERROR
        return;
    }
}

KeyManager::~KeyManager() { sqlite3_close(db_); }

void KeyManager::setKey(size_t id, const QByteArray& key) {
    if (hasKey(id)) {
        // HANDLE ERROR
        return;
    }
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kInsertKeySql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        // HANDLE ERROR
        return;
    }
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_blob(stmt, 2, key.data(), key.size(), SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        // HANDLE ERROR
        return;
    }
    sqlite3_finalize(stmt);
}

QByteArray KeyManager::getKey(size_t id) {
    if (!hasKey(id)) {
        // HANDLE ERROR
        return {};
    }
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kSelectKeySql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        // HANDLE ERROR
        return {};
    }
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        // HANDLE ERROR
        sqlite3_finalize(stmt);
        return {};
    }
    const void* keyData = sqlite3_column_blob(stmt, 0);
    int keySize = sqlite3_column_bytes(stmt, 0);
    QByteArray key((char*)keyData, keySize);
    sqlite3_finalize(stmt);
    return key;
}

bool KeyManager::hasKey(size_t id) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kSelectKeyIdSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        // HANDLE ERROR
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

void KeyManager::clear() {
    int rc = sqlite3_exec(db_, "DELETE FROM keys", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        // HANDLE ERROR
        return;
    }
}

}  // namespace local
