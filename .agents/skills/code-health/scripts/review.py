"""Diff-time checks: the ratchet a change must pass, and move-only proof.

The ratchet compares a change with its own base, so it needs no stored
baseline: oversized files may not grow beyond a small fix, new files may not start oversized,
a new include may not cross a target boundary, a header may not get much
heavier to rebuild, added code may not repeat code that already exists, a
removed definition may not leave mentions behind, and a removed use may not
leave a definition nothing uses."""
from collections import Counter
from pathlib import Path
import re
import subprocess

import clones
import cmake_model
import cpp_text
import includes
import metrics
from report import LIMITS, file_limit
from sources import LANG_FILE, git, wanted


HUNK = re.compile(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@')
SCAFFOLDING = re.compile(
    r'^(#include .*|#pragma once|namespace [\w:]+ \{|\} // namespace.*|'
    r'namespace \{|\{|\}|\};|(class|struct) \w+;|public:|private:|protected:|'
    r'using [\w:]+ = [\w:<>, *&]+;|using namespace [\w:]+;|'
    r'[\w/.-]+\.(cpp|h|mm|style|cmake)|'
    r'This file is part of.*|the official desktop application.*|'
    r'For license and copyright.*|https://github.com/.*LEGAL|/\*|\*/|)$')
BUILD_SHARE = 0.002
GROWTH_TOLERANCE = 20
PREFIXES = re.compile(r'^(\[\[nodiscard\]\] |static |inline )+')
DEFAULTS = re.compile(r'([\w>*&] [*&]?\w+) = [^,(){}]+(?=[,)])')
CONTROL = ('if ', 'for ', 'while ', 'switch ', 'return ', '} else')
DECLARATION = re.compile(r'^(?!return )[\w:<>*&\[\], ]+ [*&]?~?\w+\(')
NOT_TYPE = r'(?!(?:return|co_return|else|case|throw|new|delete|co_await|not|and|or)\b)'
TOKEN = re.compile(r"\.?[0-9](?:[eEpP][+-]|[\w.]|'\w)*|([A-Za-z_]\w*)")
CODE_WORDS = frozenset('''
    auto bool break case catch char class const constexpr continue default
    delete double else enum explicit false float for friend if inline int long
    mutable namespace new noexcept nullptr operator override private protected
    public return short signed sizeof static struct switch template this throw
    true try typedef typename union unsigned using virtual void volatile while
    final int64 uint64 int32 uint32 float64 QString const_cast static_cast
    reinterpret_cast dynamic_cast std base not_null crl rpl
'''.split())


def code_path(path):
    return wanted(path) and path.endswith(
        cpp_text.EXTENSIONS_TU + cpp_text.EXTENSIONS_HEADER)


def tracked_path(path):
    """Code plus the files that define what code names: styles and keys."""
    return code_path(path) or (wanted(path) and path.endswith('.style')) or path == LANG_FILE


def untracked(repo):
    raw = git(repo, 'ls-files', '--others', '--exclude-standard', '--',
              'Telegram').decode('utf-8', 'replace')
    return [path for path in raw.splitlines() if tracked_path(path)]


def changed(repo, base, staged):
    """(status letter, path) of changed code, style and language files;
    without `staged`, untracked files count as added, so a change is seen
    before its commit."""
    args = ['diff', '--name-status', '--no-renames']
    args += ['--cached', base] if staged else [base]
    raw = git(repo, *args, '--', 'Telegram').decode('utf-8', 'replace')
    result = []
    for line in raw.splitlines():
        status, _, path = line.partition('\t')
        if tracked_path(path):
            result.append((status[0], path))
    if not staged:
        result += [('A', path) for path in untracked(repo)]
    return result


def diff_lines(repo, base, staged):
    """{path: (removed lines, added lines)} over the changed files."""
    args = ['diff', '-U0', '--no-color', '--no-renames']
    args += ['--cached', base] if staged else [base]
    raw = git(repo, *args, '--', 'Telegram').decode('utf-8', 'replace')
    result = {}
    path = None
    for line in raw.splitlines():
        if line.startswith('--- '):
            path = line[6:] if line.startswith('--- a/') else None
            continue
        if line.startswith('+++ '):
            path = line[6:] if line.startswith('+++ b/') else path
            continue
        if not path or not tracked_path(path) or not line or line[0] not in '+-':
            continue
        entry = result.setdefault(path, ([], []))
        entry[0 if line[0] == '-' else 1].append(line[1:])
    if not staged:
        for path in untracked(repo):
            text = (Path(repo) / path).read_text(encoding='utf-8', errors='replace')
            result[path] = ([], text.split('\n'))
    return result


def base_text(repo, base, path):
    try:
        return git(repo, 'show', f'{base}:{path}').decode('utf-8', 'replace')
    except subprocess.CalledProcessError:
        return None


def added_hunks(repo, base, staged, path):
    args = ['diff', '-U0', '--no-color']
    args += ['--cached', base] if staged else [base]
    raw = git(repo, *args, '--', path).decode('utf-8', 'replace')
    hunk = []
    number = 0
    for line in raw.splitlines():
        match = HUNK.match(line)
        if match:
            number = int(match.group(1))
            continue
        if line.startswith('+') and not line.startswith('+++'):
            hunk.append((number, line[1:]))
            number += 1
    return hunk


def check(tree, base='HEAD', staged=False, strict=False):
    """Returns (errors, warnings) as lists of strings."""
    errors = []
    warnings = []
    model = cmake_model.build(tree.files)
    graph = includes.IncludeGraph(tree, model)
    costs = None
    added_code = []
    crossings = []
    for status, path in changed(tree.repo, base, staged):
        if status == 'D' or not code_path(path):
            continue
        head = tree.files.get(path)
        if head is None:
            continue
        before = base_text(tree.repo, base, path)
        head_lines = head.count('\n') + 1
        limit = file_limit(path)
        if before is None:
            if head_lines > limit:
                errors.append(f'{path}: new file starts at {head_lines} lines (limit {limit}).')
        else:
            base_lines = before.count('\n') + 1
            if head_lines > limit and head_lines - base_lines > GROWTH_TOLERANCE:
                message = (f'{path}: grows {base_lines} -> {head_lines} lines while over '
                           f'the {limit}-line limit; put the new code in a new file.')
                (errors if strict else warnings).append(message)
            grown_classes(path, before, head, warnings)
        base_includes = set()
        if before is not None:
            base_includes = {
                graph.resolve(path, include)
                for _, include in cpp_text.includes(before)
            }
        source = model.target_of(path)
        fresh = []
        for number, target in graph.edges.get(path, ()):
            if target in base_includes:
                continue
            fresh.append(target)
            owner = model.target_of(target)
            if source and owner and owner not in model.dependencies(source):
                crossings.append((path, number, target, source, owner))
        if fresh and cpp_text.is_header(path):
            if costs is None:
                costs = graph.costs()
            heavier(path, fresh, graph, costs, warnings)
        hunks = added_hunks(tree.repo, base, staged, path)
        if not hunks and before is None:
            hunks = list(enumerate(head.split('\n'), 1))
        added_code.append((path, hunks))
    if crossings:
        new = {(path, target) for path, _, target, _, _ in crossings}
        existing = {}
        for item in graph.layering():
            if (item['path'], item['include']) not in new:
                pair = (item['from'], item['to'])
                existing[pair] = existing.get(pair, 0) + 1
        for path, number, target, source, owner in crossings:
            count = existing.get((source, owner), 0)
            if count:
                warnings.append(
                    f'{path}:{number}: includes {target} of target {owner}, which '
                    f'{source} does not link ({count} such includes already exist).')
            else:
                errors.append(
                    f'{path}:{number}: includes {target} of target {owner}, '
                    f'which {source} does not link.')
    for copy in clones.copied_from(added_code, tree.files):
        message = (f'{copy["path"]}:{copy["start"]}-{copy["end"]}: repeats '
                   f'{copy["lines"]} lines of {copy["source"]["path"]}:'
                   f'{copy["source"]["start"]}; reuse or promote it instead.')
        (errors if strict else warnings).append(message)
    for name, places in stranded(tree, base, staged):
        where = ', '.join(f'{path}:{number}' for path, number in places[:5])
        more = f' and {len(places) - 5} more' if len(places) > 5 else ''
        warnings.append(
            f'{name}: definition removed but still mentioned at {where}{more}; '
            f'remove what is left, or keep it if code this host does not '
            f'compile calls it.')
    for name, places in orphaned(tree, base, staged):
        where = ', '.join(f'{path}:{number}' for path, number in places[:5])
        warnings.append(
            f'{name}: the change removed its last use; only its definition '
            f'remains at {where}; remove it too.')
    return errors, warnings


CLASS_HEAD = re.compile(
    r'^(?:template\s*<.*>\s*)?(?:class|struct|union|enum(?:\s+class)?)\s+'
    r'(?:\[\[[^\]]*\]\]\s*)?([\w:]+)')
STYLE_DEFINITION = re.compile(r'^(\w+)\s*:', re.M)
LANG_DEFINITION = re.compile(r'^"(\w+?)(?:#\w+)?"\s*=', re.M)


def definitions(text, path=''):
    """(short name, qualifier or None) of column-0 functions and classes, of
    top-level styles in a `.style` file, or of keys in the language file."""
    if path == LANG_FILE:
        return {(name, None) for name in LANG_DEFINITION.findall(text)}
    if path.endswith('.style'):
        return {(name, None) for name in STYLE_DEFINITION.findall(text)}
    result = set()
    for item in cpp_text.functions(text):
        parts = item.name.split('::')
        result.add((parts[-1], parts[-2] if len(parts) > 1 else None))
    return result | {(item.name, None) for item in cpp_text.classes(text)}


def mention_lines(text):
    """Lines with comments blanked, numbering preserved."""
    return [line.split('//')[0] for line in cpp_text.code_lines(text)]


def identifiers(text):
    """Names in `text`, in order; letters inside a numeric literal such as
    `1e-4`, `0x1F`, `10ull` or `0.5f` belong to the number."""
    return [name for name in TOKEN.findall(text) if name]


def defines(path, line, name):
    """Whether this mention of `name` declares or defines it rather than
    using it: its key in the language file, its entry in a `.style` file, or
    a declaration, definition or constructor initializer in code."""
    escaped = re.escape(name)
    if path == LANG_FILE:
        return re.match(r'\s*"' + escaped + r'(?:#\w+)?"\s*=', line) is not None
    if path.endswith('.style'):
        return re.match(r'\s*' + escaped + r'\s*:', line) is not None
    stripped = line.strip()
    if re.match(r'[,:]\s*' + escaped + r'\s*[({]', stripped):
        return True
    if not line[:1].isspace():
        head = CLASS_HEAD.match(line)
        if head:
            return head.group(1).split('::')[-1] == name
        if '(' in line.split('=')[0]:
            declared = cpp_text.definition_name(line)
            return declared is not None and declared.split('::')[-1] == name
        return metrics.declares(line, re.compile(r'\b' + escaped + r'\b'))
    typed = (r'^' + NOT_TYPE + r'(?:\[\[nodiscard\]\]\s*)?'
             r'(?:(?:static|virtual|inline|explicit|constexpr|mutable|const)\s+)*'
             r'[\w:<>*&,\[\] ]+\s[*&]?')
    if re.match(typed + escaped + r'\(', stripped):
        return True
    return re.match(
        typed + escaped + r'(?:\s*=\s*[^;]*|\s*\{[^;]*\}|\[[^\]]*\])?;$',
        stripped) is not None


def orphaned(tree, base, staged):
    """Names the removed lines used whose only remaining mentions in the tree
    are their own definitions or declarations: a function, field, class,
    style or language key the change left without a user."""
    removed = Counter()
    added = Counter()
    used = set()
    for path, (minus, plus) in diff_lines(tree.repo, base, staged).items():
        for line in minus:
            names = set(identifiers(line.split('//')[0]))
            removed.update(names)
            used |= {name for name in names if not defines(path, line, name)}
        for line in plus:
            added.update(identifiers(line.split('//')[0]))
    candidates = {
        name for name, count in removed.items()
        if count > added.get(name, 0) and name in used and len(name) >= 4
        and name not in CODE_WORDS and not metrics.UNUSED_EXEMPT.search(name)
    }
    if not candidates:
        return []
    files = {}
    for path, text in tree.files.items():
        if not tracked_path(path):
            continue
        for name in set(identifiers(text)) & candidates:
            files.setdefault(name, []).append(path)
    result = []
    for name in sorted(candidates):
        paths = files.get(name, [])
        if not paths or len(paths) > 3:
            continue
        pattern = re.compile(r'\b' + re.escape(name) + r'\b')
        places = []
        for path in paths:
            for number, line in enumerate(mention_lines(tree.files[path]), 1):
                if not pattern.search(line):
                    continue
                if not defines(path, line, name) or 'override' in line:
                    places = None
                    break
                places.append((path, number))
            if places is None:
                break
        if places:
            result.append((name, places))
    return result


def platform_only(path):
    name = path.rsplit('/', 1)[-1]
    return ('/platform/' in path or path.endswith('.mm')
            or any(f'_{os}.' in name or name.startswith(f'{os}_')
                   for os in ('win', 'mac', 'linux')))


def stranded(tree, base, staged):
    """Functions and classes whose definitions the change removed, with every
    remaining mention in the tree, when nothing defines the name any more.
    Platform files are scanned too, so this catches what a local build misses.
    A removed method counts only mentions in platform files and in files
    naming its class, because method names repeat across unrelated classes
    and locals."""
    before = set()
    after = set()
    for status, path in changed(tree.repo, base, staged):
        old = base_text(tree.repo, base, path)
        if old is not None:
            before |= definitions(old, path)
        if status != 'D' and path in tree.files:
            after |= definitions(tree.files[path], path)
    kept = {name for name, _ in after}
    owners = {}
    for name, owner in before:
        if name not in kept:
            owners.setdefault(name, set()).add(owner)
    if not owners:
        return []
    pattern = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(owners))) + r')\b')
    mentions = {}
    still_defined = set()
    for path, text in tree.files.items():
        if not tracked_path(path):
            continue
        if not pattern.search(text):
            continue
        still_defined |= {name for name, _ in definitions(text, path)} & owners.keys()
        relevant = {
            name for name, qualifiers in owners.items()
            if None in qualifiers or platform_only(path)
            or any(q in text for q in qualifiers)
        }
        for number, line in enumerate(mention_lines(text), 1):
            for match in pattern.finditer(line):
                if match.group(1) in relevant:
                    mentions.setdefault(match.group(1), []).append((path, number))
    return sorted(
        (name, places) for name, places in mentions.items()
        if name not in still_defined)


def grown_classes(path, before, head, warnings):
    old = {c.name: c for c in cpp_text.classes(before)}
    for item in cpp_text.classes(head):
        prior = old.get(item.name)
        if (item.methods >= LIMITS['class_methods']
                and prior is not None and item.methods > prior.methods):
            warnings.append(
                f'{path}:{item.start}: class {item.name} grows to '
                f'{item.methods} methods; extract a collaborator instead.')


def heavier(path, fresh, graph, costs, warnings):
    reach = costs['reach'].get(path, 0)
    if not reach:
        return
    kept = {path}
    for _, target in graph.edges.get(path, ()):
        if target not in fresh:
            kept |= graph.closure(target)
    grown = set(kept)
    for target in fresh:
        grown |= graph.closure(target)
    added = sum(graph.tree.lines(p) for p in grown - kept)
    work = added * reach
    if work >= BUILD_SHARE * costs['total']:
        warnings.append(
            f'{path}: new includes add ~{added} header lines to {reach} units '
            f'(~{work / costs["total"]:.1%} of build work); forward-declare '
            f'or move the include into the .cpp.')


def moves(repo, base, head=None, staged=False):
    """Whether a change only moves code: every removed line reappears among
    added lines, every added line is a removed line or scaffolding, and
    inside a moved function the lines keep their order."""
    args = ['diff', '-U0', '--no-color', '--no-renames']
    if staged:
        args += ['--cached', base]
    elif head:
        args += [base, head]
    else:
        args += [base]
    raw = git(repo, *args, '--', 'Telegram').decode('utf-8', 'replace')
    removed = {}
    added = {}
    runs = {}
    blocks = []
    source = None
    block = None
    for line in raw.splitlines():
        if line.startswith('--- '):
            source = line[6:] if line.startswith('--- a/') else None
            continue
        if line.startswith('+++ '):
            continue
        if line.startswith('@@'):
            block = None
            continue
        if not line or line[0] not in '+-':
            continue
        text = comparable(line[1:])
        if line[0] == '-':
            removed[text] = removed.get(text, 0) + 1
            runs.setdefault(source, []).append(text)
        else:
            added[text] = added.get(text, 0) + 1
            if block is None:
                block = []
                blocks.append(block)
            block.append((line[1:], text))
    if not staged and not head:
        for path in untracked(repo):
            lines = (Path(repo) / path).read_text(
                encoding='utf-8', errors='replace').split('\n')
            block = [(line, comparable(line)) for line in lines]
            blocks.append(block)
            for _, text in block:
                added[text] = added.get(text, 0) + 1
    novel = []
    lost = []
    moved = 0
    for text, count in added.items():
        moved += min(count, removed.get(text, 0))
        extra = count - removed.get(text, 0)
        if extra > 0 and not SCAFFOLDING.match(text):
            novel.append((text, extra))
    for text, count in removed.items():
        extra = count - added.get(text, 0)
        if extra > 0 and not SCAFFOLDING.match(text):
            lost.append((text, extra))
    declarations = [
        (text, count) for text, count in novel if declaration_like(text)
    ]
    substantive = [item for item in novel if item not in declarations]
    positions = {}
    for name, run in runs.items():
        for index, text in enumerate(run):
            positions.setdefault(text, []).append((name, index))
    for block in blocks:
        text = out_of_order(block, positions)
        if text is not None:
            substantive.append(('order changed at: ' + text, 1))
    return {
        'moved_lines': moved,
        'novel_lines': sum(count for _, count in novel),
        'declaration_lines': sum(count for _, count in declarations),
        'substantive_novel': substantive,
        'lost': lost,
        'move_only': not substantive and not lost,
    }


def out_of_order(block, positions):
    """The first moved line of an added block that does not follow the
    previous one in its source. Only lines removed exactly once count, and a
    new source position may start only at a column-0 line or after a blank
    line or a closing brace, so whole functions and groups may be reordered
    but adjacent statements may not."""
    run = None
    at = -1
    previous = ''
    for raw, text in block:
        boundary = (not raw[:1].isspace() or not previous.strip()
                    or previous.strip() in ('}', '};'))
        previous = raw
        places = positions.get(text, ())
        if (len(places) != 1 or SCAFFOLDING.match(text)
                or (DECLARATION.match(text) and text.endswith(';'))):
            continue
        name, index = places[0]
        if run == name and index > at:
            at = index
            continue
        if run is not None and not boundary:
            return text
        run, at = name, index
    return None


def comparable(line):
    """A moved definition may shed `static`/`[[nodiscard]]` and default
    arguments to its new declaration; compare without them."""
    text = ' '.join(line.split())
    text = PREFIXES.sub('', text)
    if (text.endswith((') {', ')', ',', ');', ') const {', ') const;'))
            and not text.startswith(CONTROL)):
        text = DEFAULTS.sub(r'\1', text)
    return text


def declaration_like(text):
    return (text.endswith((';', ',', '(')) and '{' not in text
            and '=' not in text and not text.startswith('return'))
