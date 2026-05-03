#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
"""
Apply ONLY the "should remove" portion of IWYU output, in place.

Why a separate tool: `fix_includes.py` always canonicalises the include
block, even with `--noreorder`. It moves system headers to the top, groups
project headers, and re-emits the "full include-list" view. That is the
right thing for a full IWYU sweep but wrong for a *minimal* cleanup pass
where we only want to delete unused includes.

This applier reads IWYU output and, for every "should remove these lines:"
entry, deletes the matching `#include` line from the target file. Nothing
else is touched: no reordering, no additions, no canonicalisation.

Matches `- #include <X>` / `- #include "X"` exactly. Line numbers from the
IWYU comment (`// lines NN-NN`) are used as a sanity check only — the
match is by include text, so an unrelated reformat between IWYU run and
apply does not produce silently wrong edits.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

_REMOVE_HEADER_RE = re.compile(r"^(?P<path>.+?) should remove these lines:$")
_OTHER_HEADER_RE = re.compile(
    r"^.+? should add these lines:$|^The full include-list for .+:$|^---$"
)
_REMOVE_LINE_RE = re.compile(
    r"^- (?P<directive>#include\s+[<\"][^>\"]+[>\"])"
)


def parse_removes(iwyu_text: str) -> dict[str, list[str]]:
    """Return `{file_path: [include_directive, ...]}` for every removal block."""
    removes: dict[str, list[str]] = {}
    current: str | None = None
    for line in iwyu_text.splitlines():
        m = _REMOVE_HEADER_RE.match(line)
        if m:
            current = m.group("path").strip()
            removes.setdefault(current, [])
            continue
        if _OTHER_HEADER_RE.match(line):
            current = None
            continue
        if current is None:
            continue
        m = _REMOVE_LINE_RE.match(line)
        if m:
            removes[current].append(m.group("directive"))
    return {p: lines for p, lines in removes.items() if lines}


def apply_removes(file_path: Path, directives: list[str],
                  dry_run: bool) -> tuple[int, int, str | None]:
    """Delete every line whose stripped form equals one of `directives`.

    Returns `(matched, missing, original_text)`. `original_text` is the
    pre-edit content (None when nothing matched or in dry-run); callers
    that want to roll back can pass it back to `restore_file`.
    """
    if not file_path.exists():
        return 0, len(directives), None

    target = set(directives)
    original = file_path.read_text(encoding="utf-8")
    out_lines: list[str] = []
    matched = 0
    for raw in original.splitlines(keepends=True):
        stripped = raw.rstrip("\r\n")
        head = re.sub(r"\s*//.*$", "", stripped).rstrip()
        if head in target:
            matched += 1
            continue
        out_lines.append(raw)
    if matched and not dry_run:
        file_path.write_text("".join(out_lines), encoding="utf-8")
        return matched, len(target) - matched, original
    return matched, len(target) - matched, None


def restore_file(file_path: Path, original: str) -> None:
    file_path.write_text(original, encoding="utf-8")


def load_compile_db(path: Path) -> dict[str, list[str]]:
    """Build `{abs_path: argv_list}` from a compile_commands.json file."""
    raw = json.loads(path.read_text(encoding="utf-8"))
    db: dict[str, list[str]] = {}
    for entry in raw:
        f = entry.get("file")
        d = Path(entry.get("directory", "."))
        if not f:
            continue
        abs_path = (d / f).resolve()
        if "arguments" in entry and isinstance(entry["arguments"], list):
            argv = list(entry["arguments"])
        elif "command" in entry and isinstance(entry["command"], str):
            argv = shlex.split(entry["command"], posix=True)
        else:
            continue
        db[str(abs_path)] = argv
    return db


def _argv_for_syntax_check(argv: list[str]) -> list[str]:
    """Adapt a normal compile command to a `-fsyntax-only` invocation.

    Drops object/dependency outputs and `-c` so clang only parses; keeps
    every include path, define, PCH flag, sysroot and warning so the
    check is identical to the real build minus codegen.
    """
    out: list[str] = []
    skip_next = 0
    for tok in argv:
        if skip_next:
            skip_next -= 1
            continue
        if tok in ("-o", "-MF", "-MT", "-MQ"):
            skip_next = 1
            continue
        if tok in ("-c", "-MD", "-MMD"):
            continue
        out.append(tok)
    out.append("-fsyntax-only")
    return out


def syntax_check(argv: list[str], cwd: Path) -> tuple[int, str]:
    """Run the adapted compile command. Returns (returncode, stderr_text)."""
    cmd = _argv_for_syntax_check(argv)
    try:
        result = subprocess.run(
            cmd, cwd=cwd, capture_output=True, check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return 1, f"failed to spawn compiler: {exc}"
    # Compiler diagnostics can include non-UTF-8 (filenames in the macOS
    # SDK occasionally have Latin-1 bytes). Decode permissively.
    stderr = result.stderr.decode("utf-8", errors="replace") \
        if result.stderr else ""
    return result.returncode, stderr


def _verify_one(args: tuple) -> tuple[Path, bool, str]:
    """Worker for the verify pool.

    Receives `(path, argv, cwd)` and returns `(path, ok, stderr_excerpt)`.
    """
    path, argv, cwd = args
    rc, err = syntax_check(argv, cwd)
    return path, rc == 0, err


_NINJA_DEP_TARGET_RE = re.compile(r"^(?P<obj>.+?):\s+#deps\s+\d+")
_HEADER_SUFFIXES = (".h", ".hpp", ".hxx", ".hh")
_TU_SUFFIXES = (".cpp", ".cc", ".cxx", ".c", ".m", ".mm", ".swift")
_PCH_NAME_RE = re.compile(r"cmake_pch\.[a-z]+(?:\.[a-z]+)?$")


def build_header_dep_index(build_dir: Path,
                           tu_filter: set[str] | None = None,
                           ) -> dict[Path, set[Path]]:
    """Reverse-map every header to the translation units that include it.

    Source of truth is `ninja -t deps` from `build_dir`. When `tu_filter`
    is provided (a set of stringified absolute paths), only those paths
    are accepted as the "current TU" anchor — this is the safe way to
    ignore CMake's `cmake_pch.hxx.cxx` wrapper, which `ninja -t deps`
    lists *before* the real source file when PCH is enabled.
    """
    try:
        result = subprocess.run(
            ["ninja", "-t", "deps"],
            cwd=build_dir, capture_output=True, text=True, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return {}
    if result.returncode != 0:
        return {}

    index: dict[Path, set[Path]] = {}
    current_tu: Path | None = None
    for raw in result.stdout.splitlines():
        if not raw.strip():
            current_tu = None
            continue
        m = _NINJA_DEP_TARGET_RE.match(raw)
        if m:
            current_tu = None
            continue
        if not raw.startswith(("    ", "\t")):
            continue
        path_str = raw.strip()
        try:
            p = Path(path_str).resolve()
        except OSError:
            continue
        if current_tu is None and p.suffix.lower() in _TU_SUFFIXES:
            if _PCH_NAME_RE.search(p.name):
                continue
            if tu_filter is not None and str(p) not in tu_filter:
                continue
            current_tu = p
            continue
        if current_tu is None:
            continue
        if p.suffix.lower() in _HEADER_SUFFIXES:
            index.setdefault(p, set()).add(current_tu)
    return index


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--input", default="-",
                   help="IWYU output (default: stdin).")
    p.add_argument("--basedir", default=".", type=Path,
                   help="Resolve file paths in IWYU output relative to this "
                        "directory if they are not absolute (default: cwd).")
    p.add_argument("--only-under", default="",
                   help="Skip files whose path does not lie under this "
                        "directory (matched as a prefix). Empty = no filter.")
    p.add_argument("-n", "--dry-run", action="store_true",
                   help="Show what would change but do not write files.")
    p.add_argument("--skip-headers", action="store_true",
                   help="Drop every removal whose target file ends in .h, "
                        "since header edits propagate to all includers and "
                        "regularly break files that IWYU never analysed.")
    p.add_argument("--verify", type=Path, default=None,
                   help="Path to compile_commands.json. After applying, run "
                        "-fsyntax-only against each modified file using its "
                        "compile entry; on failure restore the file. "
                        "Replaces the iterative cmake-build/revert loop.")
    p.add_argument("--verify-jobs", type=int,
                   default=os.cpu_count() or 4,
                   help="Parallel workers for --verify (default: cpu count).")
    args = p.parse_args()

    text = sys.stdin.read() if args.input == "-" \
        else Path(args.input).read_text(encoding="utf-8", errors="replace")
    removes = parse_removes(text)

    only = args.only_under.replace("\\", "/").rstrip("/")
    if args.skip_headers:
        removes = {p: l for p, l in removes.items() if not p.endswith(".h")}

    # Resolve every (path, directives) to absolute paths and split by
    # source kind. We apply translation units first so .cpp verification
    # runs against pristine headers; headers are then applied one at a
    # time, each with its own dependent-cpp check, so a bad header does
    # not contaminate the verification of other headers.
    tu_targets: list[tuple[Path, list[str]]] = []
    hdr_targets: list[tuple[Path, list[str]]] = []
    skipped_scope = 0
    for path_str, directives in sorted(removes.items()):
        norm = path_str.replace("\\", "/")
        if only and (only not in norm):
            skipped_scope += 1
            continue
        path = Path(path_str)
        if not path.is_absolute():
            path = (args.basedir / path).resolve()
        if path.suffix.lower() in _HEADER_SUFFIXES:
            hdr_targets.append((path, directives))
        else:
            tu_targets.append((path, directives))

    tu_edits: list[tuple[Path, str]] = []
    tu_missing = 0
    tu_deleted = 0
    for path, directives in tu_targets:
        m, miss, original = apply_removes(path, directives, args.dry_run)
        tu_missing += miss
        if m:
            tu_deleted += m
            sys.stderr.write(f"  -{m:>3}  {path}\n")
            if original is not None:
                tu_edits.append((path, original))

    sys.stderr.write(
        f"\n{'(dry-run) ' if args.dry_run else ''}"
        f"phase tu: removed {tu_deleted} lines from {len(tu_edits)} "
        f"files; {tu_missing} directives no longer present.\n"
    )

    tu_stats = {"ok": len(tu_edits), "reverted": 0, "no_db_entry": 0}
    if args.verify and tu_edits and not args.dry_run:
        db = load_compile_db(args.verify)
        cwd = args.verify.parent
        originals = dict(tu_edits)
        ok, reverted, skipped = _verify_tu_edits(
            tu_edits, db, cwd, args.verify_jobs, originals,
        )
        tu_stats = {"ok": ok, "reverted": reverted, "no_db_entry": skipped}
        sys.stderr.write(
            f"  tu verify: {ok} kept, {reverted} reverted, "
            f"{skipped} skipped\n"
        )

    hdr_stats = {"ok": 0, "reverted": 0, "no_db_entry": 0}
    if hdr_targets and not args.dry_run:
        if args.verify:
            db = load_compile_db(args.verify)
            cwd = args.verify.parent
            hdr_stats = _process_headers_sequential(
                hdr_targets, db, cwd, args.verify_jobs, args.verify.parent,
            )
        else:
            # No verify requested — just apply (matches old behaviour).
            for path, directives in hdr_targets:
                m, miss, _ = apply_removes(path, directives, args.dry_run)
                if m:
                    sys.stderr.write(f"  -{m:>3}  {path}\n")

    sys.stderr.write(
        f"\nfinal: {tu_stats['ok']} TUs kept, "
        f"{tu_stats['reverted']} reverted; "
        f"{hdr_stats['ok']} headers kept, "
        f"{hdr_stats['reverted']} reverted.\n"
    )
    return 0


def _process_headers_sequential(
    targets: list[tuple[Path, list[str]]],
    db: dict[str, list[str]], cwd: Path, jobs: int, build_dir: Path,
) -> dict[str, int]:
    """Apply each header's removes in turn, syntax-check its dependents,
    keep or revert on the spot. Sequential so a kept header is the
    baseline for the next header's check."""
    sys.stderr.write(
        f"\nphase hdr: {len(targets)} headers to process — "
        f"building dep index from ninja\n"
    )
    index = build_header_dep_index(build_dir, tu_filter=set(db.keys()))
    if not index:
        sys.stderr.write(
            "  WARNING: no dep info available (run a build first); "
            "skipping header phase\n"
        )
        return {"ok": 0, "reverted": 0, "no_db_entry": len(targets)}

    ok = 0
    reverted = 0
    skipped = 0
    for path, directives in targets:
        m, _miss, original = apply_removes(path, directives, dry_run=False)
        if not m or original is None:
            continue
        deps = index.get(path.resolve(), set())
        tasks: list[tuple] = []
        for cpp in deps:
            argv = db.get(str(cpp))
            if argv is not None:
                tasks.append((cpp, argv, cwd))
        if not tasks:
            sys.stderr.write(f"  KEEP   {path}  (no compilable deps)\n")
            ok += 1
            continue

        any_failed = False
        with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
            for _done, success, _err in pool.map(_verify_one, tasks):
                if not success:
                    any_failed = True
                    break
        if any_failed:
            sys.stderr.write(
                f"  REVERT {path}  ({len(tasks)} dep(s), at least 1 failed)\n"
            )
            restore_file(path, original)
            reverted += 1
        else:
            sys.stderr.write(
                f"  KEEP   {path}  ({len(tasks)} dep(s) clean)\n"
            )
            ok += 1
    return {"ok": ok, "reverted": reverted, "no_db_entry": skipped}


def _verify_tu_edits(edits: list[tuple[Path, str]],
                     db: dict[str, list[str]], cwd: Path, jobs: int,
                     originals: dict[Path, str]) -> tuple[int, int, int]:
    if not edits:
        return 0, 0, 0
    tasks: list[tuple] = []
    skipped: set[Path] = set()
    for path, _ in edits:
        argv = db.get(str(path))
        if argv is None:
            skipped.add(path)
            continue
        tasks.append((path, argv, cwd))

    ok_paths: set[Path] = set()
    failed: list[tuple[Path, str]] = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        for done_path, ok, stderr in pool.map(_verify_one, tasks):
            if ok:
                ok_paths.add(done_path)
            else:
                failed.append((done_path, stderr))

    for path, stderr in failed:
        sys.stderr.write(f"  REVERT {path}\n")
        restore_file(path, originals[path])
    return len(ok_paths), len(failed), len(skipped)




if __name__ == "__main__":
    raise SystemExit(main())
