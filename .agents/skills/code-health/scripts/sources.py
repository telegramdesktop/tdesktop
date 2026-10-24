"""The scanned file set, read from the working tree or from an exact ref."""
import os
from pathlib import Path
import subprocess


SOURCE_ROOT = 'Telegram/SourceFiles'
LIB_PREFIX = 'Telegram/lib_'
LANG_FILE = 'Telegram/Resources/langs/lang.strings'
TOP_LEVEL = ('SourceFiles', 'cmake', 'CMakeLists.txt')
EXTENSIONS = (
    '.cpp', '.mm', '.c', '.cc', '.cxx', '.h', '.hpp', '.style', '.cmake',
)


def git(repo, *args, data=None):
    return subprocess.run(
        ['git', '-C', str(repo), *args],
        input=data,
        capture_output=True,
        check=True,
        timeout=600,
    ).stdout


def wanted(path):
    if path == LANG_FILE:
        return True
    if not (path.startswith((SOURCE_ROOT + '/', LIB_PREFIX, 'Telegram/cmake/'))
            or path == 'Telegram/CMakeLists.txt'):
        return False
    parts = path.split('/')
    if 'ThirdParty' in parts or '.git' in parts:
        return False
    if parts[-1] == 'AGENTS.md':
        return path.startswith(SOURCE_ROOT + '/')
    return path.endswith(EXTENSIONS) or parts[-1] == 'CMakeLists.txt'


def lib_roots(paths):
    """Telegram/lib_x directories, the include roots besides SourceFiles."""
    return sorted({
        '/'.join(path.split('/')[:2])
        for path in paths
        if path.startswith(LIB_PREFIX)
    })


class SourceTree:
    def __init__(self, repo, files, commit, ref, notes):
        self.repo = Path(repo)
        self.files = files
        self.commit = commit
        self.ref = ref
        self.notes = notes
        self.roots = [SOURCE_ROOT] + lib_roots(files)
        self.submodules = {}

    def lines(self, path):
        text = self.files.get(path)
        return 0 if text is None else text.count('\n') + 1

    def root_of(self, path):
        for root in self.roots:
            if path.startswith(root + '/'):
                return root
        return None


def from_worktree(repo):
    repo = Path(repo)
    files = {}
    tops = [
        name for name in os.listdir(repo / 'Telegram')
        if name in TOP_LEVEL or name.startswith('lib_')
    ]
    for top in tops:
        start = repo / 'Telegram' / top
        if start.is_file():
            walked = [(str(start.parent), [], [top])]
        else:
            walked = os.walk(start)
        for directory, subdirs, names in walked:
            relative = os.path.relpath(directory, repo).replace(os.sep, '/')
            subdirs[:] = [
                name for name in subdirs
                if name not in ('ThirdParty', '.git')
            ]
            for name in names:
                path = f'{relative}/{name}'
                if wanted(path):
                    text = read_text(repo / path)
                    if text is not None:
                        files[path] = text
    lang = read_text(repo / LANG_FILE)
    if lang is not None:
        files[LANG_FILE] = lang
    commit = git(repo, 'rev-parse', 'HEAD').decode().strip()
    dirty = bool(git(repo, 'status', '--porcelain', '--', 'Telegram').strip())
    notes = ['working tree' + (' with local changes' if dirty else '')]
    tree = SourceTree(repo, files, commit, 'WORKTREE', notes)
    tree.submodules = submodule_heads(repo)
    return tree


def from_ref(repo, ref):
    repo = Path(repo)
    commit = git(repo, 'rev-parse', '--verify', ref + '^{commit}').decode().strip()
    files = {}
    notes = []
    submodules = {}
    entries = list_tree(repo, commit, 'Telegram')
    blobs = []
    for mode, kind, oid, path in entries:
        if kind == 'commit' and path.startswith(LIB_PREFIX) and path.count('/') == 1:
            submodules[path] = oid
        elif kind == 'blob' and wanted(path):
            blobs.append((path, oid))
    files.update(read_blobs(repo, blobs))
    for path, oid in sorted(submodules.items()):
        sub = repo / path
        try:
            sub_entries = list_tree(sub, oid, '.')
        except subprocess.CalledProcessError:
            notes.append(f'{path} at {oid[:10]} unavailable locally, skipped')
            continue
        sub_blobs = [
            (f'{path}/{entry_path}', entry_oid)
            for _, kind, entry_oid, entry_path in sub_entries
            if kind == 'blob' and wanted(f'{path}/{entry_path}')
        ]
        files.update(read_blobs(sub, sub_blobs))
    tree = SourceTree(repo, files, commit, ref, notes)
    tree.submodules = submodules
    return tree


def list_tree(repo, commit, path):
    raw = git(repo, 'ls-tree', '-rz', commit, '--', path)
    result = []
    for entry in raw.split(b'\0'):
        if not entry:
            continue
        metadata, name = entry.split(b'\t', 1)
        mode, kind, oid = metadata.decode().split()
        result.append((mode, kind, oid, name.decode('utf-8')))
    return result


def read_blobs(repo, blobs):
    if not blobs:
        return {}
    request = ''.join(oid + '\n' for _, oid in blobs).encode()
    raw = git(repo, 'cat-file', '--batch', data=request)
    files = {}
    offset = 0
    for path, oid in blobs:
        end = raw.index(b'\n', offset)
        header = raw[offset:end].split()
        size = int(header[2])
        content = raw[end + 1:end + 1 + size]
        offset = end + 1 + size + 1
        text = decode(content)
        if text is not None:
            files[path] = text
    return files


def submodule_heads(repo):
    result = {}
    raw = git(repo, 'submodule', 'status', '--', 'Telegram')
    for line in raw.decode().splitlines():
        parts = line[1:].split()
        if len(parts) >= 2 and parts[1].startswith(LIB_PREFIX):
            result[parts[1]] = parts[0]
    return result


def read_text(path):
    """A working file's CRLF is read as LF, the form Git stores and diffs, so
    results do not depend on the checkout's line endings."""
    try:
        text = decode(path.read_bytes())
    except OSError:
        return None
    return None if text is None else text.replace('\r\n', '\n')


def decode(raw):
    if b'\0' in raw:
        return None
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('latin-1')
