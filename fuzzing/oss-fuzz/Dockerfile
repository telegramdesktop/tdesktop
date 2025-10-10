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

FROM gcr.io/oss-fuzz-base/base-builder

# Clone the repository with fuzzing branch
RUN git clone --depth 1 --branch ossFuzz https://github.com/telegramdesktop/tdesktop $SRC/tdesktop

# Create fuzzer directory
RUN mkdir -p $SRC/telegram_fuzzers

# Copy build script and standalone fuzzer sources to fuzzer directory
COPY build.sh $SRC/
COPY *.cpp $SRC/telegram_fuzzers/

WORKDIR $SRC/telegram_fuzzers
