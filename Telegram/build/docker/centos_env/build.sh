#!/bin/bash

cd Telegram
scl enable devtoolset-8 -- ./configure.sh "$@"

if [ -n "$DEBUG" ]; then
	scl enable devtoolset-8 -- cmake3 --build ../out/Debug -j$(nproc)
else
	scl enable devtoolset-8 -- cmake3 --build ../out/Release -j$(nproc)
fi
