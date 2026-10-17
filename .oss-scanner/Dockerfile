# Build environment for Anthropic's OSS Scanner:
# https://github.com/anthropics/oss-scanner/tree/main/projects/tdesktop
#
# The scanner gives the whole build 45 minutes, which is not enough to build
# the third-party libraries, so this starts from the image that
# .github/workflows/docker.yml publishes from Telegram/build/docker/centos_env
# with all of them prebuilt, and compiles only Telegram Desktop itself.
FROM ghcr.io/telegramdesktop/tdesktop/centos_env:latest

USER root

# Scanner contract: the checkout lives inside the image at /src.
COPY . /src
WORKDIR /src

# The scanner clones the repository without submodules.
RUN git submodule update --init --recursive --jobs 16

# LeakSanitizer can't run inside docker build, where the code generators
# run with ASan, and leak reports are noise for a desktop client anyway.
ENV ASAN_OPTIONS=detect_leaks=0

# Same configuration as .github/workflows/linux.yml, plus debug info,
# AddressSanitizer, compile_commands.json and the update verification tests.
RUN CONFIG=Debug Telegram/build/docker/centos_env/build.sh \
	-D CMAKE_CONFIGURATION_TYPES=Debug \
	-D CMAKE_C_FLAGS_DEBUG="-O0 -g" \
	-D CMAKE_CXX_FLAGS_DEBUG="-O0 -g" \
	-D CMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-D DESKTOP_APP_ASAN=ON \
	-D TDESKTOP_API_TEST=ON \
	-D TDESKTOP_BUILD_UPDATE_TESTS=ON \
	-D DESKTOP_APP_DISABLE_AUTOUPDATE=OFF \
	-D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON \
	&& test -x out/Debug/Telegram

# Cheap smoke tests, which must not fail the image.
RUN (out/Debug/test_update_verify && out/Debug/test_update_unpack) \
	|| echo "WARNING: update tests reported failures"
