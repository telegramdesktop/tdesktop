#!/usr/bin/env python3
"""Freeze the scope of a cross-review session and snapshot each round.

The contract is .agents/shared/cross-review/protocol.md.

  cross_review_scope.py init --source-root ROOT [--commits N] [--session DIR]
  cross_review_scope.py snapshot --session DIR --round R

`init` fixes the base: HEAD for uncommitted changes, HEAD~N for the last N
commits. `snapshot` records the working tree, untracked files included, as a
git tree through a private index, so the real index is never touched, and
writes the round's complete scope diff and, after the first round, the diff of
the fixes made since the previous snapshot.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time


def fail(text, code=1):
    print('cross_review_scope: ' + text, file=sys.stderr)
    sys.exit(code)


def git(root, *args, env=None, binary=False):
    result = subprocess.run(
        ['git', '-C', root] + list(args),
        capture_output=True,
        env=env)
    if result.returncode != 0:
        fail('git %s failed: %s' % (
            ' '.join(args),
            result.stderr.decode('utf-8', 'replace').strip()))
    return result.stdout if binary else result.stdout.decode('utf-8', 'replace').strip()


def session_file(session):
    return os.path.join(session, 'session.json')


def command_init(args):
    root = os.path.abspath(args.source_root)
    top = git(root, 'rev-parse', '--show-toplevel')
    common = git(root, 'rev-parse', '--git-common-dir')
    if not os.path.isabs(common):
        common = os.path.join(root, common)
    base_ref = 'HEAD~%d' % args.commits if args.commits else 'HEAD'
    if args.commits:
        count = int(git(root, 'rev-list', '--count', 'HEAD'))
        if args.commits >= count:
            fail('HEAD has only %d commits' % count)
    base = git(root, 'rev-parse', '--verify', base_ref + '^{commit}')
    session = args.session or os.path.join(
        os.path.abspath(common),
        'cross-review',
        time.strftime('%Y%m%d-%H%M%S'))
    if os.path.exists(session_file(session)):
        fail('session already exists: ' + session)
    os.makedirs(session, exist_ok=True)
    commits = []
    if args.commits:
        commits = git(root, 'log', '--format=%h %s', base + '..HEAD').splitlines()
    data = {
        'source_root': os.path.abspath(top),
        'mode': 'commits' if args.commits else 'uncommitted',
        'commits': args.commits or 0,
        'base': base,
        'base_subject': git(root, 'log', '-1', '--format=%h %s', base),
        'head_at_start': git(root, 'rev-parse', 'HEAD'),
        'commit_list': commits,
        'dirty_at_start': bool(git(root, 'status', '--porcelain')),
        'created': time.strftime('%Y-%m-%d %H:%M:%S'),
    }
    with open(session_file(session), 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)
    print(json.dumps({'session': session, **data}))
    return 0


def previous_tree(session, round_number):
    for earlier in range(round_number - 1, 0, -1):
        path = os.path.join(session, 'round%d' % earlier, 'tree.txt')
        if os.path.isfile(path):
            with open(path, encoding='utf-8') as f:
                return earlier, f.read().strip()
    return None, None


def command_snapshot(args):
    session = os.path.abspath(args.session)
    try:
        with open(session_file(session), encoding='utf-8') as f:
            data = json.load(f)
    except (OSError, ValueError):
        fail('no session at ' + session)
    root = data['source_root']
    index = os.path.join(session, 'index')
    if not os.path.exists(index):
        real = git(root, 'rev-parse', '--git-path', 'index')
        if not os.path.isabs(real):
            real = os.path.join(root, real)
        if os.path.exists(real):
            shutil.copyfile(real, index)
    env = dict(os.environ)
    env['GIT_INDEX_FILE'] = index
    git(root, 'add', '-A', env=env)
    tree = git(root, 'write-tree', env=env)
    folder = os.path.join(session, 'round%d' % args.round)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, 'tree.txt'), 'w', encoding='utf-8') as f:
        f.write(tree + '\n')
    base = data['base']
    patch = git(root, 'diff', '--no-color', '-M', base, tree, binary=True)
    with open(os.path.join(folder, 'scope.patch'), 'wb') as f:
        f.write(patch)
    files = git(root, 'diff', '--name-status', '-M', base, tree)
    with open(os.path.join(folder, 'files.txt'), 'w', encoding='utf-8') as f:
        f.write(files + ('\n' if files else ''))
    stat = git(root, 'diff', '--shortstat', base, tree)
    summary = {
        'round': args.round,
        'tree': tree,
        'scope_patch': os.path.join(folder, 'scope.patch'),
        'files_txt': os.path.join(folder, 'files.txt'),
        'files': len(files.splitlines()) if files else 0,
        'scope_stat': stat,
    }
    earlier, earlier_tree = previous_tree(session, args.round)
    if earlier_tree:
        fix = git(root, 'diff', '--no-color', '-M', earlier_tree, tree, binary=True)
        with open(os.path.join(folder, 'fix.patch'), 'wb') as f:
            f.write(fix)
        summary['fix_since_round'] = earlier
        summary['fix_patch'] = os.path.join(folder, 'fix.patch')
        summary['fix_stat'] = git(root, 'diff', '--shortstat', earlier_tree, tree)
    print(json.dumps(summary))
    return 0 if files else 3


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    init = commands.add_parser('init')
    init.add_argument('--source-root', required=True)
    init.add_argument('--commits', type=int, default=0)
    init.add_argument('--session')
    snapshot = commands.add_parser('snapshot')
    snapshot.add_argument('--session', required=True)
    snapshot.add_argument('--round', type=int, required=True)
    args = parser.parse_args()
    handlers = {'init': command_init, 'snapshot': command_snapshot}
    sys.exit(handlers[args.command](args))


if __name__ == '__main__':
    main()
