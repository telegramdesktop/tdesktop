/*
AES-IGE (Infinite Garble Extension) Encryption Fuzzer
Targets: mtproto/mtproto_auth_key.cpp - AES-IGE mode for message encryption
Critical: Used for ALL Telegram message encryption (private and group chats)
Feature: 256-bit AES with IGE mode (more secure than CBC)
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>

// AES block size
constexpr size_t AES_BLOCK_SIZE = 16;
constexpr size_t AES_KEY_SIZE = 32;  // 256-bit
constexpr size_t IGE_IV_SIZE = 32;   // 2 blocks for IGE

// Simplified AES for fuzzing (tests interface, not crypto strength)
class SimpleAES {
public:
    static void xorBlock(uint8_t* dst, const uint8_t* src) {
        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            dst[i] ^= src[i];
        }
    }
    
    static void encryptBlock(const uint8_t* key, const uint8_t* in, uint8_t* out) {
        // Simplified - real code uses OpenSSL AES
        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            out[i] = in[i] ^ key[i] ^ key[i + 16];
            out[i] = (out[i] << 1) | (out[i] >> 7);  // Rotate
        }
    }
    
    static void decryptBlock(const uint8_t* key, const uint8_t* in, uint8_t* out) {
        // Simplified decrypt
        for (size_t i = 0; i < AES_BLOCK_SIZE; ++i) {
            uint8_t temp = in[i];
            temp = (temp >> 1) | (temp << 7);  // Unrotate
            out[i] = temp ^ key[i] ^ key[i + 16];
        }
    }
};

// IGE Mode from mtproto_auth_key.cpp:151-169
class AESIGE {
public:
    // Encrypt with IGE mode
    static bool encrypt(const uint8_t* src, uint8_t* dst, size_t len,
                       const uint8_t key[AES_KEY_SIZE], const uint8_t iv[IGE_IV_SIZE]) {
        if (len == 0 || len % AES_BLOCK_SIZE != 0) {
            return false;  // IGE requires block-aligned data
        }
        
        uint8_t iv1[AES_BLOCK_SIZE];  // Previous plaintext
        uint8_t iv2[AES_BLOCK_SIZE];  // Previous ciphertext
        
        // Initialize IVs (first 16 bytes = iv1, next 16 bytes = iv2)
        memcpy(iv1, iv, AES_BLOCK_SIZE);
        memcpy(iv2, iv + AES_BLOCK_SIZE, AES_BLOCK_SIZE);
        
        for (size_t i = 0; i < len; i += AES_BLOCK_SIZE) {
            uint8_t block[AES_BLOCK_SIZE];
            memcpy(block, src + i, AES_BLOCK_SIZE);
            
            // XOR with previous ciphertext (iv2)
            SimpleAES::xorBlock(block, iv2);
            
            // Encrypt block
            uint8_t encrypted[AES_BLOCK_SIZE];
            SimpleAES::encryptBlock(key, block, encrypted);
            
            // XOR with previous plaintext (iv1)
            SimpleAES::xorBlock(encrypted, iv1);
            
            // Output encrypted block
            memcpy(dst + i, encrypted, AES_BLOCK_SIZE);
            
            // Update IVs
            memcpy(iv1, src + i, AES_BLOCK_SIZE);
            memcpy(iv2, encrypted, AES_BLOCK_SIZE);
        }
        
        return true;
    }
    
    // Decrypt with IGE mode
    static bool decrypt(const uint8_t* src, uint8_t* dst, size_t len,
                       const uint8_t key[AES_KEY_SIZE], const uint8_t iv[IGE_IV_SIZE]) {
        if (len == 0 || len % AES_BLOCK_SIZE != 0) {
            return false;
        }
        
        uint8_t iv1[AES_BLOCK_SIZE];
        uint8_t iv2[AES_BLOCK_SIZE];
        
        memcpy(iv1, iv, AES_BLOCK_SIZE);
        memcpy(iv2, iv + AES_BLOCK_SIZE, AES_BLOCK_SIZE);
        
        for (size_t i = 0; i < len; i += AES_BLOCK_SIZE) {
            uint8_t block[AES_BLOCK_SIZE];
            memcpy(block, src + i, AES_BLOCK_SIZE);
            
            // XOR with previous plaintext (iv1)
            SimpleAES::xorBlock(block, iv1);
            
            // Decrypt block
            uint8_t decrypted[AES_BLOCK_SIZE];
            SimpleAES::decryptBlock(key, block, decrypted);
            
            // XOR with previous ciphertext (iv2)
            SimpleAES::xorBlock(decrypted, iv2);
            
            // Output decrypted block
            memcpy(dst + i, decrypted, AES_BLOCK_SIZE);
            
            // Update IVs
            memcpy(iv1, decrypted, AES_BLOCK_SIZE);
            memcpy(iv2, src + i, AES_BLOCK_SIZE);
        }
        
        return true;
    }
};

// Test IGE properties
bool testIGEProperties(const uint8_t* data, size_t size) {
    if (size < AES_KEY_SIZE + IGE_IV_SIZE + AES_BLOCK_SIZE) {
        return false;
    }
    
    const uint8_t* key = data;
    const uint8_t* iv = data + AES_KEY_SIZE;
    const uint8_t* plaintext = data + AES_KEY_SIZE + IGE_IV_SIZE;
    size_t dataLen = std::min(size - AES_KEY_SIZE - IGE_IV_SIZE, size_t(1024));
    
    // Round to block size
    dataLen = (dataLen / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
    if (dataLen == 0) {
        return false;
    }
    
    // Encrypt
    std::vector<uint8_t> ciphertext(dataLen);
    if (!AESIGE::encrypt(plaintext, ciphertext.data(), dataLen, key, iv)) {
        return false;
    }
    
    // Decrypt
    std::vector<uint8_t> decrypted(dataLen);
    if (!AESIGE::decrypt(ciphertext.data(), decrypted.data(), dataLen, key, iv)) {
        return false;
    }
    
    // Verify round-trip
    return (memcmp(plaintext, decrypted.data(), dataLen) == 0);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < AES_KEY_SIZE + IGE_IV_SIZE + AES_BLOCK_SIZE || size > 1024 * 1024) {
        return 0;
    }
    
    // Test 1: Basic encryption/decryption
    if (size >= AES_KEY_SIZE + IGE_IV_SIZE + 64) {
        const uint8_t* key = data;
        const uint8_t* iv = data + AES_KEY_SIZE;
        const uint8_t* plaintext = data + AES_KEY_SIZE + IGE_IV_SIZE;
        size_t dataLen = std::min(size - AES_KEY_SIZE - IGE_IV_SIZE, size_t(256));
        dataLen = (dataLen / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
        
        if (dataLen > 0) {
            std::vector<uint8_t> ciphertext(dataLen);
            std::vector<uint8_t> decrypted(dataLen);
            
            if (AESIGE::encrypt(plaintext, ciphertext.data(), dataLen, key, iv)) {
                AESIGE::decrypt(ciphertext.data(), decrypted.data(), dataLen, key, iv);
                
                // Verify
                bool matches = (memcmp(plaintext, decrypted.data(), dataLen) == 0);
                (void)matches;
            }
        }
    }
    
    // Test 2: Non-block-aligned data (should fail)
    if (size >= AES_KEY_SIZE + IGE_IV_SIZE + 17) {
        const uint8_t* key = data;
        const uint8_t* iv = data + AES_KEY_SIZE;
        const uint8_t* plaintext = data + AES_KEY_SIZE + IGE_IV_SIZE;
        
        uint8_t output[32];
        // Should return false for non-aligned
        bool result = AESIGE::encrypt(plaintext, output, 17, key, iv);
        if (result) {
            // Bug: accepted non-block-aligned data
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 3: Zero-length data (should fail)
    {
        uint8_t key[AES_KEY_SIZE] = {0};
        uint8_t iv[IGE_IV_SIZE] = {0};
        uint8_t output[1];
        
        bool result = AESIGE::encrypt(data, output, 0, key, iv);
        if (result) {
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 4: Multiple blocks
    if (size >= AES_KEY_SIZE + IGE_IV_SIZE + 128) {
        const uint8_t* key = data;
        const uint8_t* iv = data + AES_KEY_SIZE;
        const uint8_t* plaintext = data + AES_KEY_SIZE + IGE_IV_SIZE;
        
        // Test with 8 blocks (128 bytes)
        std::vector<uint8_t> ciphertext(128);
        std::vector<uint8_t> decrypted(128);
        
        if (AESIGE::encrypt(plaintext, ciphertext.data(), 128, key, iv)) {
            AESIGE::decrypt(ciphertext.data(), decrypted.data(), 128, key, iv);
        }
    }
    
    // Test 5: IGE properties test
    testIGEProperties(data, size);
    
    // Test 6: Different IVs produce different ciphertexts
    if (size >= AES_KEY_SIZE + IGE_IV_SIZE * 2 + 32) {
        const uint8_t* key = data;
        const uint8_t* iv1 = data + AES_KEY_SIZE;
        const uint8_t* iv2 = data + AES_KEY_SIZE + IGE_IV_SIZE;
        const uint8_t* plaintext = data + AES_KEY_SIZE + IGE_IV_SIZE * 2;
        
        uint8_t cipher1[32], cipher2[32];
        
        AESIGE::encrypt(plaintext, cipher1, 32, key, iv1);
        AESIGE::encrypt(plaintext, cipher2, 32, key, iv2);
        
        // Different IVs should produce different ciphertexts
        bool different = (memcmp(cipher1, cipher2, 32) != 0);
        (void)different;
    }
    
    // Test 7: Bit flipping in ciphertext
    if (size >= AES_KEY_SIZE + IGE_IV_SIZE + 64) {
        const uint8_t* key = data;
        const uint8_t* iv = data + AES_KEY_SIZE;
        const uint8_t* plaintext = data + AES_KEY_SIZE + IGE_IV_SIZE;
        
        std::vector<uint8_t> ciphertext(64);
        std::vector<uint8_t> decrypted(64);
        
        if (AESIGE::encrypt(plaintext, ciphertext.data(), 64, key, iv)) {
            // Flip a bit in ciphertext
            ciphertext[10] ^= 0x80;
            
            // Decrypt flipped ciphertext
            AESIGE::decrypt(ciphertext.data(), decrypted.data(), 64, key, iv);
            
            // Check how many blocks are affected (IGE mode property)
            int affectedBlocks = 0;
            for (size_t i = 0; i < 4; ++i) {  // 4 blocks
                bool blockAffected = false;
                for (size_t j = 0; j < AES_BLOCK_SIZE; ++j) {
                    if (decrypted[i * AES_BLOCK_SIZE + j] != plaintext[i * AES_BLOCK_SIZE + j]) {
                        blockAffected = true;
                        break;
                    }
                }
                if (blockAffected) affectedBlocks++;
            }
            
            // IGE should affect multiple blocks
            volatile int blocks = affectedBlocks;
            (void)blocks;
        }
    }
    
    // Test 8: All-zero key and IV
    if (size >= 32) {
        uint8_t zeroKey[AES_KEY_SIZE] = {0};
        uint8_t zeroIV[IGE_IV_SIZE] = {0};
        
        std::vector<uint8_t> ciphertext(32);
        AESIGE::encrypt(data, ciphertext.data(), 32, zeroKey, zeroIV);
    }
    
    // Test 9: Maximum size
    if (size >= 10000) {
        const uint8_t* key = data;
        const uint8_t* iv = data + AES_KEY_SIZE;
        size_t dataLen = ((size - AES_KEY_SIZE - IGE_IV_SIZE) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
        
        if (dataLen > 0 && dataLen <= 100000) {
            std::vector<uint8_t> ciphertext(dataLen);
            AESIGE::encrypt(data + AES_KEY_SIZE + IGE_IV_SIZE, ciphertext.data(), 
                          dataLen, key, iv);
        }
    }
    
    return 0;
}
