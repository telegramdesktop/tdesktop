#include "coder/coder.h"
#include <iostream>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/err.h>

namespace local {

bool rsa_2048::genKeys(QByteArray& public_key, QByteArray& private_key) {
    RSA* rsa = RSA_new();
    BIGNUM* bne = BN_new();
    if (BN_set_word(bne, RSA_F4) != 1) {
        BN_free(bne);
        RSA_free(rsa);
        return false;
    }
    if (RSA_generate_key_ex(rsa, 2048, bne, nullptr) != 1) {
        BN_free(bne);
        RSA_free(rsa);
        return false;
    }
    // public key
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_RSAPublicKey(bio, rsa);
    char* key;
    size_t key_size = BIO_get_mem_data(bio, &key);
    public_key.resize(key_size);
    memcpy(public_key.data(), key, key_size);
    BIO_free(bio);
    // private key
    bio = BIO_new(BIO_s_mem());
    PEM_write_bio_RSAPrivateKey(bio, rsa, nullptr, nullptr, 0, nullptr, nullptr);
    key_size = BIO_get_mem_data(bio, &key);
    private_key.resize(key_size);
    memcpy(private_key.data(), key, key_size);
    return true;
}

bool rsa_2048::encryptPublic(const QByteArray& data, const QByteArray& key, QByteArray& encrypted) {
    RSA* rsa = RSA_new();
    BIO* bio = BIO_new_mem_buf(key.data(), key.size());
    PEM_read_bio_RSAPublicKey(bio, &rsa, nullptr, nullptr);
    BIO_free(bio);
    encrypted.resize(RSA_size(rsa));
    int rsa_size = RSA_public_encrypt(
        data.size(),
        reinterpret_cast<const uint8_t*>(data.data()),
        reinterpret_cast<uint8_t*>(encrypted.data()),
        rsa,
        RSA_PKCS1_PADDING);
    RSA_free(rsa);
    encrypted.resize(rsa_size);
    return rsa_size != -1;
}

bool rsa_2048::decryptPrivate(
    const QByteArray& data, const QByteArray& key, QByteArray& decrypted) {
    RSA* rsa = RSA_new();
    BIO* bio = BIO_new_mem_buf(key.data(), key.size());
    PEM_read_bio_RSAPrivateKey(bio, &rsa, nullptr, nullptr);
    BIO_free(bio);
    decrypted.resize(RSA_size(rsa));
    int rsa_size = RSA_private_decrypt(
        data.size(),
        reinterpret_cast<const uint8_t*>(data.data()),
        reinterpret_cast<uint8_t*>(decrypted.data()),
        rsa,
        RSA_PKCS1_PADDING);
    RSA_free(rsa);
    decrypted.resize(rsa_size);
    return rsa_size != -1;
}

bool aes_128::genKey(QByteArray& key) {
    key.resize(16);
    return RAND_bytes(reinterpret_cast<uint8_t*>(key.data()), 16);
}

bool aes_128::encrypt(const QByteArray& data, const QByteArray& key, QByteArray& encrypted) {
    if (data.size() == 0) {
        encrypted.clear();
        return true;
    }

    AES_KEY aes_key;
    if (AES_set_encrypt_key(reinterpret_cast<const uint8_t*>(key.data()), 128, &aes_key) == -1) {
        return false;
    }

    size_t data_to_encode_size = data.size() + 1;  // add one byte for padding size
    size_t padding_size = 16 - data_to_encode_size % 16 + 1;
    size_t data_size = data_to_encode_size + padding_size - 1;

    encrypted.resize(data_size);

    // encrypt blocks except the last one
    size_t num_of_blocks = data_size / 16;
    for (size_t i = 0; i < num_of_blocks - 1; ++i) {
        AES_encrypt(
            reinterpret_cast<const uint8_t*>(data.data()) + i * 16,
            reinterpret_cast<uint8_t*>(encrypted.data()) + i * 16,
            &aes_key);
    }

    // encrypt the last block
    std::vector<uint8_t> last_block(16);
    last_block[15] = static_cast<uint8_t>(padding_size);
    size_t current_index = (num_of_blocks - 1) * 16;
    memcpy(last_block.data(), data.data() + current_index, data_to_encode_size % 16);
    AES_encrypt(
        last_block.data(), reinterpret_cast<uint8_t*>(encrypted.data()) + current_index, &aes_key);
    return true;
}

bool aes_128::decrypt(const QByteArray& data, const QByteArray& key, QByteArray& decrypted) {
    if (data.size() == 0) {
        decrypted.clear();
        return true;
    }
    if (data.size() % 16 != 0) {
        return false;
    }

    AES_KEY aes_key;
    if (AES_set_decrypt_key(reinterpret_cast<const uint8_t*>(key.data()), 128, &aes_key) == -1) {
        return false;
    }

    decrypted.resize(data.size());

    // decrypt all blocks
    size_t num_of_full_blocks = data.size() / 16;
    for (size_t i = 0; i < num_of_full_blocks; ++i) {
        AES_decrypt(
            reinterpret_cast<const uint8_t*>(data.data()) + i * 16,
            reinterpret_cast<uint8_t*>(decrypted.data()) + i * 16,
            &aes_key);
    }

    // remove padding
    size_t padding_size = static_cast<size_t>(decrypted.back());
    decrypted.resize(decrypted.size() - padding_size);
    return true;
}

}  // namespace local
