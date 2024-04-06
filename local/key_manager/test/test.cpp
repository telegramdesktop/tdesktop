#include "gtest/gtest.h"
#include "local/coder/coder.h"
#include "local/key_manager/key_manager.h"
#include <openssl/rand.h>

using namespace local;

QByteArray generateRandomData(size_t size) {
    QByteArray data(size, 0);
    RAND_bytes(reinterpret_cast<uint8_t*>(data.data()), size);
    return data;
}

TEST(KeyManagerTest, KeySetGet) {
    // KeyManager::setPath("../keys.sqlite3");
    KeyManager& km = KeyManager::getInstance();
    QByteArray key;
    for (size_t i = 0; i < 100; i++) {
        aes_128::genKey(key);
        km.setKey(i, key);
        ASSERT_EQ(key, km.getKey(i));
    }
}

TEST(KeyManagerTest, KeyClear) {
    KeyManager& km = KeyManager::getInstance();
    km.clear();
    for (size_t i = 0; i < 100; i++) {
        ASSERT_FALSE(km.hasKey(i));
    }
}

TEST(KeyManagerTest, KeySetGet2) {
    KeyManager& km = KeyManager::getInstance();
    QByteArray key;
    for (size_t i = 0; i < 1000; i++) {
        aes_128::genKey(key);
        km.setKey(i, key);
        ASSERT_EQ(key, km.getKey(i));
    }
    km.clear();
}
