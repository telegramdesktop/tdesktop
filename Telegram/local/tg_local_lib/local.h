#pragma once
#include <utility>
#include <QByteArray>
#include "coder.h"
#include "key_manager.h"

namespace local {
namespace api {
std::pair<QByteArray, QByteArray> genKeys();
QByteArray encryptPublic(const QByteArray& data, const QByteArray& key);
QByteArray decryptPrivate(const QByteArray& data, const QByteArray& key);

QByteArray genKey();
QByteArray encrypt(const QByteArray& data, const QByteArray& key);
QByteArray decrypt(const QByteArray& data, const QByteArray& key);

void setDbPath(const char* path);
void setKey(size_t id, const QByteArray& key);
QByteArray getKey(size_t id);
bool hasKey(size_t id);
};  // namespace api
}  // namespace local
