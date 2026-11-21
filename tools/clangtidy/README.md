# clang-tidy for tdesktop

```bash
python3 tools/clangtidy/compile_db.py               # after each configure
git diff -U0 HEAD | clang-tidy-diff.py -p1 -path out/clang-tidy -j 10 -quiet
run-clang-tidy -p out/clang-tidy -j 10 -quiet <files>
```

Both runners ship with LLVM, under `share/clang`. Needs clang-tidy 20 or
newer: an older one ignores `CustomFunctions` without a word.

## Git hooks

```bash
python3 tools/rules/install.py
python3 tools/rules/test_hook.py
```

Build the optional plugin with `python3 ../stylecheck/run.py build`.
The plugin checks wrapped constructor parameters and nested multiline
arguments. Nested-call diagnostics offer a whitespace fix when the result
fits the 78-column limit. Preview the fixes with:

```bash
python3 ../stylecheck/run.py sweep "$PWD" --db out/clang-tidy \
    --check tdesktop-nested-multiline-call --diff
```

After a Ninja build, rerun `python3 tools/clangtidy/compile_db.py` to index its
header dependencies. The hook uses that index to find a compiled C++ source
that includes each staged header. A header absent from the index can still be
checked through a staged source that includes it or a compiled source with the
same basename. Diagnostics are limited to the staged lines.
