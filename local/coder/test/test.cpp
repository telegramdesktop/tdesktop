#include "gtest/gtest.h"
#include "local/coder/coder.h"
#include <openssl/rand.h>

using namespace local;

QByteArray generateRandomData(size_t size) {
    QByteArray data(size, 0);
    RAND_bytes(reinterpret_cast<uint8_t*>(data.data()), size);
    return data;
}

TEST(RSA2048Test, KeyGeneration) {
    QByteArray public_key, private_key;
    ASSERT_TRUE(rsa_2048::genKeys(public_key, private_key));
}

TEST(RSA2048Test, PublicEncryption) {
    QByteArray public_key, private_key;
    QByteArray data, encrypted, decrypted;
    for (size_t i = 0; i < 100; i++) {
        data = generateRandomData(i);
        rsa_2048::genKeys(public_key, private_key);
        ASSERT_TRUE(rsa_2048::encryptPublic(data, public_key, encrypted));
    }
}

TEST(RSA2048Test, PrivateDecryption) {
    QByteArray public_key, private_key;
    QByteArray data, encrypted, decrypted;
    for (size_t i = 0; i < 100; i++) {
        data = generateRandomData(i);
        rsa_2048::genKeys(public_key, private_key);
        ASSERT_TRUE(rsa_2048::encryptPublic(data, public_key, encrypted));
        ASSERT_TRUE(rsa_2048::decryptPrivate(encrypted, private_key, decrypted));
    }
}

TEST(RSA2048Test, EncryptionDecryption) {
    QByteArray public_key, private_key;
    QByteArray data, encrypted, decrypted;
    for (size_t i = 0; i < 100; i++) {
        data = generateRandomData(i);
        rsa_2048::genKeys(public_key, private_key);
        ASSERT_TRUE(rsa_2048::encryptPublic(data, public_key, encrypted));
        ASSERT_TRUE(rsa_2048::decryptPrivate(encrypted, private_key, decrypted));
        ASSERT_EQ(data, decrypted);
    }
}

TEST(AES128Test, EncryptionDecryption) {
    QByteArray key;
    QByteArray data, encrypted, decrypted;
    for (size_t i = 1000000; i < 1000010; i++) {
        data = generateRandomData(i);
        ASSERT_TRUE(aes_128::genKey(key));
        ASSERT_TRUE(aes_128::encrypt(data, key, encrypted));
        ASSERT_TRUE(aes_128::decrypt(encrypted, key, decrypted));
        ASSERT_EQ(data, decrypted);
    }
}