#pragma once
#include <openssl/aes.h>
#include <openssl/rsa.h>
#include <vector>
#include <QtCore/QByteArray>

namespace local {

namespace rsa_2048 {
bool genKeys(QByteArray& public_key, QByteArray& private_key);
bool encryptPublic(const QByteArray& data, const QByteArray& key, QByteArray& encrypted);
bool decryptPrivate(const QByteArray& data, const QByteArray& key, QByteArray& decrypted);
}  // namespace rsa_2048

namespace aes_128 {
bool genKey(QByteArray& key);
bool encrypt(const QByteArray& data, const QByteArray& key, QByteArray& encrypted);
bool decrypt(const QByteArray& data, const QByteArray& key, QByteArray& decrypted);
}  // namespace aes_128

}  // namespace local
