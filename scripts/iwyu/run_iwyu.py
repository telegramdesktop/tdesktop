#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
"""Drive the IWYU cleanup pipeline: configure, build, analyze, filter,
apply, optionally commit. See scripts/iwyu/README.md for details."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

THIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(THIS_DIR))
import iwyu_filter

DEFAULT_BUILD_DIR = "out/iwyu"
DEFAULT_TARGET = "Telegram"
DEFAULT_PROJECT_SUBDIR = "Telegram/SourceFiles"
DEFAULT_RAW_OUTPUT = "iwyu.out"
DEFAULT_FILTERED_OUTPUT = "iwyu.filtered.out"


def _print_step(title: str) -> None:
    bar = "=" * max(8, len(title) + 4)
    sys.stderr.write(f"\n{bar}\n  {title}\n{bar}\n")


def _run(cmd: list[str], *, cwd: Path | None = None,
         capture_stdout: Path | None = None,
         env: dict[str, str] | None = None,
         check: bool = True) -> int:
    pretty = " ".join(_quote(c) for c in cmd)
    sys.stderr.write(f"$ {pretty}\n")
    if capture_stdout is None:
        result = subprocess.run(cmd, cwd=cwd, env=env, check=False)
    else:
        capture_stdout.parent.mkdir(parents=True, exist_ok=True)
        with capture_stdout.open("wb") as fh:
            result = subprocess.run(cmd, cwd=cwd, env=env, stdout=fh, check=False)
    if check and result.returncode != 0:
        sys.exit(f"Command failed (exit {result.returncode}): {pretty}")
    return result.returncode


def _quote(s: str) -> str:
    if any(c.isspace() for c in s) or any(c in s for c in '"\'\\'):
        return '"' + s.replace('"', '\\"') + '"'
    return s


def _which_tool(*names: str, env_var: str | None = None) -> list[str]:
    """Find a CLI tool, returning argv ready for subprocess. On Windows
    .py files have no shebang and can't be CreateProcess'd directly,
    so prepend the current interpreter for those."""
    def _argv(path: str) -> list[str]:
        return [sys.executable, path] if path.endswith(".py") else [path]

    if env_var and os.environ.get(env_var):
        return _argv(os.environ[env_var])
    for name in names:
        found = shutil.which(name)
        if found:
            return _argv(found)
    sys.exit(
        f"Could not find any of {list(names)} on PATH"
        + (f" (override with ${env_var})" if env_var else "")
        + ". Install include-what-you-use."
    )


def detect_current_platform() -> str | None:
    return {"Darwin": "mac", "Linux": "linux", "Windows": "win"}.get(platform.system())


def configure(build_dir: Path, repo_root: Path, extra_cmake: list[str]) -> None:
    _print_step("configure")
    cmd = [
        "cmake",
        "-S", str(repo_root),
        "-B", str(build_dir),
        "-G", "Ninja",
        "-DDESKTOP_APP_USE_IWYU=ON",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_BUILD_TYPE=Debug",
    ]
    cmd.extend(extra_cmake)
    _run(cmd)


def build(build_dir: Path, target: str, jobs: int) -> None:
    _print_step(f"build target {target}")
    cmd = ["cmake", "--build", str(build_dir), "--target", target,
           "--config", "Debug", "--parallel", str(jobs), "--", "-k", "0"]
    _run(cmd, check=False)


def list_in_scope_files(build_dir: Path, repo_root: Path,
                       project_subdir: str) -> list[str]:
    cc = build_dir / "compile_commands.json"
    if not cc.exists():
        sys.exit(f"{cc} not found — run the configure stage first.")
    with cc.open("r", encoding="utf-8") as fh:
        entries = json.load(fh)

    project_abs = (repo_root / project_subdir).resolve()
    files: list[str] = []
    seen: set[str] = set()
    for entry in entries:
        f = entry.get("file")
        if not f:
            continue
        try:
            f_abs = (Path(entry.get("directory", ".")) / f).resolve()
        except OSError:
            continue
        try:
            f_abs.relative_to(project_abs)
        except ValueError:
            continue
        s = str(f_abs)
        if s in seen:
            continue
        seen.add(s)
        files.append(s)
    return sorted(files)


def _read_cmake_compiler(build_dir: Path) -> str:
    cache = build_dir / "CMakeCache.txt"
    if cache.exists():
        for raw in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if raw.startswith("CMAKE_CXX_COMPILER:") and "=" in raw:
                val = raw.split("=", 1)[1].strip()
                if val:
                    return val
    return "clang"


def _detect_resource_dir(build_dir: Path) -> str | None:
    compiler = _read_cmake_compiler(build_dir)
    try:
        result = subprocess.run(
            [compiler, "-print-resource-dir"],
            capture_output=True, text=True, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    out = result.stdout.strip()
    return out or None


def _detect_macos_sysroot() -> str | None:
    if platform.system() != "Darwin":
        return None
    try:
        result = subprocess.run(
            ["xcrun", "--show-sdk-path"],
            capture_output=True, text=True, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    out = result.stdout.strip()
    return out or None


def _detect_compiler_system_includes(build_dir: Path) -> list[str]:
    """Pin IWYU to the same stdlib gcc sees. Without this, IWYU's clang
    falls back to system /usr/include/c++/<old>, missing C++20 headers
    when the build uses a newer gcc-toolset. macOS uses -isysroot;
    Windows clang-cl picks up MSVC headers from $INCLUDE."""
    if platform.system() != "Linux":
        return []
    compiler = _read_cmake_compiler(build_dir)
    try:
        result = subprocess.run(
            [compiler, "-E", "-v", "-xc++", "-"],
            capture_output=True, text=True, check=False,
            stdin=subprocess.DEVNULL,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    text = (result.stderr or "") + (result.stdout or "")
    paths: list[str] = []
    in_block = False
    for line in text.splitlines():
        s = line.strip()
        if s == "#include <...> search starts here:":
            in_block = True
            continue
        if s.startswith("End of search list"):
            break
        if in_block and s:
            paths.append(s.split(" (")[0])
    return paths


# `-fhardened` is a fatal "unknown argument" for clang; warning flags
# below are gcc-only noise.
_GCC_ONLY_FLAGS_EXACT = {
    "-fhardened",
}
_GCC_ONLY_FLAG_PREFIXES = (
    "-Wno-hardened",
    "-Wno-maybe-uninitialized",
    "-Wno-class-memaccess",
    "-Wno-stringop-",
    "-Wno-format-overflow",
    "-Wno-format-truncation",
    "-Wno-restrict",
)


_MSVC_PCH_FLAG_PREFIXES = (
    "/Yu", "/Yc", "/Fp",
    "-Yu", "-Yc", "-Fp",
)

# clang-cl is stricter than cl.exe: -Wc++11-narrowing fires on signed→
# unsigned switch cases that scheme.h/mtproto generators emit, and the
# lambda-capture rule rejects accesses cl.exe lets through. Demote both
# to non-fatal so IWYU can finish parsing the affected TUs.
_CLANG_CL_LENIENT_FLAGS = (
    "-Wno-c++11-narrowing",
    "-Wno-microsoft-exception-spec",
    "-Wno-deprecated-anon-enum-enum-conversion",
    "-Wno-error",
)


def _strip_pch_from_command(tokens: list[str]) -> list[str]:
    """Drop loaded-PCH directives (IWYU rejects PCH binaries) and any
    compiler-only flags clang cannot parse. Text-include forms
    (`-Xclang -include <header>`, `/FI<header>`) are kept so
    PCH-provided declarations stay visible."""
    out: list[str] = []
    i = 0
    while i < len(tokens):
        t = tokens[i]
        if (t == "-Xclang"
                and i + 3 < len(tokens)
                and tokens[i + 1] == "-include-pch"
                and tokens[i + 2] == "-Xclang"):
            i += 4
            continue
        if t == "-include-pch" and i + 1 < len(tokens):
            i += 2
            continue
        if any(t.startswith(p) for p in _MSVC_PCH_FLAG_PREFIXES):
            i += 1
            continue
        if t in _GCC_ONLY_FLAGS_EXACT \
                or any(t.startswith(p) for p in _GCC_ONLY_FLAG_PREFIXES):
            i += 1
            continue
        out.append(t)
        i += 1
    return out


def _maybe_swap_msvc_to_clang_cl(tokens: list[str]) -> list[str]:
    """If the entry's compiler driver is cl.exe, swap to clang-cl so
    IWYU's clang frontend can parse the MSVC-style flags. Lets the
    actual build run under cl.exe (which Telegram supports) while IWYU
    sees a clang-driveable invocation."""
    if not tokens:
        return tokens
    # Split manually so this works whether the host runs Windows or not.
    name = tokens[0].replace("\\", "/").rsplit("/", 1)[-1].lower()
    if name not in ("cl.exe", "cl"):
        return tokens
    clang_cl = shutil.which("clang-cl") or shutil.which("clang-cl.exe")
    if not clang_cl:
        return tokens
    out = [clang_cl] + tokens[1:]
    out.extend(_CLANG_CL_LENIENT_FLAGS)
    return out


def _strip_outer_quotes(tokens: list[str]) -> list[str]:
    """Non-POSIX shlex keeps surrounding quotes on each token; remove
    them so `Path(t).name` and downstream prefix matches behave."""
    out: list[str] = []
    for t in tokens:
        if len(t) >= 2 and t[0] == t[-1] and t[0] in ('"', "'"):
            out.append(t[1:-1])
        else:
            out.append(t)
    return out


def _make_iwyu_compile_db(build_dir: Path,
                          in_scope: set[str] | None = None) -> Path:
    src = build_dir / "compile_commands.json"
    if not src.exists():
        sys.exit(f"{src} not found — run the configure stage first.")
    dst_dir = build_dir / "iwyu_db"
    dst_dir.mkdir(parents=True, exist_ok=True)
    with src.open("r", encoding="utf-8") as fh:
        entries = json.load(fh)

    import shlex
    use_posix = platform.system() != "Windows"
    fixed: list[dict] = []
    for entry in entries:
        e = dict(entry)
        if in_scope is not None:
            f = e.get("file")
            d = Path(e.get("directory", "."))
            try:
                f_abs = (d / f).resolve() if f else None
            except OSError:
                f_abs = None
            if f_abs is None or str(f_abs) not in in_scope:
                continue
        tokens: list[str] | None = None
        if "arguments" in e and isinstance(e["arguments"], list):
            tokens = list(e["arguments"])
        elif "command" in e and isinstance(e["command"], str):
            # POSIX shlex treats backslashes as escape, mangling Windows
            # paths; non-POSIX mode keeps them verbatim. Always emit
            # `arguments` so we never round-trip through shell quoting.
            tokens = shlex.split(e["command"], posix=use_posix)
            if not use_posix:
                tokens = _strip_outer_quotes(tokens)
        if tokens is None:
            fixed.append(e)
            continue
        tokens = _strip_pch_from_command(tokens)
        tokens = _maybe_swap_msvc_to_clang_cl(tokens)
        e.pop("command", None)
        e["arguments"] = tokens
        fixed.append(e)
    (dst_dir / "compile_commands.json").write_text(
        json.dumps(fixed, indent=2), encoding="utf-8"
    )
    return dst_dir


def analyze(build_dir: Path, files: list[str], output_file: Path,
            jobs: int, mapping_file: Path | None) -> None:
    _print_step("analyze (iwyu_tool.py)")
    if not files:
        sys.exit("No source files in scope — nothing to analyze.")
    iwyu_tool = _which_tool("iwyu_tool.py", "iwyu_tool", env_var="IWYU_TOOL")
    # Filter the IWYU compile DB to in-scope files instead of passing
    # them on argv: 1100+ absolute paths blow past Windows' ~32K
    # CreateProcess command-line limit.
    db_dir = _make_iwyu_compile_db(build_dir, in_scope=set(files))
    cmd = list(iwyu_tool) + ["-p", str(db_dir), "-j", str(jobs), "-o", "iwyu"]
    cmd.append("--")
    cmd += ["-Xiwyu", "--no_fwd_decls"]
    cmd += ["-Xiwyu", "--cxx17ns"]
    cmd += ["-Xiwyu", "--quoted_includes_first"]
    cmd += ["-Xiwyu", "--max_line_length=120"]
    if mapping_file and mapping_file.exists():
        cmd += ["-Xiwyu", f"--mapping_file={mapping_file.resolve()}"]
    resource_dir = _detect_resource_dir(build_dir)
    if resource_dir:
        cmd.append(f"-resource-dir={resource_dir}")
    macos_sdk = _detect_macos_sysroot()
    if macos_sdk:
        cmd += ["-isysroot", macos_sdk]
    for inc in _detect_compiler_system_includes(build_dir):
        cmd += ["-isystem", inc]
    _run(cmd, capture_stdout=output_file, check=False)
    sys.stderr.write(f"Wrote raw IWYU output to {output_file}\n")


def filter_output(repo_root: Path, raw: Path, filtered: Path,
                  skip_platforms: list[str], only_under: str,
                  extra_ignores: list[str], drop_adds: bool) -> None:
    _print_step("filter")
    f = iwyu_filter.IwyuFilter(
        repo_root=repo_root,
        skip_platforms=skip_platforms,
        only_under=only_under,
        extra_ignore_globs=extra_ignores,
    )
    text = raw.read_text(encoding="utf-8")
    text = iwyu_filter.rewrite_generated_paths(text)
    text = iwyu_filter.rewrite_qt_long_form(text)
    out, counts = iwyu_filter.filter_iwyu_output(
        text, f, drop_adds=drop_adds)
    filtered.write_text(out, encoding="utf-8")
    if counts:
        sys.stderr.write("Filtered out:\n")
        for reason, n in sorted(counts.items()):
            sys.stderr.write(f"  {n:5d}  {reason}\n")
    sys.stderr.write(f"Wrote filtered output to {filtered}\n")


def apply_fixes(repo_root: Path, filtered: Path, only_under: str,
                dry_run: bool, removes_only: bool,
                verify_db: Path | None, verify_jobs: int,
                skip_headers: bool) -> int:
    if removes_only:
        _print_step("apply (iwyu_remove_only.py)")
        cmd = [sys.executable, str(THIS_DIR / "iwyu_remove_only.py"),
               "--input", str(filtered),
               "--basedir", str(repo_root)]
        if only_under:
            cmd += ["--only-under", only_under]
        if skip_headers:
            cmd.append("--skip-headers")
        if verify_db:
            cmd += ["--verify", str(verify_db),
                    "--verify-jobs", str(verify_jobs)]
        if dry_run:
            cmd.append("-n")
        sys.stderr.write(f"$ {' '.join(_quote(c) for c in cmd)}\n")
        return subprocess.call(cmd)

    _print_step("apply (fix_includes.py)")
    fix = _which_tool("fix_includes.py", "iwyu-fix-includes",
                      env_var="IWYU_FIX_INCLUDES")
    cmd = list(fix)
    if dry_run:
        cmd.append("-n")
    cmd.append(f"--basedir={repo_root}")
    if only_under:
        only_re = only_under.replace("\\", "/").rstrip("/") + "/"
        cmd.append(f"--only_re={only_re}")
    with filtered.open("rb") as fh:
        sys.stderr.write(f"$ {' '.join(_quote(c) for c in cmd)} < {filtered}\n")
        result = subprocess.run(cmd, stdin=fh, check=False)
    return result.returncode


def maybe_commit(repo_root: Path, branch: str | None, push: bool) -> int:
    _print_step("commit")
    cmd = [
        sys.executable,
        str(THIS_DIR / "iwyu_commit.py"),
        "--repo-root", str(repo_root),
    ]
    if branch:
        cmd += ["--branch", branch]
    if push:
        cmd += ["--push"]
    return subprocess.call(cmd)


def _split_argv_at_double_dash(argv: list[str]) -> tuple[list[str], list[str]]:
    if "--" not in argv:
        return argv, []
    idx = argv.index("--")
    return argv[:idx], argv[idx + 1:]


def main() -> int:
    argv, cmake_passthrough = _split_argv_at_double_dash(sys.argv[1:])
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=(
                                    "Example:\n"
                                    "  run_iwyu.py --skip-platform=auto -- "
                                    "-DTDESKTOP_API_TEST=ON -DDESKTOP_APP_DISABLE_AUTOUPDATE=OFF"
                                ))
    p.add_argument("--repo-root", default=".", type=Path)
    p.add_argument("--build-dir", default=DEFAULT_BUILD_DIR, type=Path)
    p.add_argument("--target", default=DEFAULT_TARGET)
    p.add_argument("--project-subdir", default=DEFAULT_PROJECT_SUBDIR,
                   help="Limit IWYU runs and rewrites to this repo-relative path.")
    p.add_argument("--cmake-arg", action="append", default=[],
                   help="Extra arg passed to cmake configure. May be "
                        "repeated, or list every flag after a literal '--'.")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    p.add_argument("--raw-output", default=DEFAULT_RAW_OUTPUT, type=Path)
    p.add_argument("--filtered-output", default=DEFAULT_FILTERED_OUTPUT, type=Path)
    p.add_argument("--mapping-file",
                   default=THIS_DIR / "iwyu.imp", type=Path,
                   help="Project IWYU mapping file. Pass an empty string to disable.")

    p.add_argument("--skip-platform", action="append", default=[],
                   choices=["auto", "win", "mac", "linux", "posix"],
                   help="Drop platform-specific files from results. 'auto' "
                        "picks the host platform.")
    p.add_argument("--ignore", action="append", default=[],
                   help="Extra glob (relative to repo root) to drop. May be repeated.")
    p.add_argument("--no-adds", action="store_true",
                   help="Cleanup-only: only deletions, no transitive includes added.")
    p.add_argument("--skip-headers", action="store_true",
                   help="With --no-adds, drop every .h removal block.")
    p.add_argument("--verify", action="store_true",
                   help="With --no-adds, syntax-check each edited file and "
                        "revert on failure (replaces iterative build/revert).")

    p.add_argument("--skip-configure", action="store_true")
    p.add_argument("--skip-build", action="store_true")
    p.add_argument("--skip-analyze", action="store_true",
                   help="Reuse an existing raw IWYU output file.")
    p.add_argument("--skip-filter", action="store_true")
    p.add_argument("--skip-apply", action="store_true",
                   help="Stop before fix_includes.py runs (analysis only).")

    p.add_argument("--dry-run", action="store_true",
                   help="Pass -n to fix_includes.py — files are not modified.")

    p.add_argument("--commit", action="store_true",
                   help="After applying, run iwyu_commit.py to make a branch+commit.")
    p.add_argument("--push", action="store_true",
                   help="With --commit, also push the branch.")
    p.add_argument("--branch", default=None,
                   help="Override the auto-named branch passed to iwyu_commit.py.")

    args = p.parse_args(argv)
    extra_cmake = list(args.cmake_arg) + cmake_passthrough

    repo_root = args.repo_root.resolve()
    build_dir = (repo_root / args.build_dir).resolve() \
        if not args.build_dir.is_absolute() else args.build_dir
    raw_path = (repo_root / args.raw_output).resolve() \
        if not args.raw_output.is_absolute() else args.raw_output
    filtered_path = (repo_root / args.filtered_output).resolve() \
        if not args.filtered_output.is_absolute() else args.filtered_output

    skip_platforms: list[str] = []
    for s in args.skip_platform:
        if s == "auto":
            current = detect_current_platform()
            if current and current not in skip_platforms:
                skip_platforms.append(current)
        elif s not in skip_platforms:
            skip_platforms.append(s)

    if not args.skip_configure:
        configure(build_dir, repo_root, extra_cmake)
    if not args.skip_build:
        build(build_dir, args.target, args.jobs)
    if not args.skip_analyze:
        files = list_in_scope_files(build_dir, repo_root, args.project_subdir)
        sys.stderr.write(f"Analyzing {len(files)} files under {args.project_subdir}.\n")
        analyze(build_dir, files, raw_path, args.jobs,
                args.mapping_file if str(args.mapping_file) else None)
    if not args.skip_filter:
        filter_output(repo_root, raw_path, filtered_path,
                      skip_platforms, args.project_subdir, args.ignore,
                      drop_adds=args.no_adds)
    if args.skip_apply:
        sys.stderr.write("Stopping before fix_includes.py per --skip-apply.\n")
        return 0

    verify_db = (build_dir / "compile_commands.json") if args.verify else None
    rc = apply_fixes(repo_root, filtered_path, args.project_subdir,
                     args.dry_run, removes_only=args.no_adds,
                     verify_db=verify_db, verify_jobs=args.jobs,
                     skip_headers=args.skip_headers)
    if rc != 0:
        sys.stderr.write(f"fix_includes.py exited with {rc}.\n")

    if args.commit and not args.dry_run:
        rc = maybe_commit(repo_root, args.branch, args.push)
        return rc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
