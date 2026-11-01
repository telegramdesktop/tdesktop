"""Daily harness inventory; archived mentions are evidence, not deletion proof."""
import datetime
from pathlib import Path
import re

import cpp_text
import sources


ROOT = 'Telegram/SourceFiles/test/'
README = ROOT + 'README.md'
README_LIMIT = 8192
WINDOW = 90
TASK = r'\d{4}/\d{2}/\d{2}/[\w-]+'
OPEN = {'todo', 'in-progress', 'blocked', 'split-required'}
MODULE = re.compile(r'^' + ROOT + r'(test_\w+)\.(?:h|cpp|mm)$')
NON_CODE = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\\n])\'', re.S)


def date(stamp):
    return datetime.datetime.fromtimestamp(int(stamp), datetime.timezone.utc).date()


def documents(tree):
    if tree.ref == 'WORKTREE':
        return {p.relative_to(tree.repo).as_posix(): p.read_text(encoding='utf-8')
                for p in (tree.repo / ROOT).rglob('*.md')}
    return sources.read_blobs(tree.repo, [
        (path, oid) for _, kind, oid, path in sources.list_tree(tree.repo, tree.commit, ROOT)
        if kind == 'blob' and path.endswith('.md')
    ])


def source_history(tree):
    kinds, births = {}, {}
    raw = sources.git(tree.repo, 'log', tree.commit,
                      '--format=@%ct %s%n%(trailers:key=Task,valueonly)',
                      '--name-only', '--diff-filter=A', '--no-renames', '--', ROOT).decode()
    stamp, task = None, None
    for line in raw.splitlines():
        if line.startswith('@'):
            stamp, task = date(line[1:].split(' ', 1)[0]), None
        elif re.fullmatch(TASK, line):
            task = line
        elif MODULE.fullmatch(line) and stamp is not None:
            name = MODULE.fullmatch(line)[1]
            if name not in births or stamp < births[name][0]:
                births[name] = (stamp, task)
    raw = sources.git(tree.repo, 'log', tree.commit,
                      '--format=@%s%n%(trailers:key=Task,valueonly)', '--', 'Telegram').decode()
    workflow = False
    for line in raw.splitlines():
        if line.startswith('@'):
            workflow = line.startswith('@[ai] ')
        elif re.fullmatch(TASK, line):
            kinds.setdefault(line, set()).add('harness' if workflow else 'product')
    shallow = sources.git(tree.repo, 'rev-parse', '--is-shallow-repository').strip() == b'true'
    return kinds, births, shallow


def identifiers(header):
    code = code_only(header)
    namespace = re.search(r'\bnamespace Test(?:::Internal)?\s*\{', code)
    if namespace:
        code = code[namespace.end():]
    calls = set(re.findall(r'\b([A-Z]\w*)\s*\(', code))
    types = set(re.findall(r'\b(?:class|struct|enum(?:\s+class)?)\s+(\w+)', code))
    return calls | types


def code_only(text, keep_literals=False):
    def replace(match):
        value = match[0]
        if keep_literals and not value.startswith(('//', '/*')):
            return value
        return '\n' * value.count('\n') + ' '
    return NON_CODE.sub(replace, text)


def kind(name, header, doc):
    declared = re.search(r'^Type: (.+)', doc, re.M)
    value = declared[1].lower() if declared else ''
    if 'self-test' in value and 'may include' not in value:
        return 'self-test'
    if 'internal dependency' in value or 'namespace Test::Internal' in header:
        return 'internal'
    symbols = set(re.findall(r'\b([A-Z]\w*)\s*\(', code_only(header)))
    if name.endswith('_self_test') or (symbols and all(s.endswith('SelfTest') for s in symbols)):
        return 'self-test'
    return 'api' if header else 'unknown'


def workflow_references(tree, names):
    entries = sources.list_tree(tree.repo, tree.commit, '.agents')
    texts = sources.read_blobs(tree.repo, [
        (path, oid) for _, entry_kind, oid, path in entries
        if entry_kind == 'blob' and path.endswith('.md')
    ])
    result = {name: [] for name in names}
    for path, text in texts.items():
        for name in names:
            if re.search(r'\b' + re.escape(name) + r'\b', text):
                result[name].append(path)
    return result


def patch_code(text):
    """Only surviving C++ hunk lines outside permanent helper implementations."""
    result, accept, hunk = [], False, False
    for line in text.splitlines():
        if line.startswith('diff --git '):
            accept, hunk = False, False
        elif line.startswith('+++ b/'):
            path = line[6:]
            accept = (path.endswith(cpp_text.EXTENSIONS_TU + cpp_text.EXTENSIONS_HEADER)
                      and (not path.startswith(ROOT) or path == ROOT + 'test_scenario.cpp'))
        elif line.startswith('@@ '):
            hunk = True
        elif accept and hunk and line.startswith(('+', ' ')):
            result.append(line[1:])
    return '\n'.join(result)


def mentions(text, names, owners, unqualified=False):
    included = {Path(path).stem for _, path in cpp_text.includes(code_only(text, True))} & names
    code = code_only(text)
    symbols = set(re.findall(r'\bTest::(?:Internal::)?(\w+)', code))
    if unqualified:
        symbols.update(re.findall(r'(?<![:\w])([A-Z]\w*)\b(?!::)', code))
    hits = {name: set() for name in included}
    for symbol in symbols & owners.keys():
        hits.setdefault(owners[symbol], set()).add(symbol)
    return hits


def archive(queue, kinds, names, owners):
    coverage = {'available': False, 'approved': 0, 'with_patches': 0,
                'missing_patches': 0, 'unknown_kind': 0, 'missing_date': 0,
                'read_errors': 0, 'queue_commit': None, 'dirty_queue': False}
    uses, active = [], {name: [] for name in names}
    if not queue or not (Path(queue) / 'tasks').is_dir():
        return uses, active, coverage
    queue = Path(queue)
    commit = sources.git(queue, 'rev-parse', 'HEAD').decode().strip()
    coverage['queue_commit'] = commit
    approved = {}
    raw = sources.git(queue, 'log', commit, '--format=%ct %s', '--', 'tasks').decode()
    for line in raw.splitlines():
        match = re.fullmatch(r'(\d+) Approve (' + TASK + r')', line)
        if match:
            approved.setdefault(match[2], date(match[1]))
    paths = sources.git(queue, 'ls-files', '-z', '--', 'tasks').decode().split('\0')
    states = [p for p in paths if re.fullmatch(r'tasks/' + TASK + r'/state.yaml', p)]
    patches = {}
    for path in paths:
        match = re.fullmatch(r'tasks/(' + TASK + r')/work/[^/]+\.patch', path)
        if match:
            patches.setdefault(match[1], []).append(path)
    for state in sorted(states):
        task = state[len('tasks/'):-len('/state.yaml')]
        try:
            text = (queue / state).read_text(encoding='utf-8')
            match = re.search(r'^status: (\S+)', text, re.M)
            status = match[1] if match else 'unknown'
            if status in OPEN:
                request = (queue / 'tasks' / task / 'task.md').read_text(encoding='utf-8')
                for name in names:
                    if re.search(r'\b' + re.escape(name) + r'\b', request):
                        active[name].append(task)
                continue
            if status != 'approved':
                continue
            coverage['approved'] += 1
            if not patches.get(task):
                coverage['missing_patches'] += 1
                continue
            coverage['with_patches'] += 1
            category = ('product' if 'product' in kinds.get(task, ())
                        else 'harness' if task in kinds else 'unknown')
            coverage['unknown_kind'] += category == 'unknown'
            coverage['missing_date'] += task not in approved
            hits = {}
            for path in patches[task]:
                code = patch_code((queue / path).read_text(encoding='utf-8'))
                for name, symbols in mentions(code, names, owners, unqualified=True).items():
                    hits.setdefault(name, set()).update(symbols)
            uses.append((task, approved.get(task), category, hits))
        except (OSError, UnicodeError):
            coverage['read_errors'] += 1
    coverage['dirty_queue'] = bool(sources.git(queue, 'status', '--porcelain', '--', 'tasks').strip())
    coverage['available'] = (coverage['with_patches'] > 0
                             and not coverage['read_errors'] and not coverage['dirty_queue']
                             and commit == sources.git(queue, 'rev-parse', 'HEAD').decode().strip())
    return uses, active, coverage


def inspect(tree, queue=None, today=None):
    today = today or datetime.datetime.now(datetime.timezone.utc).date()
    docs = documents(tree)
    modules = {}
    for path in sorted(tree.files):
        match = MODULE.fullmatch(path)
        if match and match[1] != 'test_scenario':
            modules.setdefault(match[1], []).append(path)
    kinds, births, shallow = source_history(tree)
    declarations = {}
    for name in modules:
        for symbol in identifiers(tree.files.get(ROOT + name + '.h', '')):
            declarations.setdefault(symbol, set()).add(name)
    owners = {symbol: next(iter(names)) for symbol, names in declarations.items() if len(names) == 1}
    names = set(modules)
    uses, active, coverage = archive(queue, kinds, names, owners)
    workflow = workflow_references(tree, names)
    coverage['shallow_source_history'] = shallow
    dependents = {name: set() for name in modules}
    callers = {name: [] for name in modules}
    for path, text in tree.files.items():
        if not path.endswith(cpp_text.EXTENSIONS_TU + cpp_text.EXTENSIONS_HEADER):
            continue
        own = MODULE.fullmatch(path)
        for name in mentions(text, names, owners, unqualified=bool(own)):
            if path in modules[name]:
                continue
            if own and own[1] in names:
                dependents[name].add(own[1])
            else:
                callers[name].append(path)
    rows = []
    for name, paths in modules.items():
        doc = ROOT + 'docs/helpers/' + name + '.md'
        header = tree.files.get(ROOT + name + '.h', '')
        category = kind(name, header, docs.get(doc, ''))
        born, creator = births.get(name, (None, None))
        age = (today - born).days if born and not shallow else None
        consumers = {name}
        pending = list(dependents[name])
        while pending:
            consumer = pending.pop()
            if consumer not in consumers:
                consumers.add(consumer)
                pending.extend(dependents[consumer])
        product, direct, uncertain, harness_uses, self_tests = {}, {}, {}, [], []
        for task, when, task_kind, hits in uses:
            relevant = consumers & hits.keys()
            if not relevant or task == creator or (when and born and when < born):
                continue
            normal = {m for m in relevant if not hits[m] or any(
                not symbol.endswith('SelfTest') for symbol in hits[m])}
            if not normal:
                self_tests.append(task)
            elif task_kind == 'harness':
                harness_uses.append(task)
            elif task_kind == 'unknown' or when is None:
                uncertain[task] = when.isoformat() if when else None
            else:
                product[task] = when.isoformat()
                if name in normal:
                    direct[task] = when.isoformat()
        cutoff = (today - datetime.timedelta(days=WINDOW)).isoformat()
        recent = sum(when > cutoff for when in product.values())
        open_tasks = sorted({task for m in consumers for task in active[m]})
        reasons = []
        if coverage['available'] and age is not None and age >= WINDOW and category == 'api':
            if not recent and not any(when is None or when > cutoff for when in uncertain.values()):
                reasons.append('no observed independent use in 90 days')
            if len(product) + len(uncertain) <= 2:
                reasons.append('at most two observed lifetime consumers after 90 days')
        rows.append({
            'id': 'harness-retention:' + name, 'module': name, 'kind': category,
            'paths': paths, 'added': born.isoformat() if born else None,
            'created_by': creator, 'age_days': age,
            'source_lines': sum(len(tree.files[p].splitlines()) for p in paths),
            'reference_bytes': len(docs.get(doc, '').encode()),
            'product_tasks': product if coverage['available'] else None,
            'direct_product_tasks': direct if coverage['available'] else None,
            'last_product_use': max(product.values(), default=None) if coverage['available'] else None,
            'recent_product_tasks': recent if coverage['available'] else None,
            'uncertain_tasks': uncertain, 'harness_tasks': sorted(harness_uses),
            'self_test_tasks': sorted(self_tests), 'required_by': sorted(dependents[name]),
            'source_callers': sorted(callers[name]), 'open_tasks': open_tasks,
            'workflow_references': workflow[name],
            'review_reasons': reasons,
            'routing': 'held: open consumer' if open_tasks else 'review only',
        })
    readme_bytes = len(docs[README].encode()) if README in docs else None
    index = docs.get(ROOT + 'docs/helpers/README.md', '')
    missing = [name for name in modules if ROOT + 'docs/helpers/' + name + '.md' not in docs
               or f'[{name}]({name}.md)' not in index]
    return {
        'checked_on': today.isoformat(), 'ref': tree.ref, 'commit': tree.commit,
        'coverage': coverage, 'modules': rows, 'readme_bytes': readme_bytes,
        'reference_bytes': sum(len(text.encode()) for path, text in docs.items() if path != README),
        'missing_references': missing,
        'limits': ['Final saved patch mentions are a lower bound, not executed-use telemetry.',
                   'Creation and harness-only tasks and self-test-only calls do not count as product reuse.',
                   'Unknown task classification/dates stay separate; indirect consumers are included.',
                   'Age is the first addition of the current module path in reachable Git history.',
                   'Source lines and documentation bytes do not measure runtime or compile time.'],
    }


def markdown(data):
    coverage = data['coverage']
    out = ['# Harness lifecycle', '', f'`{data["ref"]}` at `{data["commit"]}`; {data["checked_on"]}.', '',
           f'Readme: {data["readme_bytes"]} bytes (limit {README_LIMIT}); '
           f'detailed references: {data["reference_bytes"]} bytes.', '',
           f'Usage coverage: {"available" if coverage["available"] else "unavailable"}; '
           f'{coverage["with_patches"]}/{coverage["approved"]} approved records have saved patches; '
           f'{coverage["unknown_kind"]} unclassified, {coverage["missing_date"]} undated, '
           f'{coverage["read_errors"]} read errors. '
           f'Shallow source history: {coverage["shallow_source_history"]}; '
           f'dirty queue: {coverage["dirty_queue"]}.', '',
           'Review candidates require consumer and retention checks before any proposal. '
           'Self-tests and internal dependencies are not retired for lacking product calls.', '',
           '| Helper | Kind | Age days | Lines | Doc bytes | Product tasks incl. indirect | Last 90d | Uncertain | Review |',
           '|---|---|---|---|---|---|---|---|---|']
    for row in data['modules']:
        count = len(row['product_tasks']) if row['product_tasks'] is not None else 'unknown'
        age = row['age_days'] if row['age_days'] is not None else 'unknown'
        recent = row['recent_product_tasks'] if row['recent_product_tasks'] is not None else 'unknown'
        review = '; '.join(row['review_reasons']) or '—'
        if row['open_tasks']:
            review += '; held: open consumer'
        out.append(f'| {row["module"]} | {row["kind"]} | {age} | '
                   f'{row["source_lines"]} | {row["reference_bytes"]} | {count} | '
                   f'{recent} | {len(row["uncertain_tasks"])} | {review} |')
    out += ['', '## Coverage and interpretation', ''] + ['- ' + text for text in data['limits']]
    if data['missing_references']:
        out += ['', 'Missing indexed references: ' + ', '.join(data['missing_references']) + '.']
    out += ['', 'Exact task ids, source callers and dependency edges are in `harness.json`.', '']
    return '\n'.join(out)
