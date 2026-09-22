# clang-tidy for tdesktop

```bash
python3 tools/clangtidy/compile_db.py               # after each configure
git diff -U0 HEAD | clang-tidy-diff.py -p1 -path out/clang-tidy -j 10 -quiet
run-clang-tidy -p out/clang-tidy -j 10 -quiet <files>
```

Both runners ship with LLVM, under `share/clang`. Needs clang-tidy 20 or
newer: an older one ignores `CustomFunctions` without a word.

The commit hook, once per clone, submodules included; its tests run with
`python3 tools/rules/test_hook.py`:

```bash
for repo in . $(git config -f .gitmodules --get-regexp path \
        | awk '{print $2}' | grep -v ThirdParty); do
    hooks=$(git -C "$repo" rev-parse --git-path hooks)/pre-commit
    printf '#!/bin/sh\nexec python3 %s/tools/rules/hook.py --staged\n' \
        "$PWD" > "$hooks" && chmod +x "$hooks"
done
```
