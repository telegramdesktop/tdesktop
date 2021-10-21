#!/usr/bin/scl enable llvm-toolset-7.0 -- scl enable devtoolset-9 -- bash

cd Telegram
./configure.sh "$@"

if [ -n "$DEBUG" ]; then
	cmake --build ../out/Debug -j$(nproc)
else
	cmake --build ../out/Release -j$(nproc)
fi
