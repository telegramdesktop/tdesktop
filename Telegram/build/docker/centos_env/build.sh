#!/usr/bin/scl enable llvm-toolset-7.0 -- scl enable devtoolset-10 -- bash

cd Telegram
./configure.sh "$@"

if [ -n "$DEBUG" ]; then
	cmake --build ../out --config Debug --parallel
else
	cmake --build ../out --config Release --parallel
fi
