"""Code health: measure the codebase, keep a findings ledger, and check
changes. Run from the repository root.

  scan   [--ref origin/dev] [--fetch] [--queue ../ai-tdesktop]
  sync   (dedicated scan checkout only: detach at fresh origin/dev)
  atlas  [--ref origin/dev] [--check FILE]
  area   <path-prefix-or-area>
  check  [--base HEAD] [--staged] [--strict]
  moves  [--base HEAD] [--head REV] [--staged]
  verify [--base HEAD] <finding-id>...
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import atlas
import cmake_model
import cpp_text
import includes
import metrics
import report
import review
import sources


def storage_root(repo):
    common = Path(sources.git(repo, 'rev-parse', '--git-common-dir').decode().strip())
    if not common.is_absolute():
        common = Path(repo) / common
    root = common.resolve() / 'code-health'
    (root / 'runs').mkdir(parents=True, exist_ok=True)
    return root


def write_atomic(path, text):
    handle, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name + '.')
    with os.fdopen(handle, 'w', encoding='utf-8') as stream:
        stream.write(text)
    os.replace(temporary, path)


def load(path):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None


def tree_for(args):
    if args.ref:
        if args.fetch:
            sources.git(args.repo, 'fetch', '--no-tags', '--no-recurse-submodules',
                        'origin', '+refs/heads/dev:refs/remotes/origin/dev')
        return sources.from_ref(args.repo, args.ref)
    return sources.from_worktree(args.repo)


def command_scan(args):
    tree = tree_for(args)
    queue = args.queue if args.queue and Path(args.queue).is_dir() else None
    snap = report.snapshot(tree, queue)
    root = storage_root(args.repo)
    previous = load(root / 'latest.json')
    current = report.findings(snap)
    ledger = report.merge(previous, current, snap['checked_at_utc'])
    for identity in (load(root / 'ignored.json') or {}):
        if identity in ledger and ledger[identity]['status'] == 'open':
            ledger[identity]['status'] = 'ignored'
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    run = root / 'runs' / f'{stamp}-{tree.commit[:10]}'
    run.mkdir(parents=True, exist_ok=True)
    text = report.markdown(snap, ledger, previous)
    (run / 'snapshot.json').write_text(json.dumps(snap, indent=1) + '\n', encoding='utf-8')
    (run / 'report.md').write_text(text, encoding='utf-8')
    latest = {
        'commit': snap['commit'],
        'ref': snap['ref'],
        'checked_at_utc': snap['checked_at_utc'],
        'run_directory': str(run.relative_to(root)),
        'totals': snap['totals'],
        'findings': ledger,
    }
    write_atomic(root / 'latest.json', json.dumps(latest, indent=1) + '\n')
    write_atomic(root / 'latest.md', text)
    opened = sum(1 for f in ledger.values() if f['status'] == 'open')
    print(json.dumps({
        'commit': snap['commit'],
        'ref': snap['ref'],
        'storage_root': str(root),
        'run_directory': str(run),
        'worktree_matches': worktree_matches(args.repo, snap['commit']),
        'open_findings': opened,
        'totals': snap['totals'],
    }, indent=2))
    return 0


def worktree_matches(repo, commit):
    """Whether the working tree is exactly `commit`, submodules included,
    so agents reading files read what was scanned."""
    head = sources.git(repo, 'rev-parse', 'HEAD').decode().strip()
    dirty = sources.git(repo, 'status', '--porcelain', '--', 'Telegram').strip()
    return head == commit and not dirty


def command_sync(args):
    repo = Path(args.repo)
    attached = subprocess.run(
        ['git', '-C', str(repo), 'symbolic-ref', '-q', 'HEAD'],
        capture_output=True).returncode == 0
    if attached:
        print('sync runs only in a dedicated clone detached at origin/dev; '
              'see "Where it runs" in the code-health skill.',
              file=sys.stderr)
        return 1
    if sources.git(repo, 'status', '--porcelain').strip():
        print('The checkout has local changes; sync never discards them.',
              file=sys.stderr)
        return 1
    sources.git(repo, 'fetch', '--no-tags', '--no-recurse-submodules',
                'origin', '+refs/heads/dev:refs/remotes/origin/dev')
    sources.git(repo, 'checkout', '--detach', '--quiet', 'origin/dev')
    raw = sources.git(repo, 'ls-tree', 'HEAD', '--', 'Telegram/').decode()
    libs = [
        line.split('\t', 1)[1] for line in raw.splitlines()
        if line.split()[1] == 'commit' and line.split('\t', 1)[1].startswith('Telegram/lib_')
    ]
    sources.git(repo, 'submodule', 'update', '--init', '--', *libs)
    commit = sources.git(repo, 'rev-parse', 'HEAD').decode().strip()
    print(json.dumps({
        'commit': commit,
        'submodules': libs,
        'worktree_matches': worktree_matches(repo, commit),
    }, indent=2))
    return 0


def command_atlas(args):
    tree = tree_for(args)
    if args.check:
        draft = atlas.Atlas(Path(args.check).read_text(encoding='utf-8'))
        result = atlas.check(draft, tree)
        sizes = {}
        for path in tree.files:
            if atlas.mapped_file(path):
                name = draft.area_of(path)
                if name:
                    sizes[name] = sizes.get(name, 0) + tree.lines(path)
        print(f'{len(draft.areas)} areas; {len(result["unmapped"])} unmapped files; '
              f'{len(result["ambiguous"])} ambiguous; {len(result["unused"])} unused patterns.')
        folders = {}
        for path in result['unmapped']:
            folder = os.path.dirname(path)
            folders[folder] = folders.get(folder, 0) + 1
        for folder, count in sorted(folders.items()):
            print(f'unmapped  {folder}/ ({count} files)')
        for path, names in result['ambiguous'][:40]:
            print(f'ambiguous {path}: {", ".join(names)}')
        for name, pattern in result['unused']:
            print(f'unused    {name}: `{pattern}`')
        empty = [name for name in draft.areas if not sizes.get(name)]
        for name in empty:
            print(f'empty     {name}')
        print()
        for name, lines in sorted(sizes.items(), key=lambda kv: -kv[1]):
            print(f'{lines:8,}  {name}')
        return 1 if result['unmapped'] or result['ambiguous'] or result['unused'] else 0
    queue = args.queue if args.queue and Path(args.queue).is_dir() else None
    snap = report.snapshot(tree, queue)
    graph = includes.IncludeGraph(tree, cmake_model.build(tree.files))
    rows = atlas.skeleton(tree, snap, graph)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    directory = storage_root(args.repo) / 'atlas' / f'{stamp}-{tree.commit[:10]}'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'skeleton.json').write_text(json.dumps(rows, indent=1) + '\n', encoding='utf-8')
    (directory / 'skeleton.md').write_text(atlas.markdown(rows), encoding='utf-8')
    print(json.dumps({
        'commit': tree.commit,
        'directory': str(directory),
        'candidates': len(rows),
        'lines': sum(row['lines'] for row in rows),
    }, indent=2))
    return 0


def command_area(args):
    root = storage_root(args.repo)
    latest = load(root / 'latest.json')
    if not latest:
        print('No scan yet; run `health.py scan` first.', file=sys.stderr)
        return 1
    snap = load(root / latest['run_directory'] / 'snapshot.json')
    prefix = args.area.rstrip('/')

    def inside(path):
        area = snap['files'].get(path, {}).get('area') or metrics.area_of(path)
        return (area == prefix
                or area.startswith(prefix + '/')
                or path.startswith(prefix)
                or path.startswith(f'Telegram/SourceFiles/{prefix}'))

    files = {p: e for p, e in snap['files'].items() if inside(p)}
    out = [f'# Area {prefix} at `{snap["commit"][:12]}`', '',
           '| File | Lines | Churn | Hotspot | Max fn | Weight | Reach | Tested |',
           '|---|---|---|---|---|---|---|---|']
    for path, entry in sorted(files.items(), key=lambda kv: -kv[1]['hotspot']):
        out.append(
            f'| {path} | {entry["lines"]} | {entry["churn"]} | {entry["hotspot"]} '
            f'| {entry["max_function"]} | {entry.get("weight", "")} '
            f'| {entry.get("reach", "")} | {"yes" if entry["tested"] else ""} |')
    out += ['', '## Classes over 40 members', '']
    for path, entry in files.items():
        for item in entry['classes']:
            out.append(f'- {path}:{item["line"]} {item["name"]}: '
                       f'{item["methods"]} methods, {item["fields"]} fields')
    out += ['', '## Functions over 150 lines', '']
    for path, entry in files.items():
        for item in entry['long_functions']:
            out.append(f'- {path}:{item["line"]} {item["name"]}: {item["lines"]}')
    out += ['', '## Clones touching the area', '']
    for clone in snap['clones']:
        left, right = clone['left'], clone['right']
        if inside(left['path']) or inside(right['path']):
            out.append(f'- {clone["lines"]} lines: {left["path"]}:{left["start"]} '
                       f'<-> {right["path"]}:{right["start"]}')
    out += ['', '## Layering violations', '']
    for item in snap['layering']:
        if inside(item['path']) or inside(item['include']):
            out.append(f'- {item["path"]}:{item["line"]} ({item["from"]}) -> '
                       f'{item["include"]} ({item["to"]})')
    out += ['', '## Unused-function candidates', '']
    for item in snap['unused']:
        if inside(item['path']):
            out.append(f'- {item["path"]}:{item["line"]} {item["name"]} ({item["lines"]} lines)')
    tasks = [
        (task, entry) for task, entry in snap.get('tasks', {}).items()
        if any(inside(p) for p in entry['paths'])
    ]
    if tasks:
        hours = sorted(entry['hours'] for _, entry in tasks)
        out += ['', '## Agent cost', '',
                f'{len(tasks)} tasks, median {hours[len(hours) // 2]} h, '
                f'total {sum(hours):.0f} h.']
    print('\n'.join(out))
    return 0


def command_check(args):
    tree = sources.from_worktree(args.repo)
    if args.staged:
        for status, path in review.changed(tree.repo, args.base, True):
            if status == 'D':
                tree.files.pop(path, None)
            else:
                tree.files[path] = sources.git(
                    tree.repo, 'show', f':{path}').decode('utf-8', 'replace')
    errors, warnings = review.check(tree, args.base, args.staged, args.strict)
    for title, items in (('Errors', errors), ('Warnings', warnings)):
        if items:
            print(f'## {title}\n')
            for item in items:
                print(f'- {item}')
            print()
    if not errors and not warnings:
        print('Code health check: clean.')
    return 1 if errors else 0


def command_verify(args):
    """Each finding is `present` in the working tree, `resolved` (present at
    the base, gone now), or `unknown` (not found at the base either: an
    already resolved finding, a changed clone identity, or a typo). A clone
    that is gone but left a new clone in the same files is `changed`."""
    before = report.findings(report.snapshot(sources.from_ref(args.repo, args.base)))
    after = report.findings(report.snapshot(sources.from_worktree(args.repo)))
    status = 0
    for identity in args.ids:
        if identity in after:
            status = max(status, 1)
            print(f'present  {identity}: {after[identity]["title"]}')
        elif identity not in before:
            status = max(status, 2)
            print(f'unknown  {identity}: not found at {args.base}; read the code')
        else:
            paths = set(before[identity]['paths'])
            successors = [
                finding for key, finding in after.items()
                if before[identity]['kind'] == 'clone' == finding['kind']
                and key not in before and paths & set(finding['paths'])
            ]
            if successors:
                status = max(status, 1)
                for finding in successors:
                    print(f'changed  {identity} -> {finding["id"]}: {finding["title"]}')
            else:
                print(f'resolved {identity}')
    return status


def command_moves(args):
    result = review.moves(args.repo, args.base, args.head, args.staged)
    print(f'Moved lines: {result["moved_lines"]}')
    print(f'Added declarations: {result["declaration_lines"]}')
    print(f'Verdict: {"MOVE_ONLY" if result["move_only"] else "NOT_MOVE_ONLY"}')
    for title, items in (('Added lines that were not moved', result['substantive_novel']),
                         ('Removed lines that did not reappear', result['lost'])):
        if items:
            print(f'\n## {title}\n')
            for text, count in items[:200]:
                print(f'- {count}x {text}')
    return 0 if result['move_only'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--repo', default='.')
    commands = parser.add_subparsers(dest='command', required=True)
    scan = commands.add_parser('scan')
    scan.add_argument('--ref')
    scan.add_argument('--fetch', action='store_true')
    scan.add_argument('--queue', default='../ai-tdesktop')
    area = commands.add_parser('area')
    area.add_argument('area')
    check = commands.add_parser('check')
    check.add_argument('--base', default='HEAD')
    check.add_argument('--staged', action='store_true')
    check.add_argument('--strict', action='store_true')
    moves = commands.add_parser('moves')
    moves.add_argument('--base', default='HEAD')
    moves.add_argument('--head')
    moves.add_argument('--staged', action='store_true')
    commands.add_parser('sync')
    atlas_parser = commands.add_parser('atlas')
    atlas_parser.add_argument('--ref')
    atlas_parser.add_argument('--fetch', action='store_true')
    atlas_parser.add_argument('--queue', default='../ai-tdesktop')
    atlas_parser.add_argument('--check')
    verify = commands.add_parser('verify')
    verify.add_argument('--base', default='HEAD')
    verify.add_argument('ids', nargs='+')
    args = parser.parse_args()
    handlers = {
        'scan': command_scan,
        'area': command_area,
        'check': command_check,
        'moves': command_moves,
        'verify': command_verify,
        'sync': command_sync,
        'atlas': command_atlas,
    }
    try:
        return handlers[args.command](args)
    except subprocess.CalledProcessError as error:
        message = error.stderr.decode('utf-8', 'replace') if error.stderr else str(error)
        print('git failed: ' + message.strip(), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
