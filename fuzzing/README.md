# Telegram Protocol Fuzzers

A set of **8 specialized fuzzers** for testing Telegram protocols (MTProto + Private Messages).

## 📋 Contents

### MTProto Protocol Layers (5 fuzzers)
- **mtproto_v0_fuzzer** - MTProto Version 0 (0xEFEFEFEF)
- **mtproto_v1_obfuscated_fuzzer** - MTProto Version 1 (SHA256 obfuscation)
- **mtproto_vd_padded_fuzzer** - MTProto Version D (0xDDDDDDDD, random padding)
- **tl_serialization_fuzzer** - TL (Type Language) binary serialization
- **aes_ctr_obfuscation_fuzzer** - AES-256-CTR encryption

### Private Message Encryption (3 fuzzers)
- **aes_ige_encryption_fuzzer** - AES-IGE mode (used for all messages)
- **message_key_derivation_fuzzer** - Key derivation from message key (old SHA1 + new SHA256)
- **auth_key_management_fuzzer** - 2048-bit authorization key

## 🚀 Quick Start

### Build

```bash
cmake -B build . && cmake --build build -j$(nproc)
```

All fuzzers will be built in `build/fuzzers/`

### Running a single fuzzer

```bash
# MTProto v0 (fastest - 326k exec/sec)
./build/fuzzers/mtproto_v0_fuzzer -max_total_time=60

# AES-IGE message encryption
./build/fuzzers/aes_ige_encryption_fuzzer -max_total_time=60

# Message key derivation
./build/fuzzers/message_key_derivation_fuzzer -max_total_time=60
```

## 📊 Performance

| Fuzzer | Exec/sec | Coverage |
|-------|----------|----------|
| **MTProto Protocols** |
| mtproto_v0_fuzzer | 326,686 | 49 paths, 102 features |
| tl_serialization_fuzzer | 28,355 | 196 units |
| aes_ctr_obfuscation_fuzzer | 17,895 | 105 paths, 235 features |
| mtproto_v1_obfuscated_fuzzer | TBD | TBD |
| mtproto_vd_padded_fuzzer | TBD | TBD |
| **Private Messages** |
| aes_ige_encryption_fuzzer | 21,577 | 128 paths, 275 features |
| message_key_derivation_fuzzer | TBD | TBD |
| auth_key_management_fuzzer | TBD | TBD |

## 🎯 What is tested

### MTProto Protocol Layers

#### 1. MTProto v0 (Basic)
- Packet length parsing (1-byte vs 4-byte)
- Integer overflow in `length + 4`
- Boundary values (0x00, 0x7F)

#### 2. MTProto v1 (Obfuscated)
- SHA256 key derivation
- 16-byte secret validation
- Collision resistance
- Forward/reverse keys

#### 3. MTProto vD (Padded)
- Random padding (0-15 bytes)
- Secret type detection (0xEE / 0xDD)
- 32-bit length field
- Anti-DPI obfuscation

#### 4. TL Serialization
- 32/64-bit primitives
- String encoding (short/long format)
- Vector parsing (magic 0x1cb5c415)
- Padding to 4-byte boundary

#### 5. AES-CTR Obfuscation
- 64-byte connection nonce
- Key derivation from nonce
- CTR counter overflow
- "Good nonce" validation (not HTTP/TLS)

### Private Message Encryption

#### 6. AES-IGE Encryption
- **IGE mode** (Infinite Garble Extension) - more secure than CBC
- Encryption/decryption with 256-bit keys
- 32-byte IV (2 blocks for IGE)
- Bit flipping resistance
- Used for **ALL** Telegram messages

#### 7. Message Key Derivation
- **Old version** (SHA1-based) - prepareAES_oldmtp()
  - 4x SHA1 hashes
  - Combining parts of authKey with msgKey
- **New version** (SHA256-based) - prepareAES()
  - 2x SHA256 hashes
  - More secure derivation
- Derives AES key (256-bit) and IV (256-bit) from:
  - 2048-bit auth key
  - 128-bit message key
  - Send/receive direction

#### 8. Auth Key Management
- **2048-bit (256 bytes)** authorization key
- KeyID calculation (lower 64 bits of SHA1)
- Key types: Generated, Temporary, ReadFromFile, Local
- Serialization/deserialization
- Collision resistance testing
- Part extraction for message key derivation

## 🔧 Requirements

- Clang with libFuzzer support
- AddressSanitizer, UndefinedBehaviorSanitizer
- CMake 3.16+

## 📖 Documentation

Detailed documentation: [PROTOCOL_FUZZERS_SUMMARY.md](PROTOCOL_FUZZERS_SUMMARY.md)

## 🔗 OSS-Fuzz Integration

Fuzzers are ready for integration into Google OSS-Fuzz:
- Standalone design (no dependencies)
- Fast build (<10 seconds)
- Full sanitizer coverage

## 📝 Structure

```
.
├── CMakeLists.txt                          # Build configuration
├── README.md                               # This file
├── PROTOCOL_FUZZERS_SUMMARY.md             # Detailed documentation
├── run_all_fuzzers.sh                      # Run all fuzzers
├── run_parallel_fuzzer.sh                  # Parallel run of single fuzzer
│
├── mtproto_v0_fuzzer.cpp                   # 180 lines
├── mtproto_v1_obfuscated_fuzzer.cpp        # 235 lines
├── mtproto_vd_padded_fuzzer.cpp            # 312 lines
├── tl_serialization_fuzzer.cpp             # 351 lines
├── aes_ctr_obfuscation_fuzzer.cpp          # 387 lines
│
├── aes_ige_encryption_fuzzer.cpp           # 331 lines
├── message_key_derivation_fuzzer.cpp       # 322 lines
└── auth_key_management_fuzzer.cpp          # 317 lines
```

**Total**: 2,435 lines of fuzzer code

## 🔐 Telegram Encryption Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Application Layer (Messages)                                │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│ Message Encryption:                                         │
│  ┌──────────────┐    ┌─────────────────┐                   │
│  │ Message Key  │───▶│ Key Derivation  │                   │
│  │   128-bit    │    │ (SHA1/SHA256)   │                   │
│  └──────────────┘    └────────┬────────┘                   │
│                               │                             │
│                       ┌───────▼────────┐                   │
│                       │  AES-IGE Mode  │                   │
│                       │   256-bit key  │                   │
│                       └────────────────┘                   │
└─────────────────────────────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│ Protocol Layer (MTProto)                                    │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ Version 0   │  │  Version 1   │  │  Version D   │      │
│  │ (Basic)     │  │ (SHA256)     │  │ (Padding)    │      │
│  └─────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│ TL Serialization                                            │
└─────────────────────────────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│ Connection Obfuscation (AES-CTR)                            │
└─────────────────────────────────────────────────────────────┘
                       │
                       ▼
                 TCP/Network
```

## 🐛 Bug Discovery

Crashes are saved in `crashes/<fuzzer_name>/crash-<hash>`

To reproduce:
```bash
./build/fuzzers/<fuzzer_name> crashes/<fuzzer_name>/crash-<hash>
```

---

**Created**: 2025-10-10
**Location**: `/home/kali/tdesktop/fuzzing/`
**Fuzzers**: 8 (5 protocol + 3 private message)
