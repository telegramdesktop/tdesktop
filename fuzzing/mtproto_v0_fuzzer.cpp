/*
MTProto Version 0 Protocol Fuzzer
Targets: mtproto/connection_tcp.cpp - Protocol::Version0
Magic ID: 0xEFEFEFEF
Critical: Basic packet parsing without obfuscation
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

// Extracted from connection_tcp.cpp:54-138
class ProtocolVersion0 {
public:
    static constexpr auto kUnknownSize = -1;
    static constexpr auto kInvalidSize = -2;
    static constexpr auto kPacketSizeMax = int(0x01000000 * 4); // 16MB

    // Exact implementation from Telegram
    int readPacketLength(const uint8_t* bytes, size_t size) const {
        if (size == 0) {
            return kUnknownSize;
        }

        const auto first = static_cast<char>(bytes[0]);
        if (first == 0x7F) {
            // 4-byte length encoding
            if (size < 4) {
                return kUnknownSize;
            }
            const auto ints = static_cast<uint32_t>(bytes[1])
                | (static_cast<uint32_t>(bytes[2]) << 8)
                | (static_cast<uint32_t>(bytes[3]) << 16);

            // Critical check: ints must be >= 0x7F
            return (ints >= 0x7F) ? (int(ints << 2) + 4) : kInvalidSize;
        } else if (first > 0 && first < 0x7F) {
            // 1-byte length encoding
            const auto ints = uint32_t(first);
            return int(ints << 2) + 1;
        }
        return kInvalidSize;
    }

    bool readPacket(const uint8_t* bytes, size_t size, size_t* outPacketSize) const {
        const auto packetSize = readPacketLength(bytes, size);

        if (packetSize == kUnknownSize) {
            return false; // Need more data
        }
        if (packetSize == kInvalidSize) {
            return false; // Invalid packet
        }
        if (packetSize < 0 || packetSize > kPacketSizeMax) {
            return false; // Size out of bounds
        }
        if (static_cast<size_t>(packetSize) > size) {
            return false; // Not enough data
        }

        *outPacketSize = static_cast<size_t>(packetSize);
        return true;
    }
};

// Test packet finalization (encoding)
bool testPacketEncoding(const uint8_t* data, size_t size) {
    if (size < 4 || size > 1000000) {
        return false;
    }

    // Simulate packet encoding
    const auto intsSize = (size - 2) / 4;

    if (intsSize < 0x7F) {
        // 1-byte length
        uint8_t encoded[1];
        encoded[0] = static_cast<uint8_t>(intsSize);
        volatile uint8_t check = encoded[0];
        (void)check;
    } else {
        // 4-byte length
        uint8_t encoded[4];
        encoded[0] = 0x7F;
        encoded[1] = static_cast<uint8_t>(intsSize & 0xFF);
        encoded[2] = static_cast<uint8_t>((intsSize >> 8) & 0xFF);
        encoded[3] = static_cast<uint8_t>((intsSize >> 16) & 0xFF);

        // Verify round-trip
        const auto decoded = static_cast<uint32_t>(encoded[1])
            | (static_cast<uint32_t>(encoded[2]) << 8)
            | (static_cast<uint32_t>(encoded[3]) << 16);

        if (decoded != intsSize) {
            return false; // Encoding mismatch
        }
    }

    return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 1024 * 1024) {
        return 0;
    }

    ProtocolVersion0 protocol;

    // Test 1: Parse packet length
    int packetLength = protocol.readPacketLength(data, size);

    // Test 2: Handle all possible length values
    if (packetLength > 0) {
        size_t outSize = 0;
        bool valid = protocol.readPacket(data, size, &outSize);

        if (valid && outSize > 0) {
            // Access packet data safely
            const size_t sizeLength = (static_cast<char>(data[0]) == 0x7F) ? 4 : 1;
            if (sizeLength < size) {
                volatile uint8_t firstByte = data[sizeLength];
                (void)firstByte;
            }
        }
    }

    // Test 3: Edge cases
    if (size >= 1) {
        // Test boundary values
        uint8_t testCases[] = {
            0x00,  // Invalid
            0x01,  // Min valid (1-byte)
            0x7E,  // Max 1-byte
            0x7F,  // Switch to 4-byte
            0x80,  // After switch
            0xFF,  // Max byte
        };

        for (auto testByte : testCases) {
            uint8_t testBuffer[256];
            testBuffer[0] = testByte;
            if (size > 1) {
                memcpy(testBuffer + 1, data, std::min(size - 1, size_t(255)));
            }

            protocol.readPacketLength(testBuffer, std::min(size + 1, size_t(256)));
        }
    }

    // Test 4: Packet encoding
    testPacketEncoding(data, size);

    // Test 5: Multiple packets in stream
    size_t offset = 0;
    int packetsFound = 0;
    while (offset < size && packetsFound < 100) {
        size_t packetSize = 0;
        if (protocol.readPacket(data + offset, size - offset, &packetSize)) {
            offset += packetSize;
            packetsFound++;
        } else {
            break;
        }
    }

    // Test 6: Integer overflow scenarios
    if (size >= 4) {
        // Test max valid value
        uint8_t maxTest[4] = {0x7F, 0xFF, 0xFF, 0xFF};
        protocol.readPacketLength(maxTest, 4);

        // Test overflow scenarios
        uint8_t overflowTest[4] = {0x7F, 0xFF, 0xFF, 0x3F}; // Max safe value
        protocol.readPacketLength(overflowTest, 4);
    }

    return 0;
}
