#!/usr/bin/scl enable llvm-toolset-7.0 -- scl enable devtoolset-9 -- bash

cd Telegram
./configure.sh "$@"

if [ -n "$DEBUG" ]; then
	cmake3 --build ../out/Debug -j$(nproc)
else
	cmake3 --build ../out/Release -j$(nproc)
fi
