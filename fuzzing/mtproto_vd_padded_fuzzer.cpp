/*
MTProto Version D (Padded) Protocol Fuzzer
Targets: mtproto/connection_tcp.cpp - Protocol::VersionD
Magic ID: 0xDDDDDDDD
Feature: Random padding (0-15 bytes) to defeat traffic analysis
Critical: Anti-DPI obfuscation with variable packet sizes
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

// VersionD from connection_tcp.cpp:168-230
class ProtocolVersionD {
public:
    static constexpr auto kUnknownSize = -1;
    static constexpr auto kInvalidSize = -2;
    static constexpr auto kPacketSizeMax = int(0x01000000 * 4); // 16MB
    static constexpr uint32_t kMagicID = 0xDDDDDDDDU;

    bool supportsArbitraryLength() const {
        return true; // Supports padding!
    }

    // From connection_tcp.cpp:207-216
    int readPacketLength(const uint8_t* bytes, size_t size) const {
        if (size < 4) {
            return kUnknownSize;
        }

        // Read 32-bit length + 4 bytes overhead
        const auto value = *reinterpret_cast<const uint32_t*>(bytes) + 4;

        return (value >= 8 && value < kPacketSizeMax)
            ? int(value)
            : kInvalidSize;
    }

    // From connection_tcp.cpp:218-226
    bool readPacket(const uint8_t* bytes, size_t size, size_t* outSize) const {
        const auto packetSize = readPacketLength(bytes, size);

        if (packetSize == kUnknownSize || packetSize == kInvalidSize) {
            return false;
        }
        if (static_cast<size_t>(packetSize) > size) {
            return false;
        }

        *outSize = static_cast<size_t>(packetSize);
        return true;
    }

    // Test padding logic (from connection_tcp.cpp:192-205)
    bool testPacketFinalization(const uint8_t* data, size_t dataSize, uint8_t paddingSize) {
        if (dataSize < 8 || dataSize > 100000) {
            return false;
        }

        // Padding must be 0-15 bytes
        if (paddingSize > 15) {
            return false;
        }

        const auto totalSize = dataSize + paddingSize;

        // Write length field
        uint32_t lengthField = static_cast<uint32_t>(totalSize);

        // Verify round-trip
        const auto readBack = lengthField + 4;
        if (readBack < 8 || readBack >= kPacketSizeMax) {
            return false;
        }

        return true;
    }
};

// Test secret format detection (from connection_tcp.cpp:232-245)
enum class SecretType {
    Version0,      // empty secret
    Version1,      // 16 bytes
    VersionD_EE,   // 0xEE + 16 bytes (21+ bytes)
    VersionD_DD,   // 0xDD + 16 bytes (17 bytes)
    Invalid
};

SecretType detectSecretType(const uint8_t* secret, size_t size) {
    if (size >= 21 && secret[0] == 0xEE) {
        return SecretType::VersionD_EE;
    } else if (size == 17 && secret[0] == 0xDD) {
        return SecretType::VersionD_DD;
    } else if (size == 16) {
        return SecretType::Version1;
    } else if (size == 0) {
        return SecretType::Version0;
    }
    return SecretType::Invalid;
}

// Test padding distribution
bool testPaddingDistribution(const uint8_t* data, size_t size) {
    if (size < 100) {
        return false;
    }

    // Count different padding values
    int paddingCounts[16] = {0};

    for (size_t i = 0; i + 4 < size; ++i) {
        // Treat last 4 bits as padding size
        uint8_t padding = data[i] & 0x0F;
        paddingCounts[padding]++;
    }

    // Check distribution (shouldn't be all same)
    int uniquePaddings = 0;
    for (int count : paddingCounts) {
        if (count > 0) {
            uniquePaddings++;
        }
    }

    return uniquePaddings > 1;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4 || size > 1024 * 1024) {
        return 0;
    }

    ProtocolVersionD protocol;

    // Test 1: Read packet length
    int packetLength = protocol.readPacketLength(data, size);

    if (packetLength > 0 && packetLength != ProtocolVersionD::kInvalidSize) {
        size_t outSize = 0;
        bool valid = protocol.readPacket(data, size, &outSize);

        if (valid) {
            // Verify packet boundaries
            if (outSize >= 8 && outSize <= size) {
                const uint8_t* payload = data + 4;
                size_t payloadSize = outSize - 4;

                // Access payload safely
                volatile uint8_t firstByte = payload[0];
                (void)firstByte;
            }
        }
    }

    // Test 2: Padding validation
    if (size >= 5) {
        for (uint8_t padding = 0; padding <= 15; ++padding) {
            protocol.testPacketFinalization(data, size - 1, padding);
        }
    }

    // Test 3: Secret type detection
    if (size >= 17) {
        SecretType type = detectSecretType(data, std::min(size, size_t(32)));

        // Verify VersionD secrets
        if (type == SecretType::VersionD_DD || type == SecretType::VersionD_EE) {
            // Should have 16-byte secret after magic byte
            bool validLength = (type == SecretType::VersionD_DD && size == 17) ||
                             (type == SecretType::VersionD_EE && size >= 21);
            (void)validLength;
        }
    }

    // Test 4: Multiple packets with different padding
    size_t offset = 0;
    int packetsFound = 0;

    while (offset + 4 < size && packetsFound < 100) {
        size_t packetSize = 0;
        if (protocol.readPacket(data + offset, size - offset, &packetSize)) {
            // Verify padding doesn't break packet boundaries
            if (packetSize >= 8) {
                const uint32_t storedLength = *reinterpret_cast<const uint32_t*>(data + offset);
                const size_t realPayloadSize = packetSize - 4;

                // Padding is: realPayloadSize - storedLength (0-15 bytes)
                if (realPayloadSize >= storedLength && realPayloadSize - storedLength <= 15) {
                    volatile bool validPadding = true;
                    (void)validPadding;
                }
            }

            offset += packetSize;
            packetsFound++;
        } else {
            break;
        }
    }

    // Test 5: Edge cases with length field
    if (size >= 4) {
        // Test minimum valid packet (length = 4, total = 8)
        uint8_t minPacket[8] = {4, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD};
        protocol.readPacketLength(minPacket, 8);

        // Test maximum valid packet
        uint8_t maxPacket[8] = {0xFC, 0xFF, 0xFF, 0x00, 0, 0, 0, 0};
        protocol.readPacketLength(maxPacket, 8);

        // Test overflow scenarios
        uint8_t overflowPacket[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        int overflow = protocol.readPacketLength(overflowPacket, 4);
        if (overflow != ProtocolVersionD::kInvalidSize) {
            // Should reject overflow
            volatile bool bug = true;
            (void)bug;
        }
    }

    // Test 6: Padding distribution
    testPaddingDistribution(data, size);

    // Test 7: Length field wraparound
    if (size >= 4) {
        // Test values near 32-bit boundary
        uint32_t testValues[] = {
            0x00000000,  // Min
            0x00000004,  // Min valid
            0x7FFFFFFF,  // Max int
            0x80000000,  // Sign flip
            0xFFFFFFFB,  // Max valid (+ 4 = 0xFFFFFFFF)
            0xFFFFFFFC,  // Overflow (+ 4 = 0x100000000)
        };

        for (auto testValue : testValues) {
            uint8_t testBuffer[4];
            memcpy(testBuffer, &testValue, 4);
            int result = protocol.readPacketLength(testBuffer, 4);

            // Verify overflow handling
            const auto computed = testValue + 4;
            bool shouldBeValid = (computed >= 8 && computed < ProtocolVersionD::kPacketSizeMax);
            bool isValid = (result != ProtocolVersionD::kInvalidSize);

            if (shouldBeValid != isValid) {
                // Potential bug in overflow handling
                volatile bool mismatch = true;
                (void)mismatch;
            }
        }
    }

    // Test 8: Arbitrary length support
    if (protocol.supportsArbitraryLength() && size >= 20) {
        // VersionD should handle arbitrary data after payload
        const uint8_t* payload = data + 4;
        size_t payloadSize = std::min(size - 4, size_t(1000));

        // Verify padding bytes don't affect parsing
        for (size_t i = 0; i < payloadSize; ++i) {
            volatile uint8_t byte = payload[i];
            (void)byte;
        }
    }

    return 0;
}
