#!/usr/bin/env bash
# Strips the binary, preserving function names in a MiniDebugInfo
# (.gnu_debugdata) section readable by gdb/elfutils/perf.
set -euo pipefail

bin="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

nm -p -D "$bin" --format=posix --defined-only | awk '{print $1}' > "$tmp/dynsyms"
nm -p    "$bin" --format=posix --defined-only | awk '$2 ~ /[tTwW]/ {print $1}' > "$tmp/funcsyms"
awk 'FILENAME == ARGV[1] { dyn[$0]; next } !($0 in dyn)' "$tmp/dynsyms" "$tmp/funcsyms" > "$tmp/keep"

objcopy --only-keep-debug "$bin" "$tmp/mini"
objcopy -S --remove-section .comment --keep-symbols="$tmp/keep" "$tmp/mini"
xz -9 "$tmp/mini"

strip -s "$bin"
objcopy --add-section .gnu_debugdata="$tmp/mini.xz" "$bin"
