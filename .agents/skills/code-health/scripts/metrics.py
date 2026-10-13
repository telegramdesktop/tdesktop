"""Per-file size metrics, areas, and dead-code candidates."""
import posixpath
import re

import cpp_text
from sources import LANG_FILE


LONG_FUNCTION = 150
UNUSED_EXEMPT = re.compile(
    r'(^~|^operator|Event$|^w?main$|WinMain$|^DllMain$|^on[A-Z]|^paint'
    r'|^Q_|^qt_|^test)')


def area_of(path):
    """Directory up to two levels under its source root."""
    parts = path.split('/')
    if len(parts) >= 3 and parts[1] == 'SourceFiles':
        inner = parts[2:-1]
        return '/'.join(inner[:2]) if inner else '(root)'
    if len(parts) >= 2 and parts[1].startswith('lib_'):
        inner = parts[2:-1]
        return '/'.join([parts[1]] + inner[:2])
    return '/'.join(parts[:-1])


def file_metrics(tree):
    result = {}
    for path, text in tree.files.items():
        if not path.endswith(cpp_text.EXTENSIONS_TU + cpp_text.EXTENSIONS_HEADER):
            continue
        functions = cpp_text.functions(text) if cpp_text.is_tu(path) else []
        classes = cpp_text.classes(text)
        result[path] = {
            'lines': text.count('\n') + 1,
            'functions': len(functions),
            'max_function': max((f.lines for f in functions), default=0),
            'long_functions': [
                {'name': f.name, 'line': f.start, 'lines': f.lines}
                for f in sorted(functions, key=lambda f: -f.lines)
                if f.lines >= LONG_FUNCTION
            ],
            'classes': [
                {
                    'name': c.name,
                    'line': c.start,
                    'lines': c.end - c.start + 1,
                    'methods': c.methods,
                    'fields': c.fields,
                }
                for c in classes if c.methods + c.fields >= 40
            ],
        }
    return result


def unused_candidates(tree):
    """Functions defined under SourceFiles whose name appears nowhere but in
    its own declarations and definition. Candidates only: deletion is
    proven by building without them."""
    counts = {}
    for path, text in tree.files.items():
        if path == LANG_FILE:
            continue
        for name in cpp_text.identifiers(text):
            counts[name] = counts.get(name, 0) + 1
    result = []
    for path, text in tree.files.items():
        if (not path.startswith('Telegram/SourceFiles/')
                or not path.endswith(('.cpp', '.cc', '.cxx'))
                or '/test/' in path):
            continue
        header = tree.files.get(posixpath.splitext(path)[0] + '.h', '')
        lines = text.split('\n')
        for function in cpp_text.functions(text):
            short = function.name.split('::')[-1]
            if (UNUSED_EXEMPT.search(short)
                    or not short[:1].isalpha()
                    or counts.get(short, 0) > 2):
                continue
            parts = function.name.split('::')
            if len(parts) > 1 and parts[-2] == short:
                continue
            if not only_declared(short, counts[short], function.start, lines, header):
                continue
            result.append({
                'path': path,
                'line': function.start,
                'name': function.name,
                'lines': function.lines,
            })
    result.sort(key=lambda item: -item['lines'])
    return result


def only_declared(name, total, definition, lines, header):
    """Every mention besides the definition is a declaration: one in the
    paired header, or a column-0 forward declaration in the same file."""
    pattern = re.compile(r'\b' + re.escape(name) + r'\b')
    local = sum(len(pattern.findall(line)) for line in lines)
    if local + len(pattern.findall(header)) < total:
        return False
    others = [
        line for number, line in enumerate(lines, 1)
        if number != definition and pattern.search(line)
    ]
    return all(declares(line, pattern) for line in others)


def declares(line, pattern):
    """A column-0 mention that is not an initializer-list entry and not
    used after `=` or `(` on its line."""
    if line[:1].isspace() or line.startswith((',', ':')):
        return False
    before = line[:pattern.search(line).start()]
    return '=' not in before and '(' not in before
