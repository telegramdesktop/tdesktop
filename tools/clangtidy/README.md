# clang-tidy for tdesktop

```bash
python3 tools/clangtidy/compile_db.py               # after each configure
git diff -U0 HEAD | clang-tidy-diff.py -p1 -path out/clang-tidy -j 10 -quiet
run-clang-tidy -p out/clang-tidy -j 10 -quiet <files>
```

The second line is the gate: it reports only the lines a commit touches. Both
runners ship with LLVM, under `share/clang` next to the binary.

Needs clang-tidy 20 or newer -- an older one ignores `CustomFunctions`, which
the bans in `.clang-tidy` are built on, without a word.
