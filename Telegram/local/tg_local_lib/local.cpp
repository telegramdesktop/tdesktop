#include "local.h"
#include "coder.h"
#include <iostream>

namespace local::api {

std::pair<QByteArray, QByteArray> genKeys() {
    QByteArray public_key, private_key;
    local::rsa_2048::genKeys(public_key, private_key);
    return {public_key, private_key};
}

QByteArray encryptPublic(const QByteArray& data, const QByteArray& key) {
    QByteArray encrypted;
    local::rsa_2048::encryptPublic(data, key, encrypted);
    return encrypted;
}

QByteArray decryptPrivate(const QByteArray& data, const QByteArray& key) {
    QByteArray decrypted;
    local::rsa_2048::decryptPrivate(data, key, decrypted);
    return decrypted;
}

QByteArray genKey() {
    QByteArray key;
    local::aes_128::genKey(key);
    return key;
}

QByteArray encrypt(const QByteArray& data, const QByteArray& key) {
    QByteArray encrypted;
    local::aes_128::encrypt(data, key, encrypted);
    return encrypted;
}

QByteArray decrypt(const QByteArray& data, const QByteArray& key) {
    QByteArray decrypted;
    local::aes_128::decrypt(data, key, decrypted);
    return decrypted;
}

void setDbPath(const char* path) { KeyManager::setPath(path); }
void setKey(size_t id, const QByteArray& key) { local::KeyManager::getInstance().setKey(id, key); }
QByteArray getKey(size_t id) { return local::KeyManager::getInstance().getKey(id); }
bool hasKey(size_t id) { return local::KeyManager::getInstance().hasKey(id); }

}  // namespace local::api
