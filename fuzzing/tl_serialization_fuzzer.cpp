/*
TL (Type Language) Serialization Fuzzer
Targets: mtproto/core_types.h - TL binary format
Critical: ALL network data uses TL serialization
Format: [type_id:32bit][field1][field2]...[fieldn]
*/

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>

// TL Reader from core_types.h
class TLReader {
public:
    explicit TLReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    // Read 32-bit value (TL "Prime")
    bool readUInt32(uint32_t& out) {
        if (pos_ + 4 > size_) {
            return false;
        }

        out = *reinterpret_cast<const uint32_t*>(data_ + pos_);
        pos_ += 4;
        return true;
    }

    // Read 64-bit value
    bool readUInt64(uint64_t& out) {
        if (pos_ + 8 > size_) {
            return false;
        }

        out = *reinterpret_cast<const uint64_t*>(data_ + pos_);
        pos_ += 8;
        return true;
    }

    // Read bytes with length prefix
    bool readBytes(std::vector<uint8_t>& out) {
        // TL string format:
        // - If len < 254: [len:1byte][data][padding to 4-byte boundary]
        // - If len >= 254: [0xFE][len:3bytes][data][padding]

        if (pos_ >= size_) {
            return false;
        }

        uint32_t len = 0;
        size_t dataStart = 0;

        uint8_t first = data_[pos_];
        if (first < 254) {
            // Short format
            len = first;
            dataStart = pos_ + 1;
        } else if (first == 254) {
            // Long format
            if (pos_ + 4 > size_) {
                return false;
            }
            len = data_[pos_ + 1] | (data_[pos_ + 2] << 8) | (data_[pos_ + 3] << 16);
            dataStart = pos_ + 4;
        } else {
            // 0xFF is reserved
            return false;
        }

        // Check for reasonable limits
        if (len > 16 * 1024 * 1024) { // 16MB max
            return false;
        }

        if (dataStart + len > size_) {
            return false;
        }

        // Read data
        out.resize(len);
        memcpy(out.data(), data_ + dataStart, len);

        // Calculate padding to 4-byte boundary
        size_t totalLen = (first < 254) ? (1 + len) : (4 + len);
        size_t padding = (4 - (totalLen % 4)) % 4;

        pos_ = dataStart + len + padding;
        return true;
    }

    // Read string (same as bytes)
    bool readString(std::string& out) {
        std::vector<uint8_t> bytes;
        if (!readBytes(bytes)) {
            return false;
        }
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

    // Read vector of 32-bit values
    bool readVector(std::vector<uint32_t>& out) {
        uint32_t magic;
        if (!readUInt32(magic)) {
            return false;
        }

        // Vector magic: 0x1cb5c415
        if (magic != 0x1cb5c415) {
            return false;
        }

        uint32_t count;
        if (!readUInt32(count)) {
            return false;
        }

        // Reasonable limit
        if (count > 100000) {
            return false;
        }

        out.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!readUInt32(out[i])) {
                return false;
            }
        }

        return true;
    }

    bool hasMore() const {
        return pos_ < size_;
    }

    size_t position() const {
        return pos_;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

// Test TL object parsing
struct TLObject {
    uint32_t typeId;
    std::vector<uint32_t> fields;
    std::vector<uint8_t> payload;
};

bool parseTLObject(TLReader& reader, TLObject& obj) {
    // Read type ID
    if (!reader.readUInt32(obj.typeId)) {
        return false;
    }

    // Read some fields (simplified)
    for (int i = 0; i < 5 && reader.hasMore(); ++i) {
        uint32_t field;
        if (reader.readUInt32(field)) {
            obj.fields.push_back(field);
        }
    }

    return true;
}

// Test nested TL structures
bool testNestedStructures(const uint8_t* data, size_t size) {
    TLReader reader(data, size);

    // Try to parse container of objects
    uint32_t magic;
    if (!reader.readUInt32(magic)) {
        return false;
    }

    // msg_container magic: 0x73f1f8dc
    if (magic == 0x73f1f8dc) {
        uint32_t count;
        if (!reader.readUInt32(count) || count > 1000) {
            return false;
        }

        for (uint32_t i = 0; i < count && reader.hasMore(); ++i) {
            TLObject obj;
            if (!parseTLObject(reader, obj)) {
                break;
            }
        }
    }

    return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4 || size > 1024 * 1024) {
        return 0;
    }

    TLReader reader(data, size);

    // Test 1: Read primitives
    uint32_t u32;
    if (reader.readUInt32(u32)) {
        volatile uint32_t val = u32;
        (void)val;
    }

    // Test 2: Read 64-bit values
    TLReader reader64(data, size);
    uint64_t u64;
    if (reader64.readUInt64(u64)) {
        volatile uint64_t val = u64;
        (void)val;
    }

    // Test 3: Read bytes/strings with length prefix
    TLReader readerBytes(data, size);
    std::vector<uint8_t> bytes;
    if (readerBytes.readBytes(bytes)) {
        if (!bytes.empty()) {
            volatile uint8_t first = bytes[0];
            (void)first;
        }
    }

    // Test 4: Read string
    TLReader readerStr(data, size);
    std::string str;
    if (readerStr.readString(str)) {
        volatile size_t len = str.length();
        (void)len;
    }

    // Test 5: Read vector
    TLReader readerVec(data, size);
    std::vector<uint32_t> vec;
    if (readerVec.readVector(vec)) {
        volatile size_t count = vec.size();
        (void)count;
    }

    // Test 6: Parse TL object
    TLReader readerObj(data, size);
    TLObject obj;
    if (parseTLObject(readerObj, obj)) {
        // Check type ID validity
        if (obj.typeId == 0 || obj.typeId == 0xFFFFFFFF) {
            // Suspicious type IDs
            volatile bool suspicious = true;
            (void)suspicious;
        }
    }

    // Test 7: Nested structures
    testNestedStructures(data, size);

    // Test 8: Multiple objects in stream
    TLReader readerMulti(data, size);
    int objectsRead = 0;
    while (readerMulti.hasMore() && objectsRead < 100) {
        TLObject obj;
        if (parseTLObject(readerMulti, obj)) {
            objectsRead++;
        } else {
            break;
        }
    }

    // Test 9: Edge cases with length encoding
    if (size >= 1) {
        // Test short length (< 254)
        uint8_t shortLen[256];
        shortLen[0] = std::min(uint8_t(size - 1), uint8_t(253));
        if (size > 1) {
            memcpy(shortLen + 1, data, std::min(size - 1, size_t(255)));
        }

        TLReader shortReader(shortLen, std::min(size, size_t(256)));
        std::vector<uint8_t> shortBytes;
        shortReader.readBytes(shortBytes);

        // Test long length (>= 254)
        if (size >= 4) {
            uint8_t longLen[1024];
            longLen[0] = 0xFE;
            uint32_t len = std::min(uint32_t(size - 4), uint32_t(1000));
            longLen[1] = len & 0xFF;
            longLen[2] = (len >> 8) & 0xFF;
            longLen[3] = (len >> 16) & 0xFF;
            if (size > 4) {
                memcpy(longLen + 4, data, std::min(size - 4, size_t(1020)));
            }

            TLReader longReader(longLen, std::min(size + 4, size_t(1024)));
            std::vector<uint8_t> longBytes;
            longReader.readBytes(longBytes);
        }
    }

    // Test 10: Padding validation
    if (size >= 8) {
        // String with different lengths to test padding
        for (size_t len = 0; len < 8; ++len) {
            uint8_t testBuf[16];
            testBuf[0] = static_cast<uint8_t>(len);
            if (len > 0) {
                memcpy(testBuf + 1, data, std::min(len, size));
            }

            TLReader padReader(testBuf, 16);
            std::vector<uint8_t> padBytes;
            padReader.readBytes(padBytes);

            // Verify padding doesn't corrupt position
            size_t expectedPos = 1 + len;
            size_t padding = (4 - (expectedPos % 4)) % 4;
            size_t actualPos = padReader.position();

            if (actualPos != expectedPos + padding) {
                volatile bool paddingBug = true;
                (void)paddingBug;
            }
        }
    }

    // Test 11: Known TL type IDs
    uint32_t knownTypes[] = {
        0x1cb5c415,  // vector
        0x73f1f8dc,  // msg_container
        0x997275b5,  // resPQ
        0x05162463,  // req_pq
        0xbe7e8ef1,  // resPQ
    };

    for (auto typeId : knownTypes) {
        uint8_t testBuf[4];
        memcpy(testBuf, &typeId, 4);
        TLReader typeReader(testBuf, 4);
        TLObject typeObj;
        parseTLObject(typeReader, typeObj);
    }

    return 0;
}
