# include-what-you-use infrastructure

Optional integration that wires up [`include-what-you-use`][iwyu] (IWYU)
for Telegram Desktop and provides Python automation that builds the
project, removes unused includes, and produces a commit attributed to
`github-actions[bot]`.

[iwyu]: https://include-what-you-use.org

## Layout

| Path | Purpose |
|------|---------|
| `Telegram/cmake/iwyu.cmake`            | CMake module: declares `DESKTOP_APP_USE_IWYU`, finds the binary, exposes `desktop_app_apply_iwyu(<target>)`. |
| `scripts/iwyu/run_iwyu.py`             | Pipeline driver: configure → build → analyze → filter → apply → commit. |
| `scripts/iwyu/iwyu_filter.py`          | Filters IWYU output by scope, submodule paths, platform suffixes. |
| `scripts/iwyu/iwyu_commit.py`          | Stages results and creates a branch+commit as `GitHub Action`. |
| `scripts/iwyu/iwyu.imp`                | Project mapping file (PCH, Qt private headers, range-v3, …). |
| `.github/workflows/iwyu_updater.yml`   | Manual workflow that runs the pipeline on Linux and pushes the result. |
| `scripts/iwyu/run.sh`                  | Thin shim around `run_iwyu.py` that sources project-root `.env`. |

## CMake hook

`Telegram/cmake/iwyu.cmake` is included from `Telegram/CMakeLists.txt`. By
default it does nothing. Setting the option turns it on:

```bash
cmake -B out/iwyu -G Ninja \
    -DDESKTOP_APP_USE_IWYU=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=Debug
```

This sets the `CXX_INCLUDE_WHAT_YOU_USE` property **only on the `Telegram`
target**, never on `lib_*` or `td_*` libraries (those live in submodules
and are out of scope for rewrites). The IWYU command is built once and
includes:

- `--no_fwd_decls` — keep includes instead of forward declarations.
- `--cxx17ns` — concise C++17 nested-namespace output.
- `--quoted_includes_first` — match the project ordering convention.
- `--mapping_file=scripts/iwyu/iwyu.imp` when the file exists.

Pass extra IWYU flags by setting `DESKTOP_APP_IWYU_EXTRA_ARGS` on the
CMake command line.

## Submodules

The pipeline never edits files outside `Telegram/SourceFiles/`. Two
layers enforce this:

1. **CMake target scope** — IWYU runs only on the `Telegram` executable;
   submodule libraries (`lib_crl`, `lib_ui`, `lib_webrtc`, every
   `Telegram/ThirdParty/*`, `cmake/`, …) are separate CMake targets and
   never see the IWYU wrapper.
2. **Filter pass** — `iwyu_filter.py` reads `.gitmodules` at runtime and
   discards any IWYU output block whose subject path is inside a
   submodule. New submodules are picked up automatically.

A third belt-and-suspenders is `fix_includes.py --only_re=Telegram/SourceFiles/`,
applied by `run_iwyu.py` before any rewrite.

## Platform-specific files

Source paths matching the platform suffixes from
`cmake/nice_target_sources.cmake` (`win/`, `winrc/`, `windows/`, `mac/`,
`darwin/`, `osx/`, `linux/`, `posix/`, and the `*_<platform>.*` siblings)
are tracked by `iwyu_filter.py`. By default **no platform is excluded**
because a Linux IWYU run only sees Linux files anyway (the others are
already `HEADER_FILE_ONLY`).

Use `--skip-platform=auto` to drop the host platform's files from the
results — useful when you want a "portable" cleanup that touches only
cross-platform code:

```bash
python3 scripts/iwyu/run_iwyu.py --skip-platform=auto
```

`--skip-platform` accepts `auto`, `win`, `mac`, `linux`, `posix`, and may
be repeated. Pair with `--ignore` to drop additional globs.

## Local usage

A typical local pass on macOS:

```bash
brew install include-what-you-use

# Everything after `--` is forwarded verbatim to `cmake` configure, so
# you can list arbitrary -D... flags without quoting tricks.
export QT=6.2.13                                    # exact dir name in ../Libraries/local/
./scripts/iwyu/run.sh \
    --skip-platform=auto \
    -- \
    -DTDESKTOP_API_TEST=ON \
    -DDESKTOP_APP_DISABLE_AUTOUPDATE=OFF \
    -DDESKTOP_APP_DISABLE_CRASH_REPORTS=OFF \
    -DCMAKE_OSX_ARCHITECTURES=arm64
```

Notes:

- `QT` must match an existing `../Libraries/local/Qt-<value>` directory
  exactly (e.g. `Qt-6.2.13`, not just `6.2`).
- `CMAKE_OSX_ARCHITECTURES` must be a single arch — Swift in
  `lib_translate` rejects fat builds.
- The first run does ~750 build steps (codegen + AUTOMOC + dependent
  libraries) before IWYU can analyze; subsequent runs are incremental.

Equivalent, repeating the explicit `--cmake-arg=` form:

```bash
./scripts/iwyu/run.sh \
    --skip-platform=auto \
    --cmake-arg=-DTDESKTOP_API_TEST=ON \
    --cmake-arg=-DDESKTOP_APP_DISABLE_AUTOUPDATE=OFF
```

Stages can be skipped to iterate quickly:

```bash
# Re-run filter+apply against an existing iwyu.out:
./scripts/iwyu/run.sh --skip-configure --skip-build --skip-analyze
```

`--dry-run` passes `-n` to `fix_includes.py` so files stay untouched.

## Cleanup-only mode (`--no-adds --verify`)

A strict IWYU sweep adds every transitively-resolved include explicitly,
which on a PCH-heavy codebase like Telegram means thousands of new
`#include` lines. The cleanup-only mode keeps just the deletions:

```bash
./scripts/iwyu/run.sh \
    --skip-platform=auto --no-adds --verify \
    -- -DTDESKTOP_API_TEST=ON -DCMAKE_OSX_ARCHITECTURES=arm64
```

What `--no-adds --verify` does behind the scenes:

1. Strips every "should add these lines:" block from the IWYU output.
2. Routes apply through `iwyu_remove_only.py` (no reordering, no
   canonicalisation — only line-level deletions of unused includes).
3. **TU phase**: applies `.cpp/.cc/.mm/...` removes, then runs
   `-fsyntax-only` on every modified file in parallel using its
   `compile_commands.json` entry. Files whose syntax check fails are
   automatically restored.
4. **Header phase**: applies each `.h` in turn, looks up its dependent
   translation units via `ninja -t deps`, syntax-checks every dependent.
   On any failure the header is restored before moving to the next one,
   so a bad header never poisons the test of another.

The two phases together replace the otherwise-required manual loop of
`cmake --build → grep FAILED → revert → repeat`. A representative full
run on the Telegram target lands ~480 files / ~1,250 deletions and
auto-reverts ~270 unsafe ones. The result builds to a linked binary.

`--skip-headers` exists for the paranoid: it drops every `.h` removal
before applying, even with `--verify`. Useful when you want to commit a
cpp-only cleanup pass and follow up with headers later.

## How IWYU is wired around PCH

`Telegram` uses a precompiled header (`stdafx.h`). IWYU explicitly rejects
compile commands that load `-include-pch`. The pipeline keeps PCH on for
the regular build and works around the conflict in the analyze stage:

- `run_iwyu.py` reads `<build-dir>/compile_commands.json`, strips every
  `-Xclang -include-pch <path>` triple, and writes a copy to
  `<build-dir>/iwyu_db/compile_commands.json`. The `-include` flag for
  the cmake-generated PCH header is left intact, so every translation
  unit still sees the symbols stdafx.h normally pulls in.
- `iwyu_tool.py` is pointed at this scrubbed copy.
- The build itself (`cmake --build`) keeps using the original DB — PCH
  stays on, build speed is preserved.

`desktop_app_apply_iwyu(<target>)` exists for the alternate "IWYU as
part of the build" mode. It sets `CXX_INCLUDE_WHAT_YOU_USE` and forces
`DISABLE_PRECOMPILE_HEADERS=ON` on the target, but this is **not**
called automatically — opt in by adding the call after your
`target_precompile_headers(...)` line. Most users want the default
`run_iwyu.py` path.

## Apple Clang vs IWYU's bundled clang

IWYU is shipped against a different clang than Apple's. Two flags close
the gap and are added automatically:

- `-resource-dir=$(${CMAKE_CXX_COMPILER} -print-resource-dir)` so
  `<climits>`, `<type_traits>`, intrinsics, etc. are found.
- `-isysroot $(xcrun --show-sdk-path)` so `<cmath>` and the rest of the
  C++ standard library resolve from the SDK.

Both are also baked into `DESKTOP_APP_IWYU_COMMAND` for build-mode IWYU.

## Commit/push

Once `fix_includes.py` has rewritten files, hand off to `iwyu_commit.py`:

```bash
python3 scripts/iwyu/iwyu_commit.py --push
```

This creates `iwyu/cleanup-YYYYMMDD` (override with `--branch`), stages
only `Telegram/SourceFiles/` (override with `--pathspec`), commits as
`github-actions[bot] <41898282+github-actions[bot]@users.noreply.github.com>`
(override with `--author-name` / `--author-email`), and pushes when
`--push` is set.

`run_iwyu.py --commit --push` chains the whole pipeline in one call.

## GitHub Actions workflow

`.github/workflows/iwyu_updater.yml` runs the pipeline inside the same
CentOS docker image used by `linux.yml` and pushes the result. Trigger it
manually from the Actions tab (`workflow_dispatch`) — the cron schedule
is staged as a comment until a few cycles have been observed.

The workflow expects `include-what-you-use` to be present in the
`tdesktop:centos_env` image. If it is not, add the package to the image
recipe under `Telegram/build/docker/centos_env/` and rerun.

## Tuning the mapping file

`scripts/iwyu/iwyu.imp` covers three layers:

1. **PCH** — `stdafx.h` is invisible to IWYU.
2. **Qt private → public** — every `qXxx.h` that the codebase saw IWYU
   suggest is mapped to its `<QXxx>` umbrella. `qcontainerfwd.h` is
   handled with symbol mappings on the contained types.
3. **Project & generated headers** — `<lang_auto.h>` → `"lang/lang_keys.h"`,
   `<scheme.h>`/`<emoji.h>` → quoted form, plus libc++/SDK leakage like
   `<_stdlib.h>` and `<_string.h>`.

What the mapping file **cannot** do (IWYU asserts on the visibility
clash): redeclare visibility for headers IWYU already knows about
(e.g. `<math>`, `<QGuiApplication>`). Those need to be rewritten in
post-processing — see below.

Use `scripts/iwyu/iwyu_stats.py` to drive the next iteration:

```bash
python3 scripts/iwyu/iwyu_stats.py --input out/iwyu/iwyu.out --top 50
```

The "should add" histogram surfaces unknown private headers; the "should
remove" histogram surfaces project-style mismatches.

## Rewriting suggestions in post-processing

`iwyu_filter.py` exposes two pure transformations that `run_iwyu.py`
applies between the analyze and apply stages:

- `rewrite_generated_paths(text)` — converts angle-bracketed includes
  for project-quoted directories (`<styles/X.h>` → `"styles/X.h"`).
  Add new gen subdirs to `_QUOTE_GEN_PREFIXES`.
- `rewrite_qt_long_form(text)` — replaces `<QClass>` with
  `<QtModule/QClass>` for the Qt classes the codebase prefers in long
  form (clipboard/app/JSON/buffer/regex/window). When a paired
  `- #include <QtModule/QClass>` removal sits in the *same* IWYU block
  as the rewritten add, it is dropped so fix_includes.py does not
  needlessly reshuffle the include block. Genuine "you don't actually
  use it" removals in other blocks survive.

Both run under `_make_iwyu_compile_db` → `iwyu_tool.py` → these
rewrites → `filter_iwyu_output` → `fix_includes.py`.
