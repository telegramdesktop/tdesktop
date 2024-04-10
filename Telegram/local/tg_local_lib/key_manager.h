#include <sqlite_orm/sqlite_orm.h>
#include <string>
#include <memory>
#include <vector>

#define PATH "."

using namespace sqlite_orm;

namespace local {

struct Peer {
    size_t peer_id;
    size_t current_key_id;
};

struct PeerPassword {
    std::shared_ptr<size_t> peer_id;
    size_t key_id;
    std::vector<char> key;
    int key_status;
};

struct CryptoMessage {
    std::shared_ptr<size_t> peer_id;
    size_t message_id;
    size_t key_id;
};

struct MessageToHide {
    std::shared_ptr<size_t> peer_id;
    size_t message_id;
};

inline auto genPeersTable() {
    return make_table(
        "Peers",
        make_column("peer_id", &Peer::peer_id, primary_key()),
        make_column("current_key_id", &Peer::current_key_id, default_value(0)));
}

inline auto genPeerPasswordsTable() {
    return make_table(
        "PeerPasswords",
        make_column("peer_id", &PeerPassword::peer_id),
        make_column("key_index", &PeerPassword::key_id),
        make_column("key", &PeerPassword::key),
        make_column("key_status", &PeerPassword::key_status),
        primary_key(&PeerPassword::peer_id, &PeerPassword::key_id),
        foreign_key(&PeerPassword::peer_id).references(&Peer::peer_id));
}

inline auto genCryptoMessagesTable() {
    return make_table(
        "CryptoMessages",
        make_column("peer_id", &CryptoMessage::peer_id),
        make_column("message_id", &CryptoMessage::message_id),
        make_column("key_id", &CryptoMessage::key_id),
        primary_key(&CryptoMessage::peer_id, &CryptoMessage::message_id),
        foreign_key(&CryptoMessage::peer_id).references(&Peer::peer_id));
}

inline auto genMessagesToHideTable() {
    return make_table(
        "MessagesToHide",
        make_column("peer_id", &MessageToHide::peer_id),
        make_column("message_id", &MessageToHide::message_id),
        primary_key(&MessageToHide::peer_id, &MessageToHide::message_id),
        foreign_key(&MessageToHide::peer_id).references(&Peer::peer_id));
}

inline auto genDB(std::string path = PATH) {
    return make_storage(
        path + "/keys.db",
        genPeersTable(),
        genPeerPasswordsTable(),
        genMessagesToHideTable(),
        genCryptoMessagesTable());
}

class KeyManager {
  public:
    static KeyManager& getInstance();

    KeyManager(const KeyManager&) = delete;
    KeyManager& operator=(const KeyManager&) = delete;

    void setPeer(size_t peer_id, size_t current_key_id = 0);
    std::unique_ptr<Peer> getPeer(size_t peer_id);
    std::optional<size_t> getCurentKeyId(size_t peer_id);
    void setCurrentKeyId(size_t peer_id, size_t current_key_id);
    bool hasPeer(size_t peer_id);

    void setPeerPassword(
        size_t peer_id, size_t key_id, const std::vector<char>& key, int key_status);

    std::unique_ptr<PeerPassword> getPeerPassword(size_t peer_id, size_t key_id);
    std::vector<char> getKeyForPeer(size_t peer_id, size_t key_id);
    std::vector<char> getCurrentKeyForPeer(size_t peer_id);
    bool changeKeyStatus(size_t peer_id, size_t key_id, int new_key_status);

    void setMessageToHide(size_t peer_id, size_t message_id);
    std::unique_ptr<MessageToHide> getMessageToHide(size_t peer_id, size_t message_id);
    bool hasMessageToHide(size_t peer_id, size_t message_id);

    void setCryptoMessage(size_t peer_id, size_t message_id, size_t key_id);
    std::unique_ptr<CryptoMessage> getCryptoMessage(size_t peer_id, size_t message_id);
    std::optional<size_t> getKeyIdForCryptoMessage(size_t peer_id, size_t message_id);

    std::vector<char> getKeyForCryptoMessage(size_t peer_id, size_t message_id);

    ~KeyManager() = default;

  private:
    KeyManager();

    decltype(genDB()) db_ = genDB();
};

}  // namespace local
