#!/bin/bash

cd Telegram
./configure.sh "$@"

if [ -n "$DEBUG" ]; then
	cmake --build ../out --config Debug --parallel
else
	cmake --build ../out --config Release --parallel
fi
