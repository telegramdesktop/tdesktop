#include "key_manager.h"

namespace local {

KeyManager& KeyManager::getInstance() {
    static KeyManager instance;
    return instance;
}

void KeyManager::setPeer(size_t peer_id, size_t current_key_id) {
    db_.replace(Peer{peer_id, current_key_id});
}

std::unique_ptr<Peer> KeyManager::getPeer(size_t peer_id) {
    return db_.get_pointer<Peer>(where(c(&Peer::peer_id) == peer_id));
}

std::optional<size_t> KeyManager::getCurentKeyId(size_t peer_id) {
    auto peer = getPeer(peer_id);
    if (peer) {
        return peer->current_key_id;
    }
    return std::nullopt;
}

void KeyManager::setCurrentKeyId(size_t peer_id, size_t current_key_id) {
    auto peer = getPeer(peer_id);
    if (peer) {
        peer->current_key_id = current_key_id;
        db_.update(*peer);
    }
}

bool KeyManager::hasPeer(size_t peer_id) { return getPeer(peer_id) != nullptr; }

void KeyManager::setPeerPassword(
    size_t peer_id, size_t key_index, const std::vector<char>& key, int key_status) {
    db_.replace(PeerPassword{std::make_shared<size_t>(peer_id), key_index, key, key_status});
}

std::unique_ptr<PeerPassword> KeyManager::getPeerPassword(size_t peer_id, size_t key_id) {
    auto peer_password = db_.get_pointer<PeerPassword>(
        where(c(&PeerPassword::peer_id) == peer_id and c(&PeerPassword::key_id) == key_id));
    return peer_password;
}

std::vector<char> KeyManager::getKeyForPeer(size_t peer_id, size_t key_id) {
    if (auto peer_password = getPeerPassword(peer_id, key_id)) {
        return peer_password->key;
    }
    return {};
}

std::vector<char> KeyManager::getCurrentKeyForPeer(size_t peer_id) {
    if (auto current_key_id = getCurentKeyId(peer_id)) {
        return getKeyForPeer(peer_id, *current_key_id);
    }
    return {};
}

bool KeyManager::changeKeyStatus(size_t peer_id, size_t key_id, int new_key_status) {
    auto peer_password = getPeerPassword(peer_id, key_id);
    if (peer_password) {
        peer_password->key_status = new_key_status;
        db_.update(*peer_password);
        return true;
    }
    return false;
}

void KeyManager::setMessageToHide(size_t peer_id, size_t message_id) {
    db_.replace(MessageToHide{std::make_shared<size_t>(peer_id), message_id});
}

std::unique_ptr<MessageToHide> KeyManager::getMessageToHide(size_t message_id, size_t peer_id) {
    return db_.get_pointer<MessageToHide>(where(
        c(&MessageToHide::message_id) == message_id and c(&MessageToHide::peer_id) == peer_id));
}

bool KeyManager::hasMessageToHide(size_t peer_id, size_t message_id) {
    return getMessageToHide(message_id, peer_id) != nullptr;
}

void KeyManager::setCryptoMessage(size_t peer_id, size_t message_id, size_t key_id) {
    db_.replace(CryptoMessage{std::make_shared<size_t>(peer_id), message_id, key_id});
}

std::unique_ptr<CryptoMessage> KeyManager::getCryptoMessage(size_t peer_id, size_t message_id) {
    return db_.get_pointer<CryptoMessage>(where(
        c(&CryptoMessage::peer_id) == peer_id and c(&CryptoMessage::message_id) == message_id));
}

std::optional<size_t> KeyManager::getKeyIdForCryptoMessage(size_t peer_id, size_t message_id) {
    auto crypto_message = getCryptoMessage(peer_id, message_id);
    if (crypto_message) {
        return crypto_message->key_id;
    }
    return std::nullopt;
}

std::vector<char> KeyManager::getKeyForCryptoMessage(size_t peer_id, size_t message_id) {
    auto crypto_message = getCryptoMessage(peer_id, message_id);
    if (crypto_message) {
        return getKeyForPeer(peer_id, crypto_message->key_id);
    }
    return {};
}

KeyManager::KeyManager() { db_.sync_schema(); }

}  // namespace local
