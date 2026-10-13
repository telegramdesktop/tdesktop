"""Text-level C++ scanning tuned to tdesktop layout: definitions start at
column 0 and close with a column-0 brace."""
import re
from dataclasses import dataclass


INCLUDE = re.compile(r'^\s*#\s*(?:include|import)\s*"([^"]+)"')
IDENT = re.compile(r'[A-Za-z_]\w*')
CALLEE = re.compile(r'(~?[A-Za-z_][\w:~]*|operator\s*\S+?)\s*\(')
CLASS_START = re.compile(
    r'^(?:template\s*<.*>\s*)?(class|struct)\s+(?:\[\[[^\]]*\]\]\s*)?'
    r'([A-Za-z_]\w*)\b')
NOT_DEFINITION = (
    '}', '#', '/', '*', ' ', '\t', ')', ',', ':', 'namespace', 'using ', 'class ',
    'struct ', 'enum ', 'union ', 'template', 'extern', 'static_assert',
    'Q_', 'typedef ', 'friend ',
)
CONTROL = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'decltype'}
TRIVIAL = {
    '', 'return', 'break', 'continue', 'default', 'else', 'public',
    'private', 'protected', 'Q_OBJECT',
}
EXTENSIONS_TU = ('.cpp', '.mm', '.c', '.cc', '.cxx')
EXTENSIONS_HEADER = ('.h', '.hpp')


@dataclass
class Function:
    name: str
    start: int
    end: int

    @property
    def lines(self):
        return self.end - self.start + 1


@dataclass
class Class:
    name: str
    start: int
    end: int
    methods: int
    fields: int


def is_tu(path):
    return path.endswith(EXTENSIONS_TU)


def is_header(path):
    return path.endswith(EXTENSIONS_HEADER)


def functions(text):
    """Column-0 definitions whose body ends at a column-0 `}`."""
    lines = code_lines(text)
    result = []
    i = 0
    count = len(lines)
    while i < count:
        line = lines[i]
        if not line or line.startswith(NOT_DEFINITION):
            i += 1
            continue
        opening = None
        for j in range(i, min(count, i + 80)):
            stripped = lines[j].rstrip()
            if j > i and (not stripped or stripped.startswith(('#', 'namespace'))):
                break
            if stripped.endswith('{'):
                opening = j
                break
            if stripped.endswith(';'):
                break
        if opening is None:
            i += 1
            continue
        end = opening + 1
        while end < count and not lines[end].startswith('}'):
            end += 1
        if end >= count:
            break
        if not lines[end].startswith(('};', '})')):
            name = definition_name(' '.join(
                part.strip() for part in lines[i:opening + 1]))
            if name:
                result.append(Function(name, i + 1, end + 1))
        i = end + 1
    return result


def definition_name(header):
    for match in CALLEE.finditer(header):
        name = match.group(1)
        if name.split('::')[-1] not in CONTROL:
            return name
    return None


def classes(text):
    """Column-0 class/struct bodies with rough member counts."""
    lines = text.split('\n')
    result = []
    i = 0
    count = len(lines)
    while i < count:
        line = lines[i]
        match = CLASS_START.match(line)
        if not match or (';' in line and '{' not in line):
            i += 1
            continue
        if line.rstrip().endswith('};'):
            result.append(Class(match.group(2), i + 1, i + 1, 0, 0))
            i += 1
            continue
        end = i + 1
        while end < count and not lines[end].startswith('};'):
            end += 1
        if end >= count:
            break
        methods = 0
        fields = 0
        for body in lines[i + 1:end]:
            if not body.startswith('\t') or body.startswith('\t\t'):
                continue
            member = body[1:]
            if member.startswith(('//', 'Q_OBJECT', 'friend ', 'using ')):
                continue
            if '(' in member and not member.lstrip().startswith('='):
                methods += 1
            elif member.rstrip().endswith(';'):
                fields += 1
        result.append(Class(match.group(2), i + 1, end + 1, methods, fields))
        i = end + 1
    return result


def includes(text):
    """[(line number, quoted include path)]."""
    result = []
    for number, line in enumerate(text.split('\n'), 1):
        if line.lstrip().startswith('#'):
            match = INCLUDE.match(line)
            if match:
                result.append((number, match.group(1)))
    return result


def identifiers(text):
    return IDENT.findall(text)


def code_lines(text):
    """Lines with /* */ block comments blanked, numbering preserved."""
    result = []
    inside = False
    for line in text.split('\n'):
        if inside:
            end = line.find('*/')
            if end < 0:
                result.append('')
                continue
            line = line[end + 2:]
            inside = False
        start = line.find('/*')
        while start >= 0 and '//' not in line[:start]:
            end = line.find('*/', start + 2)
            if end < 0:
                line = line[:start]
                inside = True
                break
            line = line[:start] + line[end + 2:]
            start = line.find('/*')
        result.append(line)
    return result


def normalize(line):
    """Whitespace-collapsed code, or '' for lines that carry no logic."""
    stripped = line.strip()
    if (not stripped
        or stripped.startswith(('//', '#', '*', '/*', 'namespace', 'using '))
        or stripped.startswith('} // namespace')):
        return ''
    cut = stripped.find(' //')
    if cut > 0:
        stripped = stripped[:cut].rstrip()
    if re.sub(r'\W', '', stripped) in TRIVIAL:
        return ''
    if len(re.sub(r'\W', '', stripped)) < 4:
        return ''
    return ' '.join(stripped.split())
