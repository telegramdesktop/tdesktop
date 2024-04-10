#pragma once
#include <QtCore/QByteArray>
#include "key_manager.h"

namespace local {

namespace api {

std::pair<QByteArray, QByteArray> genKeys();
QByteArray encryptPublic(const QByteArray& data, const QByteArray& key);
QByteArray decryptPrivate(const QByteArray& data, const QByteArray& key);
QByteArray genKey();

inline QByteArray base64Encode(const QByteArray& data) { return data.toBase64(); }
inline QByteArray base64Decode(const QByteArray& data) { return QByteArray::fromBase64(data); }

void addPeer(size_t peer_id);
bool hasPeer(size_t peer_id);

size_t getCurrentKeyId(size_t peer_id);

QByteArray getKeyForPeer(size_t peer_id, size_t key_id);
QByteArray getCurrentKeyForPeer(size_t peer_id);

void addKeyForPeer(size_t peer_id, size_t key_id, const QByteArray& key, int key_status = 0);
void changeKeyStatus(size_t peer_id, size_t key_id, int new_key_status);

void addMessageToHide(size_t peer_id, size_t message_id);
bool needToHideMessage(size_t peer_id, size_t message_id);

void addCryptoMessage(size_t peer_id, size_t message_id, size_t key_id);

QByteArray getKeyForCryptoMessage(size_t peer_id, size_t message_id);

QByteArray encryptMessage(size_t peer_id, const QByteArray& content);
QByteArray decryptMessage(size_t peer_id, size_t message_id, const QByteArray& content);

}  // namespace api

std::string whyNoCurrentKey(size_t peer_id);

}  // namespace local
