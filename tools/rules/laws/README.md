# Check the Git hooks and C++ style rules

Run from the Telegram Desktop root. The Clang checks need `../stylecheck` and matching LLVM and clang-tidy versions.

```sh
python3 -m pip install -r tools/rules/laws/requirements.txt
python3 ../stylecheck/run.py build --catalog tools/rules/laws/catalog.json
python3 tools/rules/laws/run.py
```

- Use `--quick` to skip unavailable Clang checks, `--engine python` to run the Python checks, or pass case IDs.
- Pass a custom `--catalog` to both scripts. To use `--plugin`, keep its generated `LayoutConfig.h` beside the plugin.

## Add a case

1. Add `fixtures/<case-id>/before.cpp` or `before.txt` for a commit subject.
2. Add expectations to [the catalog](catalog.json). Use an empty array for a passing engine. Set `fix` for each diagnostic. Set `mode` for edit and commit-message cases.
3. Add `after.cpp` for a fix. Set `fix_kind` to `source-rewrite` if the fix changes C++ tokens.
4. To test a staged diff, add `parent.cpp` and list the changed lines in `changed`. Set `cxx` and `headers` as needed for Clang.
5. Run the case by ID, then run the full catalog.

## Sort includes

```sh
python3 tools/rules/cpp_layout.py --fix-includes Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp
```

## Replay the installed hook

Install the hook and generate the compile database first. Set `STYLECHECK_DB` if it lives outside `out/clang-tidy`. Replay stages each commit against its first parent in a temporary index.

```sh
python3 tools/rules/laws/run.py --replay '6fe1a9f85f^..ac3274e465'
```

## Check the Bend model

Use Bend 2.0.40 and lawcheck 0.4.1. For independent kernel verification, use Lean 4.34.0 or point `BENDTT` at the executable built from `bendtt.lean`. For offline lawcheck, point `BENDLIB_BEND_SRC` at the Bend source tree.

```sh
lawcheck tools/rules/laws/bend/LAWS.bend --size 3 --max-instances 150 --strict
bend tools/rules/laws/bend/PROOF.bend --verdict
python3 tools/rules/laws/bend/probe.py --verdict
```

Run lawcheck with the same flags on `bend/legacy.bend` to reproduce the counterexample.

## Inspect or fix closing parentheses

```sh
python3 tools/rules/laws/closing_parenthesis.py < before.cpp
python3 tools/rules/laws/closing_parenthesis.py --line-ranges '[[4,4]]' < before.cpp
python3 tools/rules/laws/closing_parenthesis.py --fix < before.cpp > after.cpp
```

The [findings schema](closing_parenthesis.schema.json) uses zero-based, half-open byte ranges for edits and one-based, inclusive line ranges. Use `--catalog` to select layout parameters. Install the [parser dependencies](../requirements.txt) for the plugin's Python interpreter, or set `STYLECHECK_PYTHON` to the absolute path of an interpreter that has them.

## Change the closing-parenthesis decision

Edit [closer.bend](bend/closer.bend), its laws, and the fixture expectations.

```sh
bend tools/rules/laws/bend/PROOF.bend --verdict
python3 tools/rules/laws/bend/generate_closer.py --write
python3 ../stylecheck/run.py build --catalog tools/rules/laws/catalog.json
python3 tools/rules/laws/run.py
```

Run the generator without `--write` to verify the catalog against Bend.
