# Check the Git hooks and C++ style rules

Run these commands from the Telegram Desktop checkout. Use the sibling
`../stylecheck` checkout and matching LLVM and Clang installations for the full
run.

```sh
python3 -m pip install -r tools/rules/laws/requirements.txt
python3 ../stylecheck/run.py build --catalog tools/rules/laws/catalog.json
python3 tools/rules/laws/run.py
```

Use `--quick` to allow unavailable Clang tools. The runner prints each skipped
case and reports coverage for each engine. Use `--engine python` to select the
Python hook. Pass case IDs to run specific cases.

Pass `--catalog` to both commands to test a different catalog. The runner checks
that the plugin's generated `LayoutConfig.h` matches the selected layout. Use
`--plugin` for a plugin in a different build directory. Keep the generated header
beside the plugin.

The default catalog covers the Python edit, pre-commit, and commit-message
hooks and all `tdesktop-*` checks in the plugin. The runner checks the Python
layout messages and the plugin's registered check names against the catalog.
Every declared rule has at least one diagnostic fixture per supported mode.

## Add a case

1. Write the input C++ file under `fixtures/<case-id>/before.cpp`. For a
   commit-message case, put the subject in `before.txt`.
2. Add the case and its expected diagnostics to [the catalog](catalog.json).
   Set `fix` for each diagnostic. Use an empty array for an engine that passes.
   Set `mode` to `edit` or `commit-msg` for those hooks. Omit `line` for
   diagnostics that the hook reports without a source location.
3. If the case expects a fix, add `after.cpp` with the expected bytes.
4. If the case covers staged changes, add `parent.cpp` and list the expected
   added lines in `changed`. The runner uses the hook's Git diff parser.
5. If Clang needs specific flags or headers, set `cxx` or `headers`. The runner
   generates a compile database for each case. Include the target in `cxx` when
   it affects the diagnostic.
6. Run the new case and then the full catalog.

The catalog maps check names and Python diagnostic text to stable rule IDs.
The runner compares diagnostic lines, fix availability, and fixed bytes.
For staged Clang cases, it also matches the pre-commit hook's reported
diagnostics against the exported fix-it report.
After a fix, each selected engine for the case checks the entire file again.
For whitespace fixes, the runner also compares C++ tokens and comments.
For a rewrite that changes tokens, set the rule's `fix_kind` to
`source-rewrite` and record the exact new source in the case's `after.cpp`.

## Sort includes

```sh
python3 tools/rules/cpp_layout.py --fix-includes Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp
```

The formatter recognizes one block of literal includes at a time. It keeps
the file's own header first, then groups project, system, and generated
style headers. Nested directories precede files in the same project
directory. A standalone comment or preprocessing directive starts a new
block. Comments on an include line move with that include. The hook checks
touched blocks against the same order.

## Replay the installed hook

Generate the Telegram Desktop compile database and install the hook before
running a replay. Set `STYLECHECK_DB` if the compile database lives outside
`out/clang-tidy`. Each commit is staged against its first parent in a temporary
index.

```sh
python3 tools/rules/laws/run.py --replay '6fe1a9f85f^..ac3274e465'
```

## Check the Bend model

Use Bend 2.0.40 and lawcheck 0.4.1. For independent kernel verification, use
Lean 4.34.0 or set `BENDTT` to the executable built from Bend's `bendtt.lean`.
For an offline lawcheck run, set `BENDLIB_BEND_SRC` to the source tree for
Bend 2.0.40.

```sh
lawcheck tools/rules/laws/bend/LAWS.bend --size 3 --max-instances 150 --strict
bend tools/rules/laws/bend/PROOF.bend --verdict
python3 tools/rules/laws/bend/probe.py --verdict
```

Run `lawcheck` with the same flags on `bend/legacy.bend` to reproduce the
counterexample. Expect a nonzero exit and a one-line gap before the closer.
The probe runs the fixers on C++ cases marked with `bend_call`. It compares
the fixed bytes with the expected source, then checks the token spellings
and line positions reported by Tree-sitter and Clang against the same Bend
function.
[Bend proof guide](https://github.com/bendlang/bend/blob/main/guide/GUIDE.md#laws-and-proofs).
[lawcheck documentation](https://github.com/bendlib/bendlib/tree/main/tools/lawcheck).

## Run the closing-parenthesis specification

Run [closing_parenthesis.py](closing_parenthesis.py) on a C++ file to get its
diagnostics, applicable line intervals, and exact edits. The parser versions
are pinned in [the hook requirements](../requirements.txt). The output
follows [the findings schema](closing_parenthesis.schema.json).

```sh
python3 tools/rules/laws/closing_parenthesis.py < before.cpp
python3 tools/rules/laws/closing_parenthesis.py --line-ranges '[[4,4]]' < before.cpp
python3 tools/rules/laws/closing_parenthesis.py --fix < before.cpp > after.cpp
```

Input and fixed output are raw bytes. Each edit replaces a zero-based
half-open byte range from the original source. Source lines are one-based,
and line range endpoints are inclusive. Omit `--line-ranges` to inspect the
whole file. Add `--catalog` to select layout parameters.

The Python hook imports the specification. clang-tidy runs the same script
with the source bytes already loaded through Clang's virtual filesystem.
The plugin build records the Python executable used by `run.py build`.
Install the pinned requirements in that environment or set
`STYLECHECK_PYTHON` to the absolute path of an interpreter that has them.

## Change the closing-parenthesis decision

Edit [closer.bend](bend/closer.bend) and update its laws and fixture expectations.
The inputs are whether the closer starts its own line, whether the tail
contains only closing punctuation, whether the gap is whitespace, and
whether the adapter can join the lines within the width limit. The outputs
are skip, diagnose, and attach.

```sh
bend tools/rules/laws/bend/PROOF.bend --verdict
python3 tools/rules/laws/bend/generate_closer.py --write
python3 ../stylecheck/run.py build --catalog tools/rules/laws/catalog.json
python3 tools/rules/laws/run.py
```

Run the generator without `--write` to check that the catalog matches Bend.
The executable specification reads the generated table. It also defines the
parser inputs, layout checks, line selection, and byte edits that surround
the decision.
