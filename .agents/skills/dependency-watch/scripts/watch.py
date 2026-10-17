import argparse
import datetime
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


REQUIRED = {
    '.gitmodules',
    'Telegram/build/prepare/prepare.py',
    'Telegram/build/qt_version.py',
    'Telegram/build/docker/centos_env/Dockerfile',
    'snap/snapcraft.yaml',
}
MANIFEST_NAMES = {
    '.gitmodules', 'CMakeLists.txt', 'Cargo.toml', 'Cargo.lock',
    'DEPS', 'Dockerfile', 'package.json', 'package-lock.json',
    'pyproject.toml', 'poetry.lock', 'requirements.txt', 'vcpkg.json',
}
CANDIDATE = re.compile(
    r'https?://|git (?:clone|checkout|fetch|submodule)|'
    r'source(?:-tag|-branch|-commit|-checksum)?:|'
    r'build-packages:|stage-packages:|stage-snaps:|build-snaps:|'
    r'\b(?:FROM|QT|rustToolchain)\b|\bpip(?:3)? install\b|'
    r'\b(?:dnf|apt|apt-get|pacman)\b|^\s*uses:')
STABLE_VERSION = re.compile(r'(?:v|n|openssl-)?(\d+)\.(\d+)\.(\d+)')


def git(repo, *args):
    result = subprocess.run(
        ['git', '-C', str(repo), *args],
        capture_output=True,
        check=True,
        timeout=180,
    )
    return result.stdout


def compare(current, candidate):
    parsed = [STABLE_VERSION.fullmatch(value) for value in (current, candidate)]
    if not all(parsed):
        return 'unknown'
    before, after = [tuple(map(int, value.groups())) for value in parsed]
    if after == before:
        return 'same'
    if after < before:
        return 'older'
    if after[0] != before[0]:
        return 'major'
    if after[1] != before[1]:
        return 'minor'
    return 'patch'


def selected(path):
    return (
        path in REQUIRED
        or path.startswith(('Telegram/build/', 'snap/', '.github/'))
        or Path(path).name in MANIFEST_NAMES
    )


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def snapshot(repo, fetch=True):
    repo = Path(git(repo, 'rev-parse', '--show-toplevel').decode().strip())
    if fetch:
        git(
            repo, 'fetch', '--no-tags', '--no-recurse-submodules', 'origin',
            '+refs/heads/dev:refs/remotes/origin/dev',
        )
    checked_at = datetime.datetime.now(datetime.timezone.utc)
    commit = git(
        repo, 'rev-parse', '--verify', 'refs/remotes/origin/dev^{commit}',
    ).decode().strip()
    tree = git(repo, 'ls-tree', '-rz', commit)
    blobs = []
    gitlinks = []
    for entry in tree.split(b'\0'):
        if not entry:
            continue
        metadata, raw_path = entry.split(b'\t', 1)
        mode, kind, oid = metadata.decode().split()
        path = raw_path.decode('utf-8')
        item = {'path': path, 'object': oid}
        if kind == 'commit':
            gitlinks.append(item)
        elif selected(path):
            blobs.append({**item, 'mode': mode})
    present = {item['path'] for item in blobs if item['mode'] in ('100644', '100755')}
    missing = sorted(REQUIRED - present)
    if missing:
        raise ValueError('Required manifests missing or not regular files: ' + ', '.join(missing))

    common = Path(git(repo, 'rev-parse', '--git-common-dir').decode().strip())
    if not common.is_absolute():
        common = repo / common
    storage = common.resolve() / 'dependency-watch'
    runs = storage / 'runs'
    runs.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(
        prefix=checked_at.strftime('%Y%m%dT%H%M%SZ-'), dir=runs,
    ))
    sources = run / 'sources'
    files = []
    skipped = []
    candidates = []
    for item in blobs:
        path = item['path']
        if item['mode'] not in ('100644', '100755'):
            skipped.append({**item, 'reason': 'not a regular file'})
            continue
        raw = git(repo, 'cat-file', 'blob', item['object'])
        try:
            content = raw.decode('utf-8')
        except UnicodeDecodeError:
            skipped.append({**item, 'reason': 'not UTF-8 text'})
            continue
        if '\0' in content:
            skipped.append({**item, 'reason': 'binary content'})
            continue
        target = sources / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        files.append(item)
        for number, line in enumerate(content.splitlines(), 1):
            if CANDIDATE.search(line):
                candidates.append({'path': path, 'line': number, 'text': line.strip()})
    unreadable = REQUIRED - {item['path'] for item in files}
    if unreadable:
        raise ValueError('Required manifests unreadable: ' + ', '.join(sorted(unreadable)))
    data = {
        'schema_version': 1,
        'repository': str(repo),
        'ref': 'origin/dev',
        'commit': commit,
        'checked_at_utc': checked_at.isoformat(),
        'fresh_fetch': fetch,
        'audit_status': 'snapshot-only',
        'storage_root': str(storage),
        'run_directory': str(run),
        'files': files,
        'gitlinks': gitlinks,
        'skipped': skipped,
        'coverage_note': 'Dependency and security review pending; gitlinks require recursive inspection.',
    }
    write_json(run / 'snapshot.json', data)
    write_json(run / 'candidates.json', candidates)
    return data


def main():
    parser = argparse.ArgumentParser(description='Snapshot dependency inputs from origin/dev.')
    commands = parser.add_subparsers(dest='command', required=True)
    snapshot_parser = commands.add_parser('snapshot')
    snapshot_parser.add_argument('--repo', default='.')
    snapshot_parser.add_argument('--no-fetch', action='store_true', help='Offline test only.')
    compare_parser = commands.add_parser('compare')
    compare_parser.add_argument('current')
    compare_parser.add_argument('candidate')
    args = parser.parse_args()
    if args.command == 'compare':
        result = {
            'current': args.current,
            'candidate': args.candidate,
            'classification': compare(args.current, args.candidate),
        }
    else:
        try:
            data = snapshot(args.repo, fetch=not args.no_fetch)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError, ValueError) as error:
            print('Snapshot failed; no fresh audit is available: ' + str(error), file=sys.stderr)
            return 1
        result = {key: data[key] for key in (
            'commit', 'fresh_fetch', 'audit_status', 'storage_root', 'run_directory',
        )}
        result['source_files'] = len(data['files'])
        result['gitlinks'] = len(data['gitlinks'])
        result['skipped_files'] = len(data['skipped'])
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
