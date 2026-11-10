"""Copy-paste detection over normalized lines.

Lines are whitespace-collapsed with comments and logic-free lines dropped
(cpp_text.normalize); a clone is a run of at least `window` equal
normalized lines in two places, extended to its maximal length."""
import hashlib
import re

import cpp_text


WINDOW = 6
MIN_LINES = 10
MAX_BUCKET = 6
LIST_HEAD = re.compile(r'((?:\w+::)*)(\w+)\($')
RETURN_TYPE = re.compile(r'(?:\[\[\w+\]\]\s*)*(?:(?:\w+::)*\w+(?:<.*>)?[\s*&]+)*')
DECLARATOR_END = r'(?:[\w\s:<>*&,\[\]]|->)*'
DECLARES = re.compile(DECLARATOR_END + r'(?:=\s*(?:0|default|delete)\s*)?;')
DEFINES = re.compile(DECLARATOR_END + r'\{')
NOT_QUALIFIER = re.compile(r'\b(?:override|final)\b|(?:->|=).*')
QUALIFIER = re.compile(r'\w+|[^\w\s;{]')
PARAMETER_TOKEN = re.compile(
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\.?[0-9](?:[eEpP][+-]|[\w.]|\'\w)*'
    r'|\w+|[=!<>]=|\S')
BRACKETS = {'(': ')', '[': ']', '{': '}'}


class Sequence:
    def __init__(self, path, text):
        self.path = path
        self.lines = []
        self.numbers = []
        for number, line in enumerate(cpp_text.code_lines(text), 1):
            normalized = cpp_text.normalize(line)
            if normalized:
                self.lines.append(normalized)
                self.numbers.append(number)
        self.hashes = [hash(line) for line in self.lines]


def sequences(files):
    return {
        path: Sequence(path, text)
        for path, text in files.items()
        if path.endswith(cpp_text.EXTENSIONS_TU + cpp_text.EXTENSIONS_HEADER)
    }


def window_index(seqs, window=WINDOW):
    index = {}
    for path, seq in seqs.items():
        hashes = seq.hashes
        for i in range(len(hashes) - window + 1):
            key = hash(tuple(hashes[i:i + window]))
            index.setdefault(key, []).append((path, i))
    return index


def find(files, window=WINDOW, min_lines=MIN_LINES, max_bucket=MAX_BUCKET):
    """Maximal clone pairs, longest first."""
    seqs = sequences(files)
    index = window_index(seqs, window)
    pairs = set()
    for places in index.values():
        if len(places) < 2 or len(places) > max_bucket:
            continue
        for a in range(len(places)):
            for b in range(a + 1, len(places)):
                first, second = sorted((places[a], places[b]))
                if first[0] == second[0] and second[1] - first[1] < window:
                    continue
                pairs.add((first[0], second[0], first[1], second[1]))
    result = []
    for left, right, i, j in pairs:
        if (left, right, i - 1, j - 1) in pairs:
            continue
        length = 0
        while (left, right, i + length, j + length) in pairs:
            length += 1
        count = length + window - 1
        if count < min_lines:
            continue
        a, b = seqs[left], seqs[right]
        if left == right and i + count > j:
            count = j - i
            if count < min_lines:
                continue
        block = a.lines[i:i + count]
        if declarations_only(block):
            continue
        block = '\n'.join(block)
        result.append({
            'id': hashlib.sha1(block.encode()).hexdigest()[:12],
            'lines': count,
            'left': {'path': left, 'start': a.numbers[i], 'end': a.numbers[i + count - 1]},
            'right': {'path': right, 'start': b.numbers[j], 'end': b.numbers[j + count - 1]},
        })
    result.sort(key=lambda clone: (-clone['lines'], clone['left']['path'], clone['left']['start']))
    return result


def declarations_only(block):
    """A pimpl or interface mirroring its declarations isn't a copy."""
    declared = sum(
        1 for line in block
        if line.endswith((';', ',', '(')) and '=' not in line
        and not line.startswith(('return ', 'if ', 'const auto ')))
    return declared * 5 >= len(block) * 4


def parameter_lists(text):
    """{line number: (name, kind, scope, signature, start, end, defaults_safe)}
    over each line of a function's multi-line parameter list, from the line
    that opens it through the line holding its closing parenthesis; the
    signature is the parameter text and the qualifiers that follow it.
    defaults_safe is False when the list's source holds a comment: removing
    it, as `//` cuts even inside a literal, can drop code from that text."""
    source = text.split('\n')
    lines = cpp_text.code_lines(text)
    result = {}
    i = 0
    while i < len(lines):
        found = parameter_list(lines, i)
        if found is None:
            i += 1
            continue
        start, end = found[4], found[5]
        found += (not any('//' in line or '/*' in line
                          for line in source[start - 1:end]),)
        for number in range(start, end + 1):
            result[number] = found
        i = end
    return result


def parameter_list(lines, i):
    """The list that `lines[i]` opens when it is the head of a function's
    declaration or column-0 definition; None for a call, an expression, or
    a list not closed within 80 lines and before a blank or `#` line."""
    code = lines[i].split('//')[0].rstrip()
    head = LIST_HEAD.search(code) if code.endswith('(') else None
    if head is None:
        return None
    scope, name = head.groups()
    prefix = code[:head.start()]
    if (not RETURN_TYPE.fullmatch(prefix.lstrip()) or name in cpp_text.CONTROL
            or cpp_text.CONTROL & set(cpp_text.identifiers(prefix))):
        return None
    # At column 0 only a constructor has no return type.
    if not prefix and scope.split('::')[-2:-1] != [name]:
        return None
    depth = 1
    parameters = []
    for j in range(i + 1, min(len(lines), i + 80)):
        if not lines[j].strip() or lines[j].lstrip().startswith('#'):
            return None
        code = lines[j].split('//')[0]
        for k, char in enumerate(code):
            depth += (char == '(') - (char == ')')
            if depth:
                continue
            tail = code[k + 1:].strip()
            following = lines[j + 1] if j + 1 < len(lines) else ''
            if DECLARES.fullmatch(tail):
                kind = 'declaration'
            elif prefix[:1].isspace():
                return None
            elif DEFINES.fullmatch(tail) or (
                    not prefix and not tail and following.startswith(':')
                    and not following.startswith('::')):
                kind = 'definition'
            else:
                return None
            parameters.append(code[:k])
            text = ' '.join(' '.join(parameters).split())
            return (name, kind, scope, (text, qualifiers(tail)), i + 1, j + 1)
        parameters.append(code)
    return None


def qualifiers(tail):
    """What follows a parameter list, such as `const`, `&&` or `noexcept`,
    as tokens, so a declaration and its definition agree while `const` and
    `&&` overloads don't. Left out are the closing `;` or `{`, the
    `override`, `final` or `= 0` only a declaration carries, and a trailing
    return type, which is not compared when it precedes the name either."""
    return tuple(QUALIFIER.findall(NOT_QUALIFIER.sub(' ', tail)))


def without_defaults(text):
    """A declaration's parameter text without its default values: each
    top-level `= value` up to the next top-level `,`, read over matched
    brackets and string or char literals. `<` is no bracket, being a
    comparison as often as a template list, so a comma inside template
    arguments only leaves text behind. None when a bracket or a literal is
    left open or closed by the wrong bracket, or for a raw string, whose
    content the literal reading would take for code."""
    if 'R"' in text:
        return None
    kept = []
    start = 0
    closers = []
    for match in PARAMETER_TOKEN.finditer(text):
        token = match.group()
        if token in ('"', "'"):
            return None
        if token in BRACKETS:
            closers.append(BRACKETS[token])
        elif token in (')', ']', '}'):
            if not closers or closers.pop() != token:
                return None
        elif not closers and token == '=' and start is not None:
            kept.append(text[start:match.start()].rstrip())
            start = None
        elif not closers and token == ',' and start is None:
            start = match.start()
    if closers:
        return None
    if start is not None:
        kept.append(text[start:])
    return ''.join(kept)


def counterpart(own, other):
    """Whether two parameter lists are one function's declaration and its
    definition: one name, one of each kind, scopes that agree where both
    heads spell one, and one signature, as written or once without_defaults()
    leaves out the declaration's default values where both lists are
    defaults_safe. Defaults the definition carries are kept, so a
    declaration without them still differs."""
    if other is None:
        return False
    name, kind, scope = own[:3]
    other_name, other_kind, other_scope = other[:3]
    declaration, definition = (own, other) if kind == 'declaration' else (other, own)
    declared, defined = declaration[3], definition[3]
    return (name == other_name and kind != other_kind
            and (('::' + scope).endswith('::' + other_scope)
                 or ('::' + other_scope).endswith('::' + scope))
            and (declared == defined
                 or (declaration[6] and definition[6] and declared[1] == defined[1]
                     and without_defaults(declared[0]) == defined[0])))


def copied_from(added, files, window=WINDOW, min_lines=8):
    """For each added hunk (path, [(number, text)]), existing code elsewhere
    that it repeats; used to refuse copy-paste at review time. A function's
    declaration and its definition repeat its parameter list by necessity,
    so a window lying inside one doesn't count as a copy of the other."""
    seqs = sequences(files)
    index = window_index(seqs, window)
    lists = {}

    def inside(path, first, last):
        if path not in lists:
            lists[path] = parameter_lists(files.get(path, ''))
        found = lists[path].get(first)
        return found if found == lists[path].get(last) else None

    result = []
    for path, hunk in added:
        normalized = [
            (number, cpp_text.normalize(text)) for number, text in hunk
        ]
        normalized = [(number, line) for number, line in normalized if line]
        hashes = [hash(line) for _, line in normalized]
        run_start = None
        run_source = None
        matched = set()
        for i in range(len(hashes) - window + 1):
            key = hash(tuple(hashes[i:i + window]))
            sources = [
                place for place in index.get(key, ())
                if not (place[0] == path and seqs[place[0]].numbers[place[1]]
                        == normalized[i][0])
            ]
            own = None
            if sources:
                own = inside(path, normalized[i][0], normalized[i + window - 1][0])
            if own is not None:
                sources = [
                    place for place in sources
                    if not counterpart(own, inside(
                        place[0], seqs[place[0]].numbers[place[1]],
                        seqs[place[0]].numbers[place[1] + window - 1]))
                ]
            if sources:
                matched.update(range(i, i + window))
                if run_start is None:
                    run_start = i
                    run_source = sources[0]
            elif run_start is not None:
                flush(result, path, normalized, run_start, matched, run_source, seqs, min_lines)
                run_start = None
                matched = set()
        if run_start is not None:
            flush(result, path, normalized, run_start, matched, run_source, seqs, min_lines)
    return result


def flush(result, path, normalized, start, matched, source, seqs, min_lines):
    if len(matched) < min_lines:
        return
    end = max(matched)
    seq = seqs[source[0]]
    result.append({
        'path': path,
        'start': normalized[start][0],
        'end': normalized[end][0],
        'lines': len(matched),
        'source': {
            'path': source[0],
            'start': seq.numbers[source[1]],
        },
    })
