#pragma once

#include <unordered_map>

#include "data/data_peer_id.h"


namespace Data {
    class Session;

    class EncryptSettings {
        const std::string secret_file = "tg-secret.txt";
        std::unordered_map<PeerId, std::string> secrets;
        const not_null<Session*> _owner;

        void loadFile();
        void saveToFile();

    public:
        explicit EncryptSettings(not_null<Session*> owner);

        std::optional<std::string> requestKey(PeerId peer);

        void storeKey(PeerId peer, const std::string &key);
    };
}
