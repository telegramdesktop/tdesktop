"""Copy-paste detection over normalized lines.

Lines are whitespace-collapsed with comments and logic-free lines dropped
(cpp_text.normalize); a clone is a run of at least `window` equal
normalized lines in two places, extended to its maximal length."""
import hashlib

import cpp_text


WINDOW = 6
MIN_LINES = 10
MAX_BUCKET = 6


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


def copied_from(added, files, window=WINDOW, min_lines=8):
    """For each added hunk (path, [(number, text)]), existing code elsewhere
    that it repeats; used to refuse copy-paste at review time."""
    seqs = sequences(files)
    index = window_index(seqs, window)
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
