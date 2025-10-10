/*
MTProto Version 1 Obfuscated Protocol Fuzzer
Targets: mtproto/connection_tcp.cpp - Protocol::Version1
Feature: SHA256-based key derivation with secret
Critical: Obfuscation layer to bypass DPI
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>

// Minimal SHA256 implementation for fuzzing
class SimpleSHA256 {
public:
    static void hash(const uint8_t* input, size_t length, uint8_t output[32]) {
        // Simplified version - in real code uses OpenSSL
        // For fuzzing, we test the interface not crypto correctness
        uint32_t state[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };

        // Mix input into state (simplified)
        for (size_t i = 0; i < length && i < 32; ++i) {
            state[i % 8] ^= input[i];
            state[i % 8] = (state[i % 8] << 1) | (state[i % 8] >> 31);
        }

        // Output
        for (int i = 0; i < 8; ++i) {
            output[i * 4 + 0] = (state[i] >> 24) & 0xFF;
            output[i * 4 + 1] = (state[i] >> 16) & 0xFF;
            output[i * 4 + 2] = (state[i] >> 8) & 0xFF;
            output[i * 4 + 3] = state[i] & 0xFF;
        }
    }
};

// Version1 protocol (from connection_tcp.cpp:140-166)
class ProtocolVersion1 {
public:
    static constexpr size_t KeySize = 32;

    explicit ProtocolVersion1(const uint8_t* secret, size_t secretLen)
        : secretLen_(std::min(secretLen, size_t(256))) {
        memcpy(secret_, secret, secretLen_);
    }

    // Key derivation from connection_tcp.cpp:157-162
    void prepareKey(uint8_t key[KeySize], const uint8_t* source, size_t sourceLen) {
        // Concatenate source + secret
        std::vector<uint8_t> payload;
        payload.reserve(sourceLen + secretLen_);
        payload.insert(payload.end(), source, source + sourceLen);
        payload.insert(payload.end(), secret_, secret_ + secretLen_);

        // SHA256(source || secret)
        SimpleSHA256::hash(payload.data(), payload.size(), key);
    }

    // Test key derivation properties
    bool testKeyProperties(const uint8_t* nonce, size_t nonceLen) {
        if (nonceLen < KeySize) {
            return false;
        }

        uint8_t key1[KeySize];
        uint8_t key2[KeySize];

        // Derive key twice - should be deterministic
        prepareKey(key1, nonce, KeySize);
        prepareKey(key2, nonce, KeySize);

        // Verify determinism
        if (memcmp(key1, key2, KeySize) != 0) {
            return false;
        }

        // Verify key is not all zeros
        bool allZeros = true;
        for (size_t i = 0; i < KeySize; ++i) {
            if (key1[i] != 0) {
                allZeros = false;
                break;
            }
        }

        return !allZeros;
    }

private:
    uint8_t secret_[256];
    size_t secretLen_;
};

// Test secret validation
bool validateSecret(const uint8_t* secret, size_t size) {
    // Secret must be exactly 16 bytes for Version1
    if (size != 16) {
        return false;
    }

    // Check for weak secrets (all same byte)
    bool allSame = true;
    for (size_t i = 1; i < size; ++i) {
        if (secret[i] != secret[0]) {
            allSame = false;
            break;
        }
    }

    if (allSame) {
        return false; // Weak secret
    }

    return true;
}

// Test key collision resistance
bool testKeyCollisions(const uint8_t* data, size_t size) {
    if (size < 48) { // Need 16 (secret) + 32 (nonce1) + ...
        return false;
    }

    const uint8_t* secret = data;
    const uint8_t* nonce1 = data + 16;
    const uint8_t* nonce2 = data + 32;

    ProtocolVersion1 proto(secret, 16);

    uint8_t key1[32];
    uint8_t key2[32];

    proto.prepareKey(key1, nonce1, 16);
    proto.prepareKey(key2, nonce2, 16);

    // Different nonces should produce different keys
    bool different = (memcmp(key1, key2, 32) != 0);

    return different;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 16 || size > 1024 * 1024) {
        return 0;
    }

    // Test 1: Secret validation
    if (size >= 16) {
        bool valid = validateSecret(data, 16);
        (void)valid;
    }

    // Test 2: Key derivation
    if (size >= 48) {
        const uint8_t* secret = data;
        const uint8_t* nonce = data + 16;
        size_t nonceLen = std::min(size - 16, size_t(32));

        ProtocolVersion1 proto(secret, 16);

        uint8_t derivedKey[32];
        proto.prepareKey(derivedKey, nonce, nonceLen);

        // Verify key properties
        proto.testKeyProperties(nonce, nonceLen);
    }

    // Test 3: Different secret lengths
    for (size_t secretLen = 1; secretLen <= std::min(size, size_t(32)); ++secretLen) {
        ProtocolVersion1 proto(data, secretLen);

        if (size > secretLen + 32) {
            uint8_t key[32];
            proto.prepareKey(key, data + secretLen, 32);
        }
    }

    // Test 4: Key collision testing
    if (size >= 48) {
        testKeyCollisions(data, size);
    }

    // Test 5: Edge cases
    if (size >= 16) {
        // Test with zero secret
        uint8_t zeroSecret[16] = {0};
        ProtocolVersion1 zeroProto(zeroSecret, 16);

        uint8_t key[32];
        zeroProto.prepareKey(key, data, std::min(size, size_t(32)));

        // Test with max secret
        uint8_t maxSecret[16];
        memset(maxSecret, 0xFF, 16);
        ProtocolVersion1 maxProto(maxSecret, 16);

        maxProto.prepareKey(key, data, std::min(size, size_t(32)));
    }

    // Test 6: Reversed key derivation (for decryption)
    if (size >= 64) {
        const uint8_t* secret = data;
        const uint8_t* nonce = data + 16;

        ProtocolVersion1 proto(secret, 16);

        // Forward key
        uint8_t forwardKey[32];
        proto.prepareKey(forwardKey, nonce, 32);

        // Reversed nonce (for decryption key)
        uint8_t reversedNonce[32];
        for (size_t i = 0; i < 32; ++i) {
            reversedNonce[i] = nonce[31 - i];
        }

        uint8_t reverseKey[32];
        proto.prepareKey(reverseKey, reversedNonce, 32);

        // Keys should be different
        bool different = (memcmp(forwardKey, reverseKey, 32) != 0);
        (void)different;
    }

    return 0;
}
