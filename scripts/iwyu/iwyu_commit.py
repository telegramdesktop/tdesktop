#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
"""
Stage IWYU's edits, commit them on a fresh branch, and (optionally) push.

The commit is authored as the canonical `github-actions[bot]` identity
(name `github-actions[bot]`, email
`41898282+github-actions[bot]@users.noreply.github.com`) so that the
provenance is identical between local runs and the scheduled workflow,
and GitHub renders the commit with the bot avatar.

Submodule changes are explicitly not staged: even if a working tree happens
to have unrelated submodule pointer drift, the commit reflects only the
files this run intended to touch (i.e. the include rewrites in
Telegram/SourceFiles).
"""

from __future__ import annotations

import argparse
import datetime as _dt
import os
import subprocess
import sys
from pathlib import Path

DEFAULT_AUTHOR_NAME = "github-actions[bot]"
DEFAULT_AUTHOR_EMAIL = "41898282+github-actions[bot]@users.noreply.github.com"
DEFAULT_BRANCH_PREFIX = "iwyu/cleanup"
DEFAULT_PATHSPECS = ["Telegram/SourceFiles/"]
DEFAULT_COMMIT_MESSAGE = "Removed unused includes via include-what-you-use."


def _git(args: list[str], *, cwd: Path, check: bool = True,
         capture: bool = False, env: dict[str, str] | None = None) -> str:
    cmd = ["git"] + args
    sys.stderr.write("$ " + " ".join(cmd) + "\n")
    result = subprocess.run(
        cmd,
        cwd=cwd,
        env=env,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=None,
    )
    if check and result.returncode != 0:
        sys.exit(f"git {args[0]} failed (exit {result.returncode}).")
    return (result.stdout or "").strip()


def has_changes_in(repo_root: Path, pathspecs: list[str],
                   include_untracked: bool) -> bool:
    out = _git(
        ["status", "--porcelain", "--", *pathspecs],
        cwd=repo_root, capture=True,
    )
    for line in out.splitlines():
        if not line.strip():
            continue
        if line.startswith("??") and not include_untracked:
            continue
        return True
    return False


def auto_branch_name(prefix: str) -> str:
    today = _dt.datetime.now(_dt.timezone.utc).strftime("%Y%m%d")
    return f"{prefix}-{today}"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--repo-root", default=".", type=Path)
    p.add_argument("--branch", default=None,
                   help="Branch name to create. Default: iwyu/cleanup-YYYYMMDD.")
    p.add_argument("--branch-prefix", default=DEFAULT_BRANCH_PREFIX,
                   help="Prefix for the auto-generated branch name.")
    p.add_argument("--message", default=DEFAULT_COMMIT_MESSAGE,
                   help="Commit message subject (must end with a period).")
    p.add_argument("--pathspec", action="append", default=None,
                   help=("Pathspec(s) to stage. May be repeated. "
                         f"Default: {DEFAULT_PATHSPECS}."))
    p.add_argument("--author-name", default=DEFAULT_AUTHOR_NAME)
    p.add_argument("--author-email", default=DEFAULT_AUTHOR_EMAIL)
    p.add_argument("--allow-empty", action="store_true",
                   help="Make a commit even if there are no changes (rare).")
    p.add_argument("--no-branch", action="store_true",
                   help="Commit on the current branch — do not create a new one.")
    p.add_argument("--include-untracked", action="store_true",
                   help="Also stage untracked files inside the pathspec. "
                        "Off by default — IWYU only rewrites existing files, "
                        "so by default we use `git add --update` and ignore "
                        "any unrelated junk left in the working tree.")
    p.add_argument("--push", action="store_true")
    p.add_argument("--push-remote", default="origin")
    p.add_argument("--force-push", action="store_true",
                   help="Allow non-fast-forward push (use --force-with-lease).")
    args = p.parse_args()

    repo_root = args.repo_root.resolve()
    if not (repo_root / ".git").exists():
        sys.exit(f"{repo_root} is not a git working tree.")

    pathspecs = args.pathspec if args.pathspec else list(DEFAULT_PATHSPECS)

    if not has_changes_in(repo_root, pathspecs, args.include_untracked) \
            and not args.allow_empty:
        sys.stderr.write("No changes in scope — nothing to commit.\n")
        return 0

    branch = args.branch or auto_branch_name(args.branch_prefix)
    if not args.no_branch:
        existing = _git(["branch", "--list", branch],
                        cwd=repo_root, capture=True)
        if existing.strip():
            _git(["checkout", branch], cwd=repo_root)
        else:
            _git(["checkout", "-b", branch], cwd=repo_root)

    add_args = ["add"]
    if not args.include_untracked:
        add_args.append("--update")
    add_args += ["--", *pathspecs]
    _git(add_args, cwd=repo_root)

    env = os.environ.copy()
    env["GIT_AUTHOR_NAME"] = args.author_name
    env["GIT_AUTHOR_EMAIL"] = args.author_email
    env["GIT_COMMITTER_NAME"] = args.author_name
    env["GIT_COMMITTER_EMAIL"] = args.author_email

    commit_args = ["commit", "-m", args.message]
    if args.allow_empty:
        commit_args.append("--allow-empty")
    _git(commit_args, cwd=repo_root, env=env)

    if args.push:
        push_args = ["push", "--set-upstream", args.push_remote, branch]
        if args.force_push:
            push_args.insert(1, "--force-with-lease")
        _git(push_args, cwd=repo_root)
    else:
        sys.stderr.write(
            f"Done. Branch '{branch}' has the IWYU cleanup commit. "
            f"Push with: git push -u {args.push_remote} {branch}\n"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
