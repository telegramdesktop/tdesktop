#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
"""
Summarize an IWYU run, so the operator can decide what to tune.

Reads raw IWYU output and prints:
  * total file blocks vs. clean files
  * top-N "should add" headers by frequency
  * top-N "should remove" headers by frequency
  * top-N files with the most suggestions

Tuning is driven by add-frequency: an `<qstring.h>` showing up in 200 files
is exactly one missing entry in iwyu.imp, fixing all 200 in one go.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

_ADD_RE = re.compile(r"^(?P<path>.+?) should add these lines:$")
_REMOVE_HEADER_RE = re.compile(r"^(?P<path>.+?) should remove these lines:$")
_CLEAN_RE = re.compile(r"^\((?P<path>.+?) has correct #includes/fwd-decls\)$")
_INCLUDE_RE = re.compile(r"^#include (?P<header>[<\"][^>\"]+[>\"])")
_REMOVE_RE = re.compile(r"^- #include (?P<header>[<\"][^>\"]+[>\"])")


def parse(text: str) -> dict:
    state: str | None = None  # "add" | "remove" | None
    current_path: str | None = None
    add_counts: Counter[str] = Counter()
    remove_counts: Counter[str] = Counter()
    per_file_add: Counter[str] = Counter()
    per_file_remove: Counter[str] = Counter()
    clean_files = 0
    files_with_changes: set[str] = set()
    files_seen: set[str] = set()

    for line in text.splitlines():
        if line.startswith("---"):
            state = None
            current_path = None
            continue
        m = _ADD_RE.match(line)
        if m:
            current_path = m.group("path").strip()
            files_seen.add(current_path)
            state = "add"
            continue
        m = _REMOVE_HEADER_RE.match(line)
        if m:
            current_path = m.group("path").strip()
            files_seen.add(current_path)
            state = "remove"
            continue
        m = _CLEAN_RE.match(line)
        if m:
            files_seen.add(m.group("path").strip())
            clean_files += 1
            state = None
            current_path = None
            continue
        if state == "add" and current_path:
            mi = _INCLUDE_RE.match(line)
            if mi:
                add_counts[mi.group("header")] += 1
                per_file_add[current_path] += 1
                files_with_changes.add(current_path)
        elif state == "remove" and current_path:
            mr = _REMOVE_RE.match(line)
            if mr:
                remove_counts[mr.group("header")] += 1
                per_file_remove[current_path] += 1
                files_with_changes.add(current_path)

    return {
        "files_seen": len(files_seen),
        "clean_files": clean_files,
        "dirty_files": len(files_with_changes),
        "total_adds": sum(add_counts.values()),
        "total_removes": sum(remove_counts.values()),
        "add_counts": add_counts,
        "remove_counts": remove_counts,
        "per_file_total": Counter(
            {f: per_file_add.get(f, 0) + per_file_remove.get(f, 0)
             for f in files_with_changes}
        ),
    }


def render(stats: dict, top: int) -> str:
    out: list[str] = []
    out.append("=== Summary ===")
    out.append(f"  files seen           : {stats['files_seen']}")
    out.append(f"  clean (no changes)   : {stats['clean_files']}")
    out.append(f"  with suggestions     : {stats['dirty_files']}")
    out.append(f"  total 'add'    lines : {stats['total_adds']}")
    out.append(f"  total 'remove' lines : {stats['total_removes']}")

    out.append("")
    out.append(f"=== Top {top} 'should add' includes ===")
    for header, count in stats["add_counts"].most_common(top):
        out.append(f"  {count:5d}  {header}")

    out.append("")
    out.append(f"=== Top {top} 'should remove' includes ===")
    for header, count in stats["remove_counts"].most_common(top):
        out.append(f"  {count:5d}  {header}")

    out.append("")
    out.append(f"=== Files with most suggestions (top {top}) ===")
    for path, count in stats["per_file_total"].most_common(top):
        short = path.split("/Telegram/SourceFiles/")[-1]
        out.append(f"  {count:5d}  {short}")

    return "\n".join(out) + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--input", default="-",
                   help="IWYU raw output to summarize (default: stdin).")
    p.add_argument("--top", type=int, default=30,
                   help="How many entries per ranking (default: 30).")
    args = p.parse_args()

    text = sys.stdin.read() if args.input == "-" \
        else Path(args.input).read_text(encoding="utf-8", errors="replace")
    sys.stdout.write(render(parse(text), args.top))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
