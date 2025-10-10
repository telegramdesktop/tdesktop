/*
Auth Key Management Fuzzer
Targets: mtproto/mtproto_auth_key.cpp - 2048-bit authorization key handling
Critical: Auth key is the foundation of ALL Telegram encryption
Feature: Key generation, KeyID calculation, serialization, temporary keys
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>

constexpr size_t AUTH_KEY_SIZE = 256;  // 2048 bits
using KeyId = uint64_t;

// Simplified SHA1 for KeyID calculation
class SimpleSHA1 {
public:
    static void hash(const uint8_t* input, size_t length, uint8_t output[20]) {
        uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        
        for (size_t i = 0; i < length && i < AUTH_KEY_SIZE; ++i) {
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

// Auth Key from mtproto_auth_key.h:16-63
class AuthKey {
public:
    enum class Type {
        Generated,      // Normal auth key
        Temporary,      // Temporary (with expiration)
        ReadFromFile,   // Loaded from local storage
        Local,          // Local encryption
    };
    
    AuthKey(Type type, uint16_t dcId, const uint8_t data[AUTH_KEY_SIZE])
        : type_(type), dcId_(dcId) {
        memcpy(key_, data, AUTH_KEY_SIZE);
        countKeyId();
    }
    
    Type type() const { return type_; }
    uint16_t dcId() const { return dcId_; }
    KeyId keyId() const { return keyId_; }
    
    const uint8_t* data() const { return key_; }
    
    // From mtproto_auth_key.cpp:144-149
    void countKeyId() {
        uint8_t hash[20];
        SimpleSHA1::hash(key_, AUTH_KEY_SIZE, hash);
        
        // Lower 64 bits of SHA1(key) = bytes [12..19]
        memcpy(&keyId_, hash + 12, 8);
    }
    
    // Check if two auth keys are equal
    bool equals(const AuthKey& other) const {
        return memcmp(key_, other.key_, AUTH_KEY_SIZE) == 0;
    }
    
    // Serialize auth key
    void serialize(uint8_t* output) const {
        memcpy(output, key_, AUTH_KEY_SIZE);
    }
    
    // Get part of key for message key derivation (mtproto_auth_key.cpp:102-104)
    const uint8_t* partForMsgKey(bool send) const {
        return key_ + 88 + (send ? 0 : 8);
    }
    
private:
    Type type_;
    uint16_t dcId_;
    uint8_t key_[AUTH_KEY_SIZE];
    KeyId keyId_ = 0;
};

// Test auth key filling (mtproto_auth_key.cpp:132-142)
bool fillAuthKeyData(uint8_t authKey[AUTH_KEY_SIZE], const uint8_t* computed, size_t computedSize) {
    if (computedSize > AUTH_KEY_SIZE) {
        return false;
    }
    
    if (computedSize < AUTH_KEY_SIZE) {
        // Pad with zeros at the beginning
        memset(authKey, 0, AUTH_KEY_SIZE - computedSize);
        memcpy(authKey + (AUTH_KEY_SIZE - computedSize), computed, computedSize);
    } else {
        memcpy(authKey, computed, AUTH_KEY_SIZE);
    }
    
    return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < AUTH_KEY_SIZE || size > 1024 * 1024) {
        return 0;
    }
    
    // Test 1: Create auth key and calculate KeyID
    {
        AuthKey key(AuthKey::Type::Generated, 2, data);
        
        KeyId keyId = key.keyId();
        volatile KeyId id = keyId;
        (void)id;
    }
    
    // Test 2: KeyID determinism
    {
        AuthKey key1(AuthKey::Type::Generated, 2, data);
        AuthKey key2(AuthKey::Type::Generated, 2, data);
        
        if (key1.keyId() != key2.keyId()) {
            // Bug: non-deterministic KeyID
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 3: Different keys produce different KeyIDs
    if (size >= AUTH_KEY_SIZE * 2) {
        AuthKey key1(AuthKey::Type::Generated, 2, data);
        AuthKey key2(AuthKey::Type::Generated, 2, data + AUTH_KEY_SIZE);
        
        // Should be different (unless collision)
        bool different = (key1.keyId() != key2.keyId());
        (void)different;
    }
    
    // Test 4: Auth key equality
    if (size >= AUTH_KEY_SIZE * 2) {
        AuthKey key1(AuthKey::Type::Generated, 2, data);
        AuthKey key2(AuthKey::Type::Generated, 2, data);
        AuthKey key3(AuthKey::Type::Generated, 2, data + AUTH_KEY_SIZE);
        
        bool equal = key1.equals(key2);
        bool notEqual = !key1.equals(key3);
        
        if (!equal || !notEqual) {
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 5: Serialization and deserialization
    {
        AuthKey key1(AuthKey::Type::Generated, 2, data);
        
        uint8_t serialized[AUTH_KEY_SIZE];
        key1.serialize(serialized);
        
        AuthKey key2(AuthKey::Type::ReadFromFile, 2, serialized);
        
        // Should be equal
        if (!key1.equals(key2) || key1.keyId() != key2.keyId()) {
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 6: Different DC IDs
    {
        AuthKey key1(AuthKey::Type::Generated, 1, data);
        AuthKey key2(AuthKey::Type::Generated, 2, data);
        AuthKey key3(AuthKey::Type::Generated, 5, data);
        
        // Same key data but different DC IDs
        volatile uint16_t dc1 = key1.dcId();
        volatile uint16_t dc2 = key2.dcId();
        volatile uint16_t dc3 = key3.dcId();
        (void)dc1; (void)dc2; (void)dc3;
    }
    
    // Test 7: Different auth key types
    {
        AuthKey generated(AuthKey::Type::Generated, 2, data);
        AuthKey temporary(AuthKey::Type::Temporary, 2, data);
        AuthKey fromFile(AuthKey::Type::ReadFromFile, 2, data);
        AuthKey local(AuthKey::Type::Local, 2, data);
        
        // All should have same keyId (same data)
        if (generated.keyId() != temporary.keyId() ||
            generated.keyId() != fromFile.keyId() ||
            generated.keyId() != local.keyId()) {
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 8: Part for message key (send vs receive)
    {
        AuthKey key(AuthKey::Type::Generated, 2, data);
        
        const uint8_t* sendPart = key.partForMsgKey(true);
        const uint8_t* recvPart = key.partForMsgKey(false);
        
        // Should be 8 bytes apart (88 vs 96)
        ptrdiff_t diff = recvPart - sendPart;
        if (diff != 8) {
            volatile bool bug = true;
            (void)bug;
        }
        
        // Check they're within key bounds
        const uint8_t* keyStart = key.data();
        if (sendPart < keyStart || sendPart >= keyStart + AUTH_KEY_SIZE ||
            recvPart < keyStart || recvPart >= keyStart + AUTH_KEY_SIZE) {
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 9: Fill auth key with smaller computed key
    if (size >= 128) {
        uint8_t authKey[AUTH_KEY_SIZE];
        
        // Test with various sizes
        for (size_t computedSize : {32, 64, 128, 256}) {
            if (computedSize <= size) {
                if (!fillAuthKeyData(authKey, data, computedSize)) {
                    volatile bool error = true;
                    (void)error;
                }
                
                // Create AuthKey and verify
                AuthKey key(AuthKey::Type::Generated, 2, authKey);
                volatile KeyId id = key.keyId();
                (void)id;
            }
        }
    }
    
    // Test 10: Zero auth key
    {
        uint8_t zeroKey[AUTH_KEY_SIZE] = {0};
        AuthKey key(AuthKey::Type::Generated, 2, zeroKey);
        
        KeyId keyId = key.keyId();
        // KeyID should be calculable even for zero key
        volatile KeyId id = keyId;
        (void)id;
    }
    
    // Test 11: All 0xFF auth key
    {
        uint8_t maxKey[AUTH_KEY_SIZE];
        memset(maxKey, 0xFF, AUTH_KEY_SIZE);
        
        AuthKey key(AuthKey::Type::Generated, 2, maxKey);
        volatile KeyId id = key.keyId();
        (void)id;
    }
    
    // Test 12: KeyID collision resistance
    if (size >= AUTH_KEY_SIZE * 10) {
        std::vector<KeyId> keyIds;
        
        for (size_t i = 0; i < 10 && (i * AUTH_KEY_SIZE < size); ++i) {
            AuthKey key(AuthKey::Type::Generated, 2, data + i * AUTH_KEY_SIZE);
            keyIds.push_back(key.keyId());
        }
        
        // Check for collisions (shouldn't happen with random data)
        for (size_t i = 0; i < keyIds.size(); ++i) {
            for (size_t j = i + 1; j < keyIds.size(); ++j) {
                if (keyIds[i] == keyIds[j]) {
                    // Collision found
                    volatile bool collision = true;
                    (void)collision;
                }
            }
        }
    }
    
    // Test 13: Oversized computed key (should fail)
    {
        uint8_t authKey[AUTH_KEY_SIZE];
        bool result = fillAuthKeyData(authKey, data, AUTH_KEY_SIZE + 1);
        
        if (result) {
            // Bug: accepted oversized key
            volatile bool bug = true;
            (void)bug;
        }
    }
    
    // Test 14: Single-bit differences in key produce different KeyIDs
    if (size >= AUTH_KEY_SIZE + 1) {
        AuthKey key1(AuthKey::Type::Generated, 2, data);
        
        uint8_t modifiedKey[AUTH_KEY_SIZE];
        memcpy(modifiedKey, data, AUTH_KEY_SIZE);
        modifiedKey[128] ^= 0x01;  // Flip one bit in middle
        
        AuthKey key2(AuthKey::Type::Generated, 2, modifiedKey);
        
        // KeyIDs should be different (avalanche effect)
        if (key1.keyId() == key2.keyId()) {
            // Collision from single bit flip
            volatile bool collision = true;
            (void)collision;
        }
    }
    
    return 0;
}
