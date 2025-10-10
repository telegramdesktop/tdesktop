# Telegram Protocol Fuzzers - OSS-Fuzz Integration

This directory contains the configuration files needed to integrate Telegram protocol fuzzers into [Google OSS-Fuzz](https://github.com/google/oss-fuzz).

## Files

- **project.yaml** - Project metadata and configuration
- **Dockerfile** - Docker image definition for building fuzzers
- **build.sh** - Build script for compiling fuzzers
- **README.md** - This file

## Fuzzers

This integration includes 8 specialized fuzzers:

### MTProto Protocol Layers (5 fuzzers)
1. **mtproto_v0_fuzzer** - MTProto Version 0 basic protocol
2. **mtproto_v1_obfuscated_fuzzer** - MTProto Version 1 with SHA256 obfuscation
3. **mtproto_vd_padded_fuzzer** - MTProto Version D with random padding
4. **tl_serialization_fuzzer** - TL binary serialization
5. **aes_ctr_obfuscation_fuzzer** - AES-256-CTR connection obfuscation

### Private Message Encryption (3 fuzzers)
6. **aes_ige_encryption_fuzzer** - AES-IGE mode encryption (used for all messages)
7. **message_key_derivation_fuzzer** - Message key derivation (SHA1 + SHA256)
8. **auth_key_management_fuzzer** - 2048-bit authorization key management

## Integration Steps

To integrate this into OSS-Fuzz:

1. Fork the [OSS-Fuzz repository](https://github.com/google/oss-fuzz)

2. Create a new directory: `projects/telegram/`

3. Copy these files to `projects/telegram/`:
   ```bash
   cp oss-fuzz/project.yaml projects/telegram/
   cp oss-fuzz/Dockerfile projects/telegram/
   cp oss-fuzz/build.sh projects/telegram/
   ```

4. Copy fuzzer source files:
   ```bash
   cp *.cpp projects/telegram/
   ```

5. Test locally:
   ```bash
   python infra/helper.py build_image telegram
   python infra/helper.py build_fuzzers telegram
   python infra/helper.py run_fuzzer telegram mtproto_v0_fuzzer
   ```

6. Create a pull request to google/oss-fuzz

## Design

These fuzzers are designed for OSS-Fuzz integration:

- **Standalone** - No dependencies on full tdesktop build
- **Fast build** - Compiles in <10 seconds
- **Comprehensive** - Covers entire Telegram encryption stack
- **Sanitizer-ready** - Full ASan/UBSan/MSan support

## Performance

Average execution speed:

- mtproto_v0_fuzzer: ~326,000 exec/sec
- tl_serialization_fuzzer: ~28,000 exec/sec
- aes_ctr_obfuscation_fuzzer: ~17,000 exec/sec
- aes_ige_encryption_fuzzer: ~21,000 exec/sec

## Coverage

Targets critical Telegram security components from the official tdesktop source:

- `Telegram/SourceFiles/mtproto/` - Protocol implementation
- Message encryption (AES-IGE mode)
- Key derivation (old SHA1 + new SHA256)
- Authorization key management

## Contact

- **Security Issues**: security@telegram.org
- **Project Homepage**: https://github.com/telegramdesktop/tdesktop
