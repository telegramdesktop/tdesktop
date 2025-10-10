# Integration Guide - Telegram Protocol Fuzzers

This document explains how these fuzzers integrate with Telegram Desktop and Google OSS-Fuzz.

## Overview

These fuzzers are **standalone** - they don't require linking against the full Telegram Desktop codebase. Instead, they contain simplified implementations of protocol parsing and encryption logic extracted from tdesktop source files.

## Why Standalone?

1. **Fast Build** - Compiles in ~5 seconds (vs hours for full tdesktop)
2. **No Dependencies** - Works without Qt, OpenSSL, or other tdesktop dependencies
3. **OSS-Fuzz Ready** - Simple Docker build process
4. **Portable** - Runs locally and in OSS-Fuzz without modification

## File Structure in tdesktop

```
tdesktop/
├── Telegram/              # Main application code
│   └── SourceFiles/
│       └── mtproto/       # Protocol implementation (fuzzing targets)
├── cmake/                 # Build system
├── docs/                  # Documentation
└── fuzzing/              # ← This directory
    ├── *.cpp             # 8 fuzzer sources
    ├── CMakeLists.txt    # Standalone build
    ├── README.md         # Documentation
    ├── INTEGRATION.md    # This file
    └── oss-fuzz/         # OSS-Fuzz files
```

## Fuzzing Targets

These fuzzers test protocol implementations from:

### `Telegram/SourceFiles/mtproto/mtproto_auth_key.cpp`
- **auth_key_management_fuzzer** → Tests `AuthKey` class
- **message_key_derivation_fuzzer** → Tests `prepareAES()` and `prepareAES_oldmtp()`
- **aes_ige_encryption_fuzzer** → Tests `aesIgeEncryptRaw()` and `aesIgeDecryptRaw()`

### `Telegram/SourceFiles/mtproto/connection_tcp.cpp`
- **mtproto_v0_fuzzer** → Tests MTProto v0 packet parsing
- **mtproto_v1_obfuscated_fuzzer** → Tests obfuscated handshake
- **mtproto_vd_padded_fuzzer** → Tests padded protocol
- **aes_ctr_obfuscation_fuzzer** → Tests connection obfuscation

### TL Serialization
- **tl_serialization_fuzzer** → Tests Type Language binary format

## Local Testing

### Build Fuzzers

```bash
cd fuzzing
cmake -B build .
cmake --build build -j$(nproc)
```

**Requirements:**
- Clang compiler
- libFuzzer support

### Run Single Fuzzer

```bash
./build/fuzzers/mtproto_v0_fuzzer -max_total_time=60
```

### Run All Fuzzers

```bash
# Test each fuzzer for 60 seconds
for fuzzer in build/fuzzers/*_fuzzer; do
    echo "Testing $fuzzer..."
    $fuzzer -max_total_time=60
done
```

## Integration with tdesktop Build System

These fuzzers are **optional** and don't affect the main tdesktop build. They can be:

1. **Standalone** - Built independently using their own CMakeLists.txt
2. **Optional** - Only built when explicitly requested
3. **Skipped** - If Clang is not available

### Option 1: Keep Separate (Recommended)

Fuzzers remain in `fuzzing/` directory with their own build system:

```cmake
# fuzzing/CMakeLists.txt (already provided)
# Builds only when explicitly invoked
```

Build:
```bash
cd fuzzing
cmake -B build . && cmake --build build
```

### Option 2: Add to Root CMakeLists.txt (Optional)

If tdesktop maintainers want to optionally include fuzzers:

```cmake
# In root CMakeLists.txt
option(BUILD_FUZZERS "Build protocol fuzzers" OFF)

if (BUILD_FUZZERS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_subdirectory(fuzzing)
    else()
        message(STATUS "Skipping fuzzers (requires Clang)")
    endif()
endif()
```

Build with fuzzers:
```bash
cmake -B build -DBUILD_FUZZERS=ON .
cmake --build build
```

## OSS-Fuzz Integration

### Directory Structure in OSS-Fuzz

```
oss-fuzz/
└── projects/
    └── telegram/
        ├── project.yaml      # Project config
        ├── Dockerfile        # Docker build environment
        ├── build.sh          # Build script
        └── *.cpp            # Fuzzer sources (copied from tdesktop/fuzzing/)
```

### Build Process

1. **Clone tdesktop**:
   ```dockerfile
   RUN git clone --depth 1 https://github.com/telegramdesktop/tdesktop $SRC/tdesktop
   ```

2. **Copy fuzzer sources**:
   ```dockerfile
   COPY *.cpp $SRC/telegram_fuzzers/
   ```

3. **Build fuzzers**:
   ```bash
   cd $SRC/telegram_fuzzers
   for fuzzer in *.cpp; do
       $CXX $CXXFLAGS -std=c++20 -c $fuzzer
       $CXX $CXXFLAGS $LIB_FUZZING_ENGINE ${fuzzer%.cpp}.o -o $OUT/${fuzzer%.cpp}
   done
   ```

### Why Not Link Against tdesktop?

The tdesktop build is complex:
- Requires Qt 6
- Multiple dependencies (OpenSSL, FFmpeg, etc.)
- Platform-specific code
- Long build time (hours)

Standalone fuzzers:
- ✅ Build in ~5 seconds
- ✅ Zero dependencies
- ✅ Simple Docker image
- ✅ Works on all platforms

## Testing in OSS-Fuzz

From oss-fuzz repository:

```bash
# Build Docker image
python infra/helper.py build_image telegram

# Build fuzzers
python infra/helper.py build_fuzzers telegram

# Test a fuzzer
python infra/helper.py run_fuzzer telegram mtproto_v0_fuzzer

# Check build quality
python infra/helper.py check_build telegram
```

## Maintenance

### Adding New Fuzzers

1. Create `new_fuzzer.cpp` in `fuzzing/`
2. Add to `FUZZERS` list in `CMakeLists.txt`
3. Update `oss-fuzz/build.sh` (fuzzers auto-detected from `*.cpp`)
4. Test locally and in OSS-Fuzz

### Updating Fuzzers

When tdesktop protocol code changes:

1. Review changes in `Telegram/SourceFiles/mtproto/`
2. Update corresponding fuzzer if protocol changed
3. Test locally
4. OSS-Fuzz will automatically pick up changes after PR merge

## CI/CD Integration

### GitHub Actions (Optional)

Add fuzzer testing to tdesktop CI:

```yaml
name: Fuzzer Tests

on: [push, pull_request]

jobs:
  fuzzers:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install Clang
        run: sudo apt-get install -y clang

      - name: Build Fuzzers
        run: |
          cd fuzzing
          cmake -B build -DCMAKE_CXX_COMPILER=clang++ .
          cmake --build build -j$(nproc)

      - name: Test Fuzzers (Quick)
        run: |
          for fuzzer in fuzzing/build/fuzzers/*_fuzzer; do
            echo "Testing $fuzzer..."
            $fuzzer -max_total_time=10 -max_len=4096
          done
```

## Security Considerations

### Bug Reporting

When OSS-Fuzz finds issues:
1. Private report sent to configured contacts
2. 90-day disclosure deadline
3. Fix, test, release
4. Public disclosure

### Coverage Improvements

Monitor OSS-Fuzz coverage reports to identify:
- Uncovered code paths
- Edge cases not tested
- New fuzzing opportunities

## References

- **OSS-Fuzz Docs**: https://google.github.io/oss-fuzz/
- **libFuzzer Tutorial**: https://github.com/google/fuzzing/blob/master/tutorial/libFuzzerTutorial.md
- **Telegram Protocol**: https://core.telegram.org/mtproto
- **tdesktop Source**: https://github.com/telegramdesktop/tdesktop

## Questions?

For questions about:
- **Fuzzer Implementation**: See source code comments
- **OSS-Fuzz Integration**: See `oss-fuzz/README.md`
- **Local Testing**: See `README.md`

---

**Maintainer**: Vahagn Vardanian (@vah13)
**Purpose**: Continuous security testing via Google OSS-Fuzz
**Status**: Production ready
