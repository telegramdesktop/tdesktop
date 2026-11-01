"""The source atlas: named areas over path patterns, parsed from
Telegram/SourceFiles/AGENTS.md, and the data skeleton a draft starts from."""
import fnmatch
import posixpath
import re
import statistics

import cpp_text
from sources import git


ATLAS_PATH = 'Telegram/SourceFiles/AGENTS.md'
SPLIT_LINES = 30000
MIN_LINES = 3000
HEADING = re.compile(r'^###\s+(\S+)')
PATHS = re.compile(r'^-\s+Paths?:\s*(.*)$')
PATTERN = re.compile(r'`([^`]+)`')


def relative(path):
    return path[len('Telegram/'):] if path.startswith('Telegram/') else path


def match(pattern, path):
    """Specificity of `pattern` for a path relative to Telegram/, or None:
    an exact file beats a glob, which beats a directory prefix; longer
    patterns beat shorter ones of the same kind."""
    if pattern.endswith('/'):
        return (1, len(pattern)) if path.startswith(pattern) else None
    if any(char in pattern for char in '*?['):
        return (2, len(pattern)) if fnmatch.fnmatchcase(path, pattern) else None
    return (3, len(pattern)) if path == pattern else None


class Atlas:
    def __init__(self, text):
        self.areas = {}
        current = None
        for line in text.splitlines():
            heading = HEADING.match(line)
            if heading:
                current = heading.group(1)
                continue
            paths = PATHS.match(line)
            if paths and current:
                self.areas.setdefault(current, []).extend(
                    PATTERN.findall(paths.group(1)))
        self.cache = {}

    def classify(self, path):
        """(area, other areas matching with the same specificity)."""
        if path in self.cache:
            return self.cache[path]
        target = relative(path)
        best = None
        tied = []
        for name, patterns in self.areas.items():
            score = max((match(p, target) or (0, 0) for p in patterns), default=(0, 0))
            if score == (0, 0):
                continue
            if best is None or score > best[0]:
                best = (score, name)
                tied = []
            elif score == best[0] and name != best[1]:
                tied.append(name)
        result = (best[1] if best else None, tied)
        self.cache[path] = result
        return result

    def area_of(self, path):
        return self.classify(path)[0]


def load(tree, text=None):
    text = text if text is not None else tree.files.get(ATLAS_PATH)
    if not text:
        return None
    atlas = Atlas(text)
    return atlas if atlas.areas else None


def mapped_file(path):
    return ((path.startswith('Telegram/SourceFiles/') or path.startswith('Telegram/lib_'))
            and path.endswith(cpp_text.EXTENSIONS_TU + cpp_text.EXTENSIONS_HEADER + ('.style',)))


def check(atlas, tree):
    """Files no area matches, files two areas claim equally, and patterns
    that match no file."""
    files = sorted(p for p in tree.files if mapped_file(p))
    unmapped = []
    ambiguous = []
    for path in files:
        area, tied = atlas.classify(path)
        if area is None:
            unmapped.append(path)
        elif tied:
            ambiguous.append((path, [area] + tied))
    targets = [relative(p) for p in files]
    unused = [
        (name, pattern)
        for name, patterns in atlas.areas.items()
        for pattern in patterns
        if not any(match(pattern, target) for target in targets)
    ]
    return {'unmapped': unmapped, 'ambiguous': ambiguous, 'unused': unused}


def candidates(files):
    """Candidate area directories: every top-level directory of each source
    root; a directory over SPLIT_LINES splits into its subdirectories of at
    least MIN_LINES, keeping the rest as its own area."""
    totals = {}
    children = {}
    for path, lines in files.items():
        folder = posixpath.dirname(path)
        while folder not in ('Telegram', ''):
            totals[folder] = totals.get(folder, 0) + lines
            parent = posixpath.dirname(folder)
            children.setdefault(parent, set()).add(folder)
            folder = parent
    result = set()

    def decide(folder):
        result.add(folder)
        if totals.get(folder, 0) <= SPLIT_LINES:
            return
        for child in children.get(folder, ()):
            if totals[child] >= MIN_LINES:
                decide(child)

    for root in sorted(children.get('Telegram', ())):
        if root == 'Telegram/SourceFiles':
            result.add(root)
            for child in children.get(root, ()):
                decide(child)
        elif root.startswith('Telegram/lib_'):
            decide(root)
    return result, totals


def owner(path, folders):
    folder = posixpath.dirname(path)
    while folder and folder not in folders:
        folder = posixpath.dirname(folder)
    return folder


def name_groups(paths, lines):
    """Line totals by file-name stem token, after a prefix most files share."""
    stems = [posixpath.splitext(posixpath.basename(p))[0] for p in paths]
    common = posixpath.commonprefix(stems)
    common = common[:common.rfind('_') + 1] if '_' in common else ''
    groups = {}
    for path, stem in zip(paths, stems):
        token = stem[len(common):].split('_')[0] or stem
        groups[token] = groups.get(token, 0) + lines[path]
    return sorted(groups.items(), key=lambda kv: -kv[1])


def co_change(tree, area_of, days=180):
    raw = git(tree.repo, 'log', tree.commit, f'--since={days}.days', '--no-merges',
              '--format=tformat:@', '--name-only', '--', 'Telegram/SourceFiles').decode(
                  'utf-8', 'replace')
    commits = []
    for line in raw.splitlines():
        if line == '@':
            commits.append(set())
        elif line and commits and mapped_file(line):
            commits[-1].add(area_of(line))
    single = {}
    pairs = {}
    for areas in commits:
        for a in areas:
            single[a] = single.get(a, 0) + 1
            for b in areas:
                if a < b:
                    pairs[(a, b)] = pairs.get((a, b), 0) + 1
    result = {}
    for (a, b), together in pairs.items():
        jaccard = together / (single[a] + single[b] - together)
        result.setdefault(a, []).append((b, jaccard, together))
        result.setdefault(b, []).append((a, jaccard, together))
    alone = {}
    for areas in commits:
        if len(areas) == 1:
            area = next(iter(areas))
            alone[area] = alone.get(area, 0) + 1
    return result, single, alone


def skeleton(tree, snap, graph):
    files = {p: e['lines'] for p, e in snap['files'].items() if mapped_file(p)}
    for path in tree.files:
        if path.endswith('.style') and mapped_file(path):
            files[path] = tree.lines(path)
    folders, totals = candidates(files)

    def area_of(path):
        return owner(path, folders)

    members = {}
    for path in files:
        members.setdefault(area_of(path), []).append(path)
    edges = {}
    for path, targets in graph.edges.items():
        if path not in files:
            continue
        source = area_of(path)
        for _, target in targets:
            if target in files:
                destination = area_of(target)
                if destination != source:
                    key = (source, destination)
                    edges[key] = edges.get(key, 0) + 1
    partners, commits, alone = co_change(tree, area_of)
    tasks = {}
    for entry in snap.get('tasks', {}).values():
        for area in {area_of(p) for p in entry['paths'] if p in files}:
            tasks.setdefault(area, []).append(entry['hours'])
    result = []
    for folder in sorted(members):
        paths = sorted(members[folder])
        lines = sum(files[p] for p in paths)
        info = [snap['files'].get(p, {}) for p in paths]
        targets = {}
        for entry in info:
            if entry.get('target'):
                targets[entry['target']] = targets.get(entry['target'], 0) + 1
        hours = tasks.get(folder, [])
        own = [p for p in paths if posixpath.dirname(p) == folder]
        result.append({
            'folder': folder,
            'lines': lines,
            'files': len(paths),
            'target': max(targets, key=targets.get) if targets else None,
            'largest': sorted(((files[p], p) for p in paths), reverse=True)[:5],
            'groups': name_groups(own, files)[:8] if lines > 15000 else [],
            'uses': sorted(((n, d) for (s, d), n in edges.items() if s == folder), reverse=True)[:8],
            'used_by': sorted(((n, s) for (s, d), n in edges.items() if d == folder), reverse=True)[:8],
            'changes_with': sorted(
                ((j, b, n) for b, j, n in partners.get(folder, []) if j >= 0.05),
                reverse=True)[:6],
            'commits': commits.get(folder, 0),
            'alone': alone.get(folder, 0),
            'churn': sum(e.get('churn', 0) for e in info),
            'tasks': len(hours),
            'task_hours_median': round(statistics.median(hours), 1) if hours else None,
            'tested_lines': sum(files[p] for p, e in zip(paths, info) if e.get('tested')),
        })
    return result


def short(folder):
    return relative(folder) + '/'


def markdown(rows):
    out = [
        '# Atlas skeleton',
        '',
        f'{len(rows)} candidate areas from the directory tree: a directory over '
        f'{SPLIT_LINES:,} lines splits into its subdirectories of at least '
        f'{MIN_LINES:,} lines; the rest stays with it. Paths are relative to '
        '`Telegram/`; a candidate owns its directory minus the candidates '
        'nested in it.',
    ]
    for row in sorted(rows, key=lambda r: r['folder']):
        tested = f'{row["tested_lines"] / row["lines"]:.0%}' if row['lines'] else '0%'
        if row['folder'].startswith('Telegram/lib_'):
            history = (f'{row["churn"]} file changes in 180 days in its own '
                       'repository, so co-change is not measured')
        else:
            alone = f'{row["alone"] / row["commits"]:.0%}' if row['commits'] else 'n/a'
            history = (f'{row["commits"]} commits in 180 days, {alone} touching '
                       'no other candidate')
        out += ['', f'### `{short(row["folder"])}`', '',
                f'- {row["lines"]:,} lines in {row["files"]} files; target '
                f'{row["target"] or "none"}; {history}; tested {tested}.']
        if row['tasks']:
            out.append(f'- Agent cost: {row["tasks"]} tasks, median '
                       f'{row["task_hours_median"]} h.')
        out.append('- Largest: ' + ', '.join(
            f'{posixpath.basename(p)} {n:,}' for n, p in row['largest']))
        if row['groups']:
            out.append('- Name groups (own files): ' + ', '.join(
                f'{g} {n:,}' for g, n in row['groups']))
        if row['uses']:
            out.append('- Includes from: ' + ', '.join(
                f'{short(d)} {n}' for n, d in row['uses']))
        if row['used_by']:
            out.append('- Included by: ' + ', '.join(
                f'{short(s)} {n}' for n, s in row['used_by']))
        if row['changes_with']:
            out.append('- Changes with: ' + ', '.join(
                f'{short(b)} {j:.2f}' for j, b, _ in row['changes_with']))
        if row['lines'] < MIN_LINES:
            out.append('- Small: consider joining the area it includes or changes with most.')
    return '\n'.join(out) + '\n'
