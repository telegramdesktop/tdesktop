#!/bin/bash -eu
# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Build Telegram Protocol Fuzzers
# These are standalone fuzzers that don't require the full tdesktop build

cd $SRC/telegram_fuzzers

# List of fuzzers to build
FUZZERS=(
    "mtproto_v0_fuzzer"
    "mtproto_v1_obfuscated_fuzzer"
    "mtproto_vd_padded_fuzzer"
    "tl_serialization_fuzzer"
    "aes_ctr_obfuscation_fuzzer"
    "aes_ige_encryption_fuzzer"
    "message_key_derivation_fuzzer"
    "auth_key_management_fuzzer"
)

# Build each fuzzer
for fuzzer in "${FUZZERS[@]}"; do
    echo "Building $fuzzer..."
    $CXX $CXXFLAGS -std=c++20 -c "${fuzzer}.cpp" -o "${fuzzer}.o"
    $CXX $CXXFLAGS $LIB_FUZZING_ENGINE "${fuzzer}.o" -o "$OUT/${fuzzer}"
done

echo "All fuzzers built successfully!"
