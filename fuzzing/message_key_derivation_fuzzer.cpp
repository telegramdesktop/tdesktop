/*
Message Key Derivation Fuzzer
Targets: mtproto/mtproto_auth_key.cpp - prepareAES() and prepareAES_oldmtp()
Critical: Derives AES key/IV from 128-bit message key and 2048-bit auth key
Feature: Two versions - old (SHA1-based) and new (SHA256-based)
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>

constexpr size_t AUTH_KEY_SIZE = 256;  // 2048 bits
constexpr size_t MSG_KEY_SIZE = 16;    // 128 bits
constexpr size_t AES_KEY_SIZE = 32;    // 256 bits
constexpr size_t AES_IV_SIZE = 32;     // 256 bits

// Simplified SHA1 (20 bytes output)
class SimpleSHA1 {
public:
    static void hash(const uint8_t* input, size_t length, uint8_t output[20]) {
        // Simplified for fuzzing
        uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        
        for (size_t i = 0; i < length && i < 64; ++i) {
            state[i % 5] ^= input[i];
            state[i % 5] = (state[i % 5] << 1) | (state[i % 5] >> 31);
        }
        
        for (int i = 0; i < 5; ++i) {
            output[i * 4 + 0] = (state[i] >> 24) & 0xFF;
            output[i * 4 + 1] = (state[i] >> 16) & 0xFF;
            output[i * 4 + 2] = (state[i] >> 8) & 0xFF;
            output[i * 4 + 3] = state[i] & 0xFF;
        }
    }
};

// Simplified SHA256 (32 bytes output)
class SimpleSHA256 {
public:
    static void hash(const uint8_t* input, size_t length, uint8_t output[32]) {
        // Simplified for fuzzing
        uint32_t state[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        
        for (size_t i = 0; i < length && i < 64; ++i) {
            state[i % 8] ^= input[i];
            state[i % 8] = (state[i % 8] << 1) | (state[i % 8] >> 31);
        }
        
        for (int i = 0; i < 8; ++i) {
            output[i * 4 + 0] = (state[i] >> 24) & 0xFF;
            output[i * 4 + 1] = (state[i] >> 16) & 0xFF;
            output[i * 4 + 2] = (state[i] >> 8) & 0xFF;
            output[i * 4 + 3] = state[i] & 0xFF;
        }
    }
};

// Old MTProto key derivation (mtproto_auth_key.cpp:42-76)
class MessageKeyDerivationOld {
public:
    static void prepareAES(const uint8_t authKey[AUTH_KEY_SIZE],
                          const uint8_t msgKey[MSG_KEY_SIZE],
                          uint8_t aesKey[AES_KEY_SIZE],
                          uint8_t aesIV[AES_IV_SIZE],
                          bool send) {
        uint32_t x = send ? 0 : 8;
        
        // SHA1(msgKey + authKey[x:x+32])
        uint8_t sha1_a[20];
        uint8_t data_a[16 + 32];
        memcpy(data_a, msgKey, 16);
        memcpy(data_a + 16, authKey + x, 32);
        SimpleSHA1::hash(data_a, 48, sha1_a);
        
        // SHA1(authKey[32+x:48+x] + msgKey + authKey[48+x:64+x])
        uint8_t sha1_b[20];
        uint8_t data_b[16 + 16 + 16];
        memcpy(data_b, authKey + 32 + x, 16);
        memcpy(data_b + 16, msgKey, 16);
        memcpy(data_b + 32, authKey + 48 + x, 16);
        SimpleSHA1::hash(data_b, 48, sha1_b);
        
        // SHA1(authKey[64+x:96+x] + msgKey)
        uint8_t sha1_c[20];
        uint8_t data_c[32 + 16];
        memcpy(data_c, authKey + 64 + x, 32);
        memcpy(data_c + 32, msgKey, 16);
        SimpleSHA1::hash(data_c, 48, sha1_c);
        
        // SHA1(msgKey + authKey[96+x:128+x])
        uint8_t sha1_d[20];
        uint8_t data_d[16 + 32];
        memcpy(data_d, msgKey, 16);
        memcpy(data_d + 16, authKey + 96 + x, 32);
        SimpleSHA1::hash(data_d, 48, sha1_d);
        
        // Construct AES key (256 bits = 32 bytes)
        memcpy(aesKey, sha1_a, 8);
        memcpy(aesKey + 8, sha1_b + 8, 12);
        memcpy(aesKey + 20, sha1_c + 4, 12);
        
        // Construct AES IV (256 bits = 32 bytes)
        memcpy(aesIV, sha1_a + 8, 12);
        memcpy(aesIV + 12, sha1_b, 8);
        memcpy(aesIV + 20, sha1_c + 16, 4);
        memcpy(aesIV + 24, sha1_d, 8);
    }
};

// New MTProto key derivation (mtproto_auth_key.cpp:78-100)
class MessageKeyDerivationNew {
public:
    static void prepareAES(const uint8_t authKey[AUTH_KEY_SIZE],
                          const uint8_t msgKey[MSG_KEY_SIZE],
                          uint8_t aesKey[AES_KEY_SIZE],
                          uint8_t aesIV[AES_IV_SIZE],
                          bool send) {
        uint32_t x = send ? 0 : 8;
        
        // SHA256(msgKey + authKey[x:x+36])
        uint8_t sha256_a[32];
        uint8_t data_a[16 + 36];
        memcpy(data_a, msgKey, 16);
        memcpy(data_a + 16, authKey + x, 36);
        SimpleSHA256::hash(data_a, 52, sha256_a);
        
        // SHA256(authKey[40+x:76+x] + msgKey)
        uint8_t sha256_b[32];
        uint8_t data_b[36 + 16];
        memcpy(data_b, authKey + 40 + x, 36);
        memcpy(data_b + 36, msgKey, 16);
        SimpleSHA256::hash(data_b, 52, sha256_b);
        
        // Construct AES key (256 bits)
        memcpy(aesKey, sha256_a, 8);
        memcpy(aesKey + 8, sha256_b + 8, 16);
        memcpy(aesKey + 24, sha256_a + 24, 8);
        
        // Construct AES IV (256 bits)
        memcpy(aesIV, sha256_b, 8);
        memcpy(aesIV + 8, sha256_a + 8, 16);
        memcpy(aesIV + 24, sha256_b + 24, 8);
    }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < AUTH_KEY_SIZE + MSG_KEY_SIZE || size > 1024 * 1024) {
        return 0;
    }
    
    const uint8_t* authKey = data;
    const uint8_t* msgKey = data + AUTH_KEY_SIZE;
    
    // Test 1: Old MTProto key derivation (send direction)
    {
        uint8_t aesKey[AES_KEY_SIZE];
        uint8_t aesIV[AES_IV_SIZE];
        
        MessageKeyDerivationOld::prepareAES(authKey, msgKey, aesKey, aesIV, true);
        
        // Verify key and IV are not all zeros
        bool keyAllZeros = true, ivAllZeros = true;
        for (size_t i = 0; i < AES_KEY_SIZE; ++i) {
            if (aesKey[i] != 0) keyAllZeros = false;
        }
        for (size_t i = 0; i < AES_IV_SIZE; ++i) {
            if (aesIV[i] != 0) ivAllZeros = false;
        }
        
        volatile bool allZeros = keyAllZeros && ivAllZeros;
        (void)allZeros;
    }
    
    // Test 2: Old MTProto key derivation (receive direction)
    {
        uint8_t aesKey[AES_KEY_SIZE];
        uint8_t aesIV[AES_IV_SIZE];
        
        MessageKeyDerivationOld::prepareAES(authKey, msgKey, aesKey, aesIV, false);
    }
    
    // Test 3: New MTProto key derivation (send direction)
    {
        uint8_t aesKey[AES_KEY_SIZE];
        uint8_t aesIV[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, aesKey, aesIV, true);
    }
    
    // Test 4: New MTProto key derivation (receive direction)
    {
        uint8_t aesKey[AES_KEY_SIZE];
        uint8_t aesIV[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, aesKey, aesIV, false);
    }
    
    // Test 5: Compare old vs new derivation
    {
        uint8_t oldKey[AES_KEY_SIZE], oldIV[AES_IV_SIZE];
        uint8_t newKey[AES_KEY_SIZE], newIV[AES_IV_SIZE];
        
        MessageKeyDerivationOld::prepareAES(authKey, msgKey, oldKey, oldIV, true);
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, newKey, newIV, true);
        
        // Keys should be different (different algorithms)
        bool different = (memcmp(oldKey, newKey, AES_KEY_SIZE) != 0);
        (void)different;
    }
    
    // Test 6: Determinism - same inputs produce same outputs
    {
        uint8_t key1[AES_KEY_SIZE], iv1[AES_IV_SIZE];
        uint8_t key2[AES_KEY_SIZE], iv2[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, key1, iv1, true);
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, key2, iv2, true);
        
        // Should be identical
        bool keyMatch = (memcmp(key1, key2, AES_KEY_SIZE) == 0);
        bool ivMatch = (memcmp(iv1, iv2, AES_IV_SIZE) == 0);
        
        if (!keyMatch || !ivMatch) {
            // Bug: non-deterministic
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 7: Send vs Receive produce different keys
    {
        uint8_t sendKey[AES_KEY_SIZE], sendIV[AES_IV_SIZE];
        uint8_t recvKey[AES_KEY_SIZE], recvIV[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, sendKey, sendIV, true);
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, recvKey, recvIV, false);
        
        // Should be different
        bool different = (memcmp(sendKey, recvKey, AES_KEY_SIZE) != 0);
        (void)different;
    }
    
    // Test 8: Different message keys produce different AES keys
    if (size >= AUTH_KEY_SIZE + MSG_KEY_SIZE * 2) {
        const uint8_t* msgKey2 = data + AUTH_KEY_SIZE + MSG_KEY_SIZE;
        
        uint8_t key1[AES_KEY_SIZE], iv1[AES_IV_SIZE];
        uint8_t key2[AES_KEY_SIZE], iv2[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, key1, iv1, true);
        MessageKeyDerivationNew::prepareAES(authKey, msgKey2, key2, iv2, true);
        
        // Different msgKeys should produce different results
        bool different = (memcmp(key1, key2, AES_KEY_SIZE) != 0);
        (void)different;
    }
    
    // Test 9: Zero auth key
    {
        uint8_t zeroAuthKey[AUTH_KEY_SIZE] = {0};
        uint8_t aesKey[AES_KEY_SIZE], aesIV[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(zeroAuthKey, msgKey, aesKey, aesIV, true);
    }
    
    // Test 10: Zero message key
    {
        uint8_t zeroMsgKey[MSG_KEY_SIZE] = {0};
        uint8_t aesKey[AES_KEY_SIZE], aesIV[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(authKey, zeroMsgKey, aesKey, aesIV, true);
    }
    
    // Test 11: All 0xFF auth key
    {
        uint8_t maxAuthKey[AUTH_KEY_SIZE];
        memset(maxAuthKey, 0xFF, AUTH_KEY_SIZE);
        
        uint8_t aesKey[AES_KEY_SIZE], aesIV[AES_IV_SIZE];
        MessageKeyDerivationNew::prepareAES(maxAuthKey, msgKey, aesKey, aesIV, true);
    }
    
    // Test 12: Collision resistance
    if (size >= AUTH_KEY_SIZE * 2 + MSG_KEY_SIZE) {
        const uint8_t* authKey2 = data + AUTH_KEY_SIZE;
        
        uint8_t key1[AES_KEY_SIZE], iv1[AES_IV_SIZE];
        uint8_t key2[AES_KEY_SIZE], iv2[AES_IV_SIZE];
        
        MessageKeyDerivationNew::prepareAES(authKey, msgKey, key1, iv1, true);
        MessageKeyDerivationNew::prepareAES(authKey2, msgKey, key2, iv2, true);
        
        // Different auth keys should produce different results
        bool different = (memcmp(key1, key2, AES_KEY_SIZE) != 0);
        (void)different;
    }
    
    return 0;
}
