#include "data/encrypt/data_encrypt_settings.h"

#include <fstream>
#include <iostream>

#include "data/data_session.h"

namespace Data {

    EncryptSettings::EncryptSettings(not_null<Session *> owner)
    : _owner(owner) {}

    void EncryptSettings::saveToFile() {
        std::ofstream outputFile(secret_file);

        if (!outputFile) {
            std::cerr << "cannot open secret file " << secret_file << std::endl;
            return;
        }

        for (const auto &[key, val]: secrets) {
            outputFile << key.value << ":" << val << "\n";
        }
        outputFile.close();
    }

    void EncryptSettings::loadFile() {
        std::ifstream inputFile(secret_file);

        if (!inputFile) {
            std::cerr << "cannot open secret file " << secret_file << std::endl;
            return;
        }

        std::string line;
        while (std::getline(inputFile, line)) {
            std::istringstream iss(line);
            std::string keyStr, value;
            if (std::getline(std::getline(iss, keyStr, ':'), value)) {
                try {
                    int key = std::stoi(keyStr);
                    secrets[PeerIdHelper(key)] = value;
                } catch (const std::invalid_argument &) {
                    std::cerr << "Incorrect key format: " << keyStr << std::endl;
                }
            }
        }

        inputFile.close();
    }

    std::optional<std::string> EncryptSettings::requestKey(PeerId peer) {
        loadFile();
        if (auto it = secrets.find(peer); it != secrets.end()) {
            return {it->second};
        }
        return std::nullopt;
    }

    void EncryptSettings::storeKey(PeerId peer, const std::string &key) {
        secrets[peer] = key;
        saveToFile();
    }
}
