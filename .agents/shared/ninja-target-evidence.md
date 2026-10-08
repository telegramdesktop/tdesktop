# Ninja Target Provenance Evidence

`.agents/shared/evidence/ninja_target.py` answers one question from a
configured CMake Ninja tree: for one exact consuming output (an executable or
shared library link edge), which object files does that edge consume, which
compile edge and source produced each one, which PCH each one uses, and what
command-line state requested macros have in a selected policy cohort. It reads
Ninja's evaluated graph only and never runs a graph command.

## When to use it

Use the helper for every claim of the form "target T links object O", "O was
compiled from S with PCH P", "macro M is (not) defined for T's sources", "CMake
option X reaches the compiler as macro M", and before any symbol or absence
check (`nm`, `objdump`, `strings`) that is meant to speak for a target. Feed
those tools only object paths the helper selected.

These are not ownership evidence:

- a glob or a basename (`find -name update_verify.cpp.o` finds the
  `Telegram.dir` copy and the `Packer.dir` copy of the same source);
- `ninja -t compdb` or `-t compdb-targets` listings: `compdb-targets` walks
  order-only dependencies, so it pulls in sibling tools such as `Debug/Packer`,
  and it does not exist on Ninja 1.11;
- `ninja -t inputs`, which flattens explicit, implicit and order-only edges;
- the CMake option name read as a macro name.

## Commands

Run from the checkout root; `--build-dir`, `--manifest`, `--config` and
`--target` are mandatory and exact.

Linux (`out/`, Ninja Multi-Config):

```bash
python3 .agents/shared/evidence/ninja_target.py \
  --build-dir out --manifest build-Debug.ninja --config Debug \
  --target Debug/Telegram \
  --policy-target Telegram \
  --option-macro DESKTOP_APP_DISABLE_AUTOUPDATE=TDESKTOP_DISABLE_AUTOUPDATE \
  --expect-defined _DEBUG \
  --select-source SOURCE_ROOT_AS_CONFIGURED/Telegram/SourceFiles/core/update_verify.cpp
```

`SOURCE_ROOT_AS_CONFIGURED` is the source root the graph spells, which is the
container path (for example `/usr/src/tdesktop`) when `out/` was configured in
a container; take it from `CMAKE_HOME_DIRECTORY` in `out/CMakeCache.txt`.

macOS Ninja tree (the link output lives inside the bundle; `Telegram` itself
is a phony alias and is refused):

```bash
python3 .agents/shared/evidence/ninja_target.py \
  --build-dir BUILD_DIR --manifest build-Debug.ninja --config Debug \
  --target Debug/Telegram.app/Contents/MacOS/Telegram \
  --policy-target Telegram \
  --option-macro DESKTOP_APP_DISABLE_AUTOUPDATE=TDESKTOP_DISABLE_AUTOUPDATE \
  --expect-defined TDESKTOP_ALLOW_CLOSED_ALPHA --expect-defined _DEBUG \
  --select-source "$PWD/Telegram/SourceFiles/core/update_verify.cpp" \
  --list-objects
```

Request only macros whose values may be published: readings include the
values of requested defined macros, so never pass `TDESKTOP_API_ID` or
`TDESKTOP_API_HASH`. Other options: `--ninja PATH`, repeated `--select-object PATH`,
`--expect-undefined NAME`, `--expect-defined NAME=VALUE`. A single-config
`Ninja` tree uses `--manifest build.ninja` with `--config` equal to
`CMAKE_BUILD_TYPE`. Selectors are exact graph spellings: build-relative object
paths (`Telegram/CMakeFiles/Telegram.dir/Debug/SourceFiles/core/update_verify.cpp.o`)
and absolute source paths as the graph spells them. A basename is refused.

Exit 0 prints a JSON report with `verdict: "PASS"`; any refusal or unmet
expectation prints `error: ...` and exits 1. There is no FAIL verdict to
interpret.

## Reading the report

- `objects`: consumed object count, counts by graph edge kind (`explicit`,
  `implicit`) and producer rule, `external` objects with no producer in the
  graph (prebuilt Qt plugin-init objects), and with `--list-objects` the
  ordered list with source, rule and PCH per object.
- `selections`: each selected object; `--select-source` also lists
  `excluded_siblings`, the same source's objects this target does not consume,
  with their rule and graph consumers (for example Packer's copy with consumer
  `Debug/Packer`).
- `pchs`: every PCH used, its producer rule, header, dialect and consumer
  count. `archives` and `shared_libraries` are listed, not resolved.
- `cohort`: the policy cohort rules, object count, its PCHs, names of defined
  macros (never values) and the requested macro readings, including option
  contracts with their cache entries.
- `build.path_mapping`, `acquisition.snapshot_digest` and `limits` (below).

Counts are observations of that tree, not constants to copy into later work.

## Limits

Configured-graph claims only. The report does not prove that a command ran,
that an object exists on disk, which archive members the linker pulled, or
what headers, compiler built-ins or source code define. The manifest snapshot
guards graph coherence during acquisition; it does not claim the tree was
otherwise untouched.

## Supported and refused grammar

Supported: CMake-generated `Ninja` / `Ninja Multi-Config` trees with
`CMakeCache.txt`; POSIX command strings made of `&&`-joined simple commands
with `:` and `cd ABS` segments (exactly one segment links `-o TARGET`, from the
build directory); GCC/Clang drivers with an optional `ccache`/`sccache`
launcher; clang `-Xclang -include-pch`/`-emit-pch` and GCC `-include` + `.gch`
PCH dialects; response files expanded by `ninja -t compdb -x`; ordered
`-DNAME[=V]`, `-D NAME`, `-UNAME`, `-U NAME` (later wins, `-DX=0` is defined).

Refused with a message: other shell operators, `$`, backticks, newlines, a
quoted `"&&"`; unexpanded `@file`; unknown link operands (including a `.pch`
that CMake puts on the link line for `$<TARGET_OBJECTS:...>` of a PCH object
library); duplicate object operands; link objects that are not explicit or
implicit graph inputs (the message says when they are order-only only); graph
inputs missing from the link line; producer output/source mismatches;
non-compile producers; PCHs used but not graph inputs (a GCC `-Winvalid-pch`
command counts as PCH use), or graph inputs not used, or produced by another
target's rule (`REUSE_FROM`); `-imacros`, `-Wp,-D`, `-Xpreprocessor`,
`-Xclang -D`; a non-PCH forced include when a macro is requested; MSVC,
Xcode and other generators. A real graph that needs more grammar is recorded
as a refusal, not widened in place.

## Policy cohorts and option contracts

`--policy-target NAME` selects consumed objects compiled by
`(C|CXX|OBJC|OBJCXX)_COMPILER__NAME(_unscanned)?_CONFIG` plus the PCHs they
use. Every cohort object and PCH must meet each expectation and each object
must agree with its PCH. The whole link closure is never one cohort: object
libraries such as `td_export` or `td_ui` have their own definitions and PCHs,
so run one invocation per cohort. On multi-config trees a cohort
`CMAKE_INTDIR` must equal the selected configuration.

An absence claim needs a known-present control in the same run: any
`--expect-undefined` (or OFF `--option-macro`) requires at least one
`--expect-defined` (or ON `--option-macro`).

`--option-macro OPTION=MACRO` reads a BOOL cache entry (ON -> defined, OFF ->
undefined) and checks the named macro. The pair is written explicitly because
spellings differ: `Telegram/cmake/telegram_options.cmake` maps
`DESKTOP_APP_DISABLE_AUTOUPDATE` to `TDESKTOP_DISABLE_AUTOUPDATE`, and
`DESKTOP_APP_SPECIAL_TARGET` (a STRING, so usable only as an
`--expect-defined TDESKTOP_ALLOW_CLOSED_ALPHA` control) likewise. On an OFF
tree a wrong macro name and the right one are both absent, so an option claim
needs a tree where the option is ON, and the same-name contract
(`DESKTOP_APP_DISABLE_AUTOUPDATE=DESKTOP_APP_DISABLE_AUTOUPDATE`) must fail
there.

## Path identity

Paths are compared as exact strings. The only transformation strips the
configured build directory (`CMAKE_CACHEFILE_DIR`) prefix from absolute
command tokens. Ninja reports its physical working directory, which can differ
from the configured one: macOS temporary trees (`/tmp` vs `/private/tmp`,
`/var/folders` vs `/private/var/folders`) record `validated_by: "samefile"`;
a tree configured in a container (`/usr/src/tdesktop/out`) and read on the
host records `validated_by: "CMakeCache.txt CMAKE_CACHEFILE_DIR"`. A
configured path that exists locally as a different directory is refused.
No realpath, basename or suffix matching is used.

## Raw captures and symbols

The helper has no capture option. When a check needs an independent
cross-check, run `ninja -C BUILD_DIR -f MANIFEST -t query ...` or
`-t compdb -x RULE` directly into the ignored task `.local/` run directory:
Telegram compile commands carry private `TDESKTOP_API_ID`/`TDESKTOP_API_HASH`
values, so raw records never go into published evidence. Use only
`query`/`compdb`; never `ninja` without `-t` on a tree you must not build.

For symbol or absence checks run `nm BUILD_DIR/<selected object>` on the
helper-selected path, and read the excluded sibling the same way as a control
that shows the markers differ.

## Self-tests

```bash
python3 .agents/shared/evidence/ninja_target_test.py -v
```

The suite needs `ninja`; its compiled class also needs `cmake`, a C/C++
compiler and `nm`, and configures and builds a small two-target fixture where
`fx_app` and `fx_tool` compile the same `shared/common.cpp`. On a host without
cmake (the WSL build host), set `NINJA_TARGET_TEST_SKIP_CMAKE=1` to skip only
that class explicitly; a missing tool otherwise fails the suite.
