#!/usr/bin/env bash
# Strips the binary, preserving function names in a MiniDebugInfo
# (.gnu_debugdata) section readable by gdb/elfutils/perf.
set -euo pipefail

bin="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

nm -D "$bin" --format=posix --defined-only | awk '{print $1}' | sort -u > "$tmp/dynsyms"
nm    "$bin" --format=posix --defined-only | awk '$2 ~ /[tTwW]/ {print $1}' | sort -u > "$tmp/funcsyms"
comm -13 "$tmp/dynsyms" "$tmp/funcsyms" > "$tmp/keep"

objcopy --only-keep-debug "$bin" "$tmp/mini"
objcopy -S --remove-section .comment --keep-symbols="$tmp/keep" "$tmp/mini"
xz -9 "$tmp/mini"

strip -s "$bin"
objcopy --add-section .gnu_debugdata="$tmp/mini.xz" "$bin"
