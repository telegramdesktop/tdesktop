#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
"""Intersect IWYU raw outputs from multiple platforms. For each source
file, `- #include` removes are kept only if every platform that
analysed that file proposed dropping them; files seen on a single
platform pass through unchanged."""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

_REMOVE_HEADER_RE = re.compile(r"^(?P<path>.+?) should remove these lines:$")
_ADD_HEADER_RE = re.compile(r"^(?P<path>.+?) should add these lines:$")
_FULL_LIST_RE = re.compile(r"^The full include-list for (?P<path>.+):$")
_CLEAN_RE = re.compile(r"^\((?P<path>.+?) has correct #includes/fwd-decls\)$")
_REMOVE_LINE_RE = re.compile(r"^- #include\s+[<\"][^>\"]+[>\"]")


def _normalize_path(path: str) -> str:
    """Map an absolute build-host path to repo-relative POSIX form so
    Linux/macOS/Windows runs converge on the same key per file."""
    s = path.replace("\\", "/").strip()
    for marker in ("/Telegram/SourceFiles/", "/Telegram/lib_"):
        idx = s.rfind(marker)
        if idx != -1:
            return "Telegram/" + s[idx + len("/Telegram/"):]
    return s


def _parse_blocks(text: str) -> list[list[str]]:
    blocks: list[list[str]] = []
    current: list[str] = []
    for line in text.splitlines():
        current.append(line)
        if line.strip() == "---":
            blocks.append(current)
            current = []
    if current:
        blocks.append(current)
    return blocks


def _block_path(block: list[str]) -> str | None:
    for line in block:
        for rx in (_FULL_LIST_RE, _REMOVE_HEADER_RE, _ADD_HEADER_RE, _CLEAN_RE):
            m = rx.match(line)
            if m:
                return m.group("path").strip()
    return None


def _block_removes(block: list[str]) -> set[str]:
    out: set[str] = set()
    in_remove = False
    for raw in block:
        line = raw.rstrip()
        if line.endswith(" should remove these lines:"):
            in_remove = True
            continue
        if (line.endswith(" should add these lines:")
                or line.startswith("The full include-list for ")
                or line == "---"):
            in_remove = False
            continue
        if in_remove and _REMOVE_LINE_RE.match(line):
            out.add(line)
    return out


def _rewrite_block_paths(block: list[str], orig: str, rel: str) -> list[str]:
    return [line.replace(orig, rel) if orig in line else line
            for line in block]


def _substitute_removes(block: list[str], allowed: set[str]) -> list[str]:
    out: list[str] = []
    in_remove = False
    for raw in block:
        line = raw.rstrip()
        if line.endswith(" should remove these lines:"):
            in_remove = True
            out.append(raw)
            continue
        if (line.endswith(" should add these lines:")
                or line.startswith("The full include-list for ")
                or line == "---"):
            in_remove = False
            out.append(raw)
            continue
        if in_remove and _REMOVE_LINE_RE.match(line):
            if line in allowed:
                out.append(raw)
            continue
        out.append(raw)
    return out


def intersect(per_platform: dict[str, str]) -> str:
    """Combine per-platform raw IWYU outputs into a single synthetic raw
    where each file's removes are intersected across the platforms that
    analysed it. The first platform's block (alphabetical by label)
    serves as the template; only its remove-section content is filtered."""
    file_blocks: dict[str, dict[str, tuple[list[str], str]]] = defaultdict(dict)
    for label, text in per_platform.items():
        for block in _parse_blocks(text):
            path = _block_path(block)
            if path is None:
                continue
            file_blocks[_normalize_path(path)][label] = (block, path)

    out: list[str] = []
    for rel, by_label in sorted(file_blocks.items()):
        labels = sorted(by_label.keys())
        template, orig = by_label[labels[0]]
        if len(labels) == 1:
            out.extend(_rewrite_block_paths(template, orig, rel))
            continue
        common = set.intersection(
            *(_block_removes(by_label[l][0]) for l in labels)
        )
        substituted = _substitute_removes(template, common)
        out.extend(_rewrite_block_paths(substituted, orig, rel))
    return "\n".join(out) + ("\n" if out else "")


def _label_from_path(path: Path) -> str:
    name = path.stem
    for prefix in ("iwyu_raw_", "iwyu.raw.", "iwyu."):
        if name.startswith(prefix):
            return name[len(prefix):]
    return name


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("inputs", nargs="+", type=Path,
                   help="Raw IWYU output files, one per platform.")
    p.add_argument("--output", "-o", default="-",
                   help="Where to write the merged raw output (default: stdout).")
    args = p.parse_args()

    if len(args.inputs) < 2:
        sys.stderr.write(
            "iwyu_intersect.py: at least two inputs required for "
            "intersection — got 1; pass per-platform raws.\n"
        )
        return 2

    per_platform: dict[str, str] = {}
    for path in args.inputs:
        if not path.exists():
            sys.exit(f"input not found: {path}")
        label = _label_from_path(path)
        per_platform[label] = path.read_text(encoding="utf-8", errors="replace")
        sys.stderr.write(f"  loaded {label:<10}  {path}\n")

    merged = intersect(per_platform)
    if args.output == "-":
        sys.stdout.write(merged)
    else:
        Path(args.output).write_text(merged, encoding="utf-8")
        sys.stderr.write(f"Wrote merged raw to {args.output}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
