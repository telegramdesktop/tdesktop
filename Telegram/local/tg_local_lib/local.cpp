#include "local.h"
#include "log.h"
#include "coder.h"
#include <vector>

namespace local::api {

std::pair<QByteArray, QByteArray> genKeys() {
    QByteArray public_key, private_key;
    if (rsa_2048::genKeys(public_key, private_key)) {
        return {base64Encode(public_key), base64Encode(private_key)};
    }
    log::write("ERROR: Failed to generate keys");
    return {};
}

QByteArray encryptPublic(const QByteArray& data, const QByteArray& key) {
    QByteArray encrypted;
    if (rsa_2048::encryptPublic(base64Decode(data), base64Decode(key), encrypted)) {
        return base64Encode(encrypted);
    }
    log::write("ERROR: Failed to encrypt data with public key");
    return {};
}

QByteArray decryptPrivate(const QByteArray& data, const QByteArray& key) {
    QByteArray decrypted;
    if (rsa_2048::decryptPrivate(base64Decode(data), base64Decode(key), decrypted)) {
        return base64Encode(decrypted);
    }
    log::write("ERROR: Failed to decrypt data with private key");
    return {};
}

QByteArray genKey() {
    QByteArray key;
    if (aes_128::genKey(key)) {
        return base64Encode(key);
    }
    log::write("ERROR: Failed to generate key");
    return {};
}

bool hasPeer(size_t peer_id) { return KeyManager::getInstance().hasPeer(peer_id); }

void addPeer(size_t peer_id) {
    if (!hasPeer(peer_id)) {
        // add with current key == 0 (no keys) because it's new peer
        KeyManager::getInstance().setPeer(peer_id);
    } else {
        log::write("WARNING: Peer with peer id:", peer_id, " already exists");
    }
}

size_t getCurrentKeyId(size_t peer_id) {
    if (auto current_key_id = KeyManager::getInstance().getCurentKeyId(peer_id)) {
        return current_key_id.value();
    }
    log::write("ERROR: No peer with peer id:", peer_id);
    return 0;
}

QByteArray getKeyForPeer(size_t peer_id, size_t key_id) {
    auto key = KeyManager::getInstance().getKeyForPeer(peer_id, key_id);
    if (!key.empty()) {
        return QByteArray(reinterpret_cast<const char*>(key.data()), key.size());
    }
    log::write("ERROR: No key for peer: ", peer_id, " with key_id: ", key_id);
    return {};
}

QByteArray getCurrentKeyForPeer(size_t peer_id) {
    auto key = KeyManager::getInstance().getCurrentKeyForPeer(peer_id);
    if (!key.empty()) {
        return QByteArray(reinterpret_cast<const char*>(key.data()), key.size());
    }
    log::write("ERROR: No current key for peer: ", peer_id);
    return {};
}

void addKeyForPeer(size_t peer_id, size_t key_id, const QByteArray& key, int key_status) {
    if (hasPeer(peer_id)) {
        QByteArray tmp = base64Decode(key);
        std::vector<char> key_data(tmp.begin(), tmp.end());
        KeyManager::getInstance().setPeerPassword(peer_id, key_id, key_data, key_status);
    } else {
        log::write("ERROR: No peer with peer id:", peer_id);
    }
}

void changeKeyStatus(size_t peer_id, size_t key_id, int new_key_status) {
    if (!KeyManager::getInstance().changeKeyStatus(peer_id, key_id, new_key_status)) {
        log::write(
            "ERROR: Failed to change key status for peer: ", peer_id, " with key_id: ", key_id);
    }
}

void addMessageToHide(size_t peer_id, size_t message_id) {
    if (hasPeer(peer_id)) {
        KeyManager::getInstance().setMessageToHide(message_id, peer_id);
    } else {
        log::write("ERROR: No peer with peer id:", peer_id);
    }
}

bool needToHideMessage(size_t peer_id, size_t message_id) {
    return KeyManager::getInstance().hasMessageToHide(peer_id, message_id);
}

void addCryptoMessage(size_t peer_id, size_t message_id, size_t key_id) {
    if (hasPeer(peer_id)) {
        KeyManager::getInstance().setCryptoMessage(peer_id, message_id, key_id);
    } else {
        log::write("ERROR: No peer with peer id:", peer_id);
    }
}

QByteArray getKeyForCryptoMessage(size_t peer_id, size_t message_id) {
    auto key = KeyManager::getInstance().getKeyForCryptoMessage(peer_id, message_id);
    if (!key.empty()) {
        return QByteArray(reinterpret_cast<const char*>(key.data()), key.size());
    }
    log::write("WARNING: No key for crypto message: ", message_id, " for peer: ", peer_id);
    return {};
}

QByteArray encryptMessage(size_t peer_id, const QByteArray& content) {
    auto key = getCurrentKeyForPeer(peer_id);

    // no current key -> won't encrypt
    if (key.isEmpty()) {
        log::write(
            "ERROR: Did not encrypt message for peer: ",
            peer_id,
            " because ",
            whyNoCurrentKey(peer_id));
        return content;
    }

    key = QByteArray(reinterpret_cast<const char*>(key.data()), key.size());

    // encrypt
    QByteArray encrypted;
    if (aes_128::encrypt(content, key, encrypted)) {
        return base64Encode(encrypted);
    }
    log::write("ERROR: Failed to encrypt message for peer: ", peer_id);
    return content;
}

QByteArray decryptMessage(size_t peer_id, size_t message_id, const QByteArray& content) {
    auto key = getKeyForCryptoMessage(peer_id, message_id);

    if (key.isEmpty()) {
        // no key -> it's new message -> write it and decrypt with current key
        auto current_key_id = KeyManager::getInstance().getCurentKeyId(peer_id).value();
        addCryptoMessage(
            peer_id, message_id, KeyManager::getInstance().getCurentKeyId(peer_id).value());
        key = getCurrentKeyForPeer(peer_id);
        // no current key -> won't decrypt
        if (key.isEmpty()) {
            log::write(
                "WARNING: Did not decrypt message for peer: ",
                peer_id,
                "with message_id: ",
                message_id,
                " because ",
                whyNoCurrentKey(peer_id));
            return content;
        }
    }

    key = QByteArray(reinterpret_cast<const char*>(key.data()), key.size());

    // decrypt
    QByteArray decrypted;
    if (aes_128::decrypt(base64Decode(content), key, decrypted)) {
        return decrypted;
    }
    log::write(
        "ERROR: Failed to decrypt message for peer: ", peer_id, " with message_id: ", message_id);
    return content;
}
}  // namespace local::api

std::string local::whyNoCurrentKey(size_t peer_id) {
    if (auto current_key_id = KeyManager::getInstance().getCurentKeyId(peer_id)) {
        if (current_key_id == 0) {
            return "no keys";
        } else {
            return "no key with id: " + std::to_string(current_key_id.value());
        }
    }
    return "no such peer";
}
