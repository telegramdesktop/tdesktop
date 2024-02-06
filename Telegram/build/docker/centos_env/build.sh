#!/bin/bash
set -e

cd Telegram
./configure.sh "$@"
cmake --build ../out --config "${CONFIG:-RelWithDebInfo}" --parallel
