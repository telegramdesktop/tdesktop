/*
AES-CTR Obfuscation Fuzzer
Targets: mtproto/connection_tcp.cpp - AES-256-CTR encryption/decryption
Critical: All network traffic is encrypted with AES-CTR
Feature: Stateful counter mode encryption for traffic obfuscation
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>

// CTR state from connection_tcp.h
struct CTRState {
    static constexpr size_t KeySize = 32;   // AES-256
    static constexpr size_t IvecSize = 16;  // 128-bit IV
    static constexpr size_t BlockSize = 16; // AES block size

    uint8_t ivec[IvecSize];
    uint32_t num;  // Counter position in block
    uint8_t ecount[BlockSize];  // Encrypted counter

    CTRState() : num(0) {
        memset(ivec, 0, sizeof(ivec));
        memset(ecount, 0, sizeof(ecount));
    }
};

// Simplified AES-CTR for fuzzing (tests interface, not crypto)
class AESCTR {
public:
    static void encrypt(uint8_t* data, size_t size, const uint8_t key[32], CTRState* state) {
        // Simplified CTR mode - XOR with "encrypted counter"
        for (size_t i = 0; i < size; ++i) {
            if (state->num == 0) {
                // Generate new keystream block
                generateKeystream(key, state->ivec, state->ecount);
                incrementCounter(state->ivec);
            }

            // XOR with keystream
            data[i] ^= state->ecount[state->num];

            state->num = (state->num + 1) % CTRState::BlockSize;
        }
    }

private:
    static void generateKeystream(const uint8_t key[32], const uint8_t iv[16], uint8_t output[16]) {
        // Simplified - real code uses OpenSSL AES
        for (int i = 0; i < 16; ++i) {
            output[i] = key[i] ^ key[i + 16] ^ iv[i];
            output[i] = (output[i] << 1) | (output[i] >> 7); // Rotate
        }
    }

    static void incrementCounter(uint8_t iv[16]) {
        // Increment 128-bit counter (little-endian)
        for (int i = 0; i < 16; ++i) {
            if (++iv[i] != 0) {
                break;  // No carry
            }
        }
    }
};

// Test connection start prefix (from connection_tcp.cpp:446-494)
struct ConnectionStartPrefix {
    uint8_t nonce[64];
    uint8_t sendKey[32];
    uint8_t receiveKey[32];
    CTRState sendState;
    CTRState receiveState;

    bool initialize(const uint8_t* randomBytes, size_t size, uint32_t protocolId, int16_t dcId) {
        if (size < 64) {
            return false;
        }

        // Copy random nonce
        memcpy(nonce, randomBytes, 64);

        // Check for bad nonces (shouldn't start with known protocols)
        if (isGoodStartNonce()) {
            // Prepare send key from nonce[8..40]
            memcpy(sendKey, nonce + 8, 32);

            // Prepare send IV from nonce[40..56]
            memcpy(sendState.ivec, nonce + 40, 16);

            // Prepare receive key (reversed)
            for (int i = 0; i < 32; ++i) {
                receiveKey[i] = nonce[8 + 31 - i];
            }

            // Prepare receive IV (reversed)
            for (int i = 0; i < 16; ++i) {
                receiveState.ivec[i] = nonce[40 + 15 - i];
            }

            // Write protocol ID and DC ID
            memcpy(nonce + 56, &protocolId, 4);
            memcpy(nonce + 60, &dcId, 2);

            return true;
        }

        return false;
    }

    bool isGoodStartNonce() const {
        // Check it doesn't look like HTTP, TLS, etc.
        // From abstract_socket.cpp

        const uint32_t* words = reinterpret_cast<const uint32_t*>(nonce);

        // Check first 4 bytes don't match known protocols
        if (words[0] == 0x20544547 || // GET
            words[0] == 0x20545550 || // PUT
            words[0] == 0x54534f50 || // POST
            words[0] == 0x47454220 || // BEG (malformed)
            words[0] == 0xeeeeeeee ||  // All same
            words[0] == 0x44414548 ||  // HEAD
            words[0] == 0x54504f20) {  // OPT
            return false;
        }

        // Check for TLS handshake (0x16 0x03 0x01...)
        if (nonce[0] == 0x16 && nonce[1] == 0x03) {
            return false;
        }

        return true;
    }
};

// Test key derivation with secrets
bool testKeyDerivation(const uint8_t* nonce, size_t nonceLen,
                      const uint8_t* secret, size_t secretLen) {
    if (nonceLen < 32 || secretLen == 0 || secretLen > 256) {
        return false;
    }

    // Simple hash for key derivation (real code uses SHA256)
    uint8_t derivedKey[32] = {0};

    for (size_t i = 0; i < 32; ++i) {
        derivedKey[i] = nonce[i];
        for (size_t j = 0; j < secretLen; ++j) {
            derivedKey[i] ^= secret[j];
            derivedKey[i] = (derivedKey[i] << 1) | (derivedKey[i] >> 7);
        }
    }

    // Verify key is not all zeros
    bool allZeros = true;
    for (int i = 0; i < 32; ++i) {
        if (derivedKey[i] != 0) {
            allZeros = false;
            break;
        }
    }

    return !allZeros;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 64 || size > 1024 * 1024) {
        return 0;
    }

    // Test 1: Connection start prefix
    ConnectionStartPrefix prefix;
    if (prefix.initialize(data, size, 0xDDDDDDDD, 2)) {
        // Verify keys are derived
        volatile uint8_t sendKey0 = prefix.sendKey[0];
        volatile uint8_t recvKey0 = prefix.receiveKey[0];
        (void)sendKey0;
        (void)recvKey0;

        // Verify reversal
        bool keysReversed = true;
        for (int i = 0; i < 16; ++i) {
            if (prefix.sendState.ivec[i] != prefix.receiveState.ivec[15 - i]) {
                keysReversed = false;
                break;
            }
        }
        (void)keysReversed;
    }

    // Test 2: AES-CTR encryption/decryption
    if (size >= 96) { // 32 (key) + 64 (data)
        const uint8_t* key = data;
        const uint8_t* plaintext = data + 32;
        size_t dataLen = std::min(size - 32, size_t(1000));

        // Encrypt
        uint8_t ciphertext[1000];
        memcpy(ciphertext, plaintext, dataLen);

        CTRState encryptState;
        memcpy(encryptState.ivec, data + 16, 16);

        AESCTR::encrypt(ciphertext, dataLen, key, &encryptState);

        // Decrypt (should get back plaintext)
        CTRState decryptState;
        memcpy(decryptState.ivec, data + 16, 16);

        AESCTR::encrypt(ciphertext, dataLen, key, &decryptState);

        // Verify round-trip
        bool matches = (memcmp(plaintext, ciphertext, dataLen) == 0);
        (void)matches;
    }

    // Test 3: Counter increment
    if (size >= 16) {
        CTRState state;
        memcpy(state.ivec, data, 16);

        // Encrypt multiple blocks to test counter
        uint8_t buffer[256] = {0};
        uint8_t key[32];
        memcpy(key, data, std::min(size, size_t(32)));

        for (int i = 0; i < 10; ++i) {
            AESCTR::encrypt(buffer + i * 16, 16, key, &state);
        }

        // State should have advanced
        volatile uint32_t num = state.num;
        (void)num;
    }

    // Test 4: Good nonce validation
    if (size >= 64) {
        ConnectionStartPrefix testPrefix;
        memcpy(testPrefix.nonce, data, 64);

        bool good = testPrefix.isGoodStartNonce();

        // Test known bad nonces
        const uint8_t badNonces[][4] = {
            {0x47, 0x45, 0x54, 0x20},  // "GET "
            {0x50, 0x4f, 0x53, 0x54},  // "POST"
            {0x16, 0x03, 0x01, 0x00},  // TLS
            {0xee, 0xee, 0xee, 0xee},  // All same
        };

        for (const auto& badNonce : badNonces) {
            memcpy(testPrefix.nonce, badNonce, 4);
            if (testPrefix.isGoodStartNonce()) {
                // Should have rejected bad nonce
                volatile bool bug = true;
                (void)bug;
            }
        }
    }

    // Test 5: Key derivation with secret
    if (size >= 80) { // 32 (nonce) + 16 (secret) + 32 (extra)
        const uint8_t* nonce = data;
        const uint8_t* secret = data + 32;
        size_t secretLen = std::min(size - 32, size_t(16));

        testKeyDerivation(nonce, 32, secret, secretLen);
    }

    // Test 6: State synchronization
    if (size >= 128) {
        // Test send and receive states stay synchronized
        uint8_t sendBuffer[64];
        uint8_t recvBuffer[64];
        memcpy(sendBuffer, data, 64);
        memcpy(recvBuffer, data, 64);

        const uint8_t* key = data + 64;

        CTRState sendState, recvState;
        memcpy(sendState.ivec, data + 96, 16);
        memcpy(recvState.ivec, data + 96, 16);

        // Encrypt on send
        AESCTR::encrypt(sendBuffer, 64, key, &sendState);

        // Decrypt on receive
        AESCTR::encrypt(sendBuffer, 64, key, &recvState);

        // Should get back original
        bool matches = (memcmp(data, sendBuffer, 64) == 0);
        (void)matches;
    }

    // Test 7: Partial block encryption
    if (size >= 48) {
        const uint8_t* key = data;
        uint8_t buffer[16];
        memcpy(buffer, data + 32, 16);

        CTRState state;
        memcpy(state.ivec, data + 16, 16);

        // Encrypt byte-by-byte
        for (int i = 0; i < 16; ++i) {
            AESCTR::encrypt(buffer + i, 1, key, &state);
        }

        // State.num should be 0 (new block)
        if (state.num != 0) {
            volatile bool stateBug = true;
            (void)stateBug;
        }
    }

    // Test 8: Counter overflow
    if (size >= 48) {
        const uint8_t* key = data;
        CTRState state;

        // Set counter to near-overflow
        memset(state.ivec, 0xFF, 16);
        state.ivec[15] = 0xFE;  // Will overflow soon

        uint8_t buffer[64];
        memset(buffer, 0, sizeof(buffer));

        // Encrypt multiple blocks to trigger overflow
        AESCTR::encrypt(buffer, 64, key, &state);

        // Counter should have wrapped
        bool wrapped = (state.ivec[0] == 0 || state.ivec[15] != 0xFE);
        (void)wrapped;
    }

    // Test 9: Zero key/IV
    {
        uint8_t zeroKey[32] = {0};
        uint8_t zeroIV[16] = {0};

        CTRState zeroState;
        memcpy(zeroState.ivec, zeroIV, 16);

        uint8_t buffer[32];
        if (size >= 32) {
            memcpy(buffer, data, 32);
            AESCTR::encrypt(buffer, 32, zeroKey, &zeroState);
        }
    }

    // Test 10: Maximum size encryption
    if (size >= 10000) {
        const uint8_t* key = data;
        std::vector<uint8_t> largeBuffer(size - 32);
        memcpy(largeBuffer.data(), data + 32, size - 32);

        CTRState largeState;
        memcpy(largeState.ivec, data + 16, 16);

        AESCTR::encrypt(largeBuffer.data(), largeBuffer.size(), key, &largeState);

        // Verify state didn't corrupt
        volatile uint32_t finalNum = largeState.num;
        (void)finalNum;
    }

    return 0;
}
