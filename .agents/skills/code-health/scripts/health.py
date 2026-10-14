"""Code health: measure the codebase, keep a findings ledger, and check
changes. Run from the repository root.

  scan     [--ref origin/dev] [--fetch] [--queue ../ai-tdesktop] [--state DIR] [--force]
  worktree [--ref COMMIT] | --remove PATH
  atlas    [--ref origin/dev] [--check FILE] [--state DIR]
  area     <path-prefix-or-area>
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


def published_state(args):
    """The code-health state published in the main ai-tdesktop worktree."""
    return Path(args.queue) / 'code-health'


def snapshot_cache(args, commit):
    return published_state(args) / '.local' / 'snapshots' / f'{commit}.json'


def cache_snapshot(args, snap):
    """Keep a commit's raw scan in the ignored cache beside the published
    state, when ai-tdesktop is present."""
    if Path(args.queue).is_dir():
        path = snapshot_cache(args, snap['commit'])
        path.parent.mkdir(parents=True, exist_ok=True)
        write_atomic(path, json.dumps(snap) + '\n')


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


def fetch_dev(repo):
    sources.git(repo, 'fetch', '--no-tags', '--no-recurse-submodules',
                'origin', '+refs/heads/dev:refs/remotes/origin/dev')


def ensure_commit(repo, commit):
    """Fetch origin dev when a published scan's commit is not in this clone."""
    present = subprocess.run(['git', '-C', str(repo), 'cat-file', '-e', commit + '^{commit}'],
                             capture_output=True).returncode == 0
    if not present:
        fetch_dev(repo)


def tree_for(args):
    if args.ref:
        if args.fetch:
            fetch_dev(args.repo)
        return sources.from_ref(args.repo, args.ref)
    return sources.from_worktree(args.repo)


def command_scan(args):
    state = Path(args.state) if args.state else None
    if state and not args.ref:
        print('Only a scan of an exact --ref is recorded in the ledger; '
              'scan the working tree without --state.', file=sys.stderr)
        return 2
    source = state or published_state(args)
    previous = load(source / 'ledger.json')
    now = datetime.datetime.now(datetime.timezone.utc)
    if (state and previous and not args.force
            and previous['checked_at_utc'][:10] == now.date().isoformat()):
        print(json.dumps({
            'skipped': 'code health already ran today',
            'commit': previous['commit'],
            'run_directory': str(state / previous['run']),
        }, indent=2))
        return 0
    tree = tree_for(args)
    queue = args.queue if args.queue and Path(args.queue).is_dir() else None
    snap = report.snapshot(tree, queue)
    current = report.findings(snap)
    ledger = report.merge(previous, current, snap['checked_at_utc'])
    for identity in (load(source / 'ignored.json') or {}):
        if identity in ledger and ledger[identity]['status'] == 'open':
            ledger[identity]['status'] = 'ignored'
    text = report.markdown(snap, ledger, previous)
    if state:
        run = state / 'runs' / f'{now.strftime("%Y%m%dT%H%M%SZ")}-{tree.commit[:10]}'
        run.mkdir(parents=True, exist_ok=True)
    else:
        run = Path(tempfile.mkdtemp(prefix='code-health-'))
    (run / 'report.md').write_text(text, encoding='utf-8')
    if state:
        cache_snapshot(args, snap)
        latest = {
            'commit': snap['commit'],
            'ref': snap['ref'],
            'checked_at_utc': snap['checked_at_utc'],
            'run': run.relative_to(state).as_posix(),
            'totals': snap['totals'],
            'findings': ledger,
        }
        write_atomic(state / 'ledger.json',
                     json.dumps(latest, indent=1, sort_keys=True) + '\n')
        with open(state / 'history.jsonl', 'a', encoding='utf-8') as stream:
            stream.write(json.dumps({
                'checked_at_utc': snap['checked_at_utc'],
                'commit': snap['commit'],
                'totals': snap['totals'],
            }, sort_keys=True) + '\n')
    else:
        (run / 'snapshot.json').write_text(json.dumps(snap, indent=1) + '\n', encoding='utf-8')
    opened = sum(1 for f in ledger.values() if f['status'] == 'open')
    print(json.dumps({
        'commit': snap['commit'],
        'ref': snap['ref'],
        'run_directory': str(run),
        'ledger': str(state / 'ledger.json') if state else None,
        'open_findings': opened,
        'totals': snap['totals'],
    }, indent=2))
    return 0


WORKTREE_PREFIX = 'code-health-source-'


def command_worktree(args):
    """A temporary detached checkout of the scanned commit, with the lib_*
    submodules the scan reads, for surveys and audits."""
    repo = Path(args.repo)
    if args.remove:
        path = Path(args.remove).resolve()
        if not path.name.startswith(WORKTREE_PREFIX):
            print(f'Not a code-health worktree: {path}', file=sys.stderr)
            return 1
        sources.git(repo, 'worktree', 'remove', '--force', str(path))
        print(json.dumps({'removed': str(path)}, indent=2))
        return 0
    ref = args.ref
    if not ref:
        latest = load(published_state(args) / 'ledger.json')
        if not latest:
            print('No published scan yet; pass --ref.', file=sys.stderr)
            return 1
        ref = latest['commit']
        ensure_commit(repo, ref)
    commit = sources.git(repo, 'rev-parse', '--verify', ref + '^{commit}').decode().strip()
    path = Path(tempfile.gettempdir()).resolve() / f'{WORKTREE_PREFIX}{commit[:10]}'
    if not path.exists():
        sources.git(repo, 'worktree', 'add', '--detach', '--quiet', str(path), commit)
    head = sources.git(path, 'rev-parse', 'HEAD').decode().strip()
    if head != commit:
        print(f'{path} exists at {head[:10]}, not {commit[:10]}; remove it first.',
              file=sys.stderr)
        return 1
    raw = sources.git(path, 'ls-tree', 'HEAD', '--', 'Telegram/').decode()
    libs = [
        line.split('\t', 1)[1] for line in raw.splitlines()
        if line.split()[1] == 'commit' and line.split('\t', 1)[1].startswith('Telegram/lib_')
    ]
    if libs:
        sources.git(path, 'submodule', 'update', '--init', '--', *libs)
    print(json.dumps({'path': str(path), 'commit': commit, 'submodules': libs}, indent=2))
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
    if args.state:
        directory = Path(args.state) / 'atlas' / f'{stamp}-{tree.commit[:10]}'
        directory.mkdir(parents=True, exist_ok=True)
    else:
        directory = Path(tempfile.mkdtemp(prefix='code-health-atlas-'))
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
    latest = load(published_state(args) / 'ledger.json')
    if not latest:
        print('No published scan yet; see the code-health skill.', file=sys.stderr)
        return 1
    snap = load(snapshot_cache(args, latest['commit']))
    if not snap:
        ensure_commit(args.repo, latest['commit'])
        tree = sources.from_ref(args.repo, latest['commit'])
        snap = report.snapshot(tree, args.queue if Path(args.queue).is_dir() else None)
        cache_snapshot(args, snap)
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
    scan.add_argument('--state')
    scan.add_argument('--force', action='store_true')
    worktree = commands.add_parser('worktree')
    worktree.add_argument('--ref')
    worktree.add_argument('--remove')
    worktree.add_argument('--queue', default='../ai-tdesktop')
    area = commands.add_parser('area')
    area.add_argument('area')
    area.add_argument('--queue', default='../ai-tdesktop')
    check = commands.add_parser('check')
    check.add_argument('--base', default='HEAD')
    check.add_argument('--staged', action='store_true')
    check.add_argument('--strict', action='store_true')
    moves = commands.add_parser('moves')
    moves.add_argument('--base', default='HEAD')
    moves.add_argument('--head')
    moves.add_argument('--staged', action='store_true')
    atlas_parser = commands.add_parser('atlas')
    atlas_parser.add_argument('--ref')
    atlas_parser.add_argument('--fetch', action='store_true')
    atlas_parser.add_argument('--queue', default='../ai-tdesktop')
    atlas_parser.add_argument('--check')
    atlas_parser.add_argument('--state')
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
        'worktree': command_worktree,
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
