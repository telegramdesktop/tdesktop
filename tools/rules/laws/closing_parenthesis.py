from bisect import bisect_left, bisect_right
from pathlib import Path
from typing import NamedTuple
import argparse
import json
import re
import sys

from tree_sitter import Language, Parser
import tree_sitter_cpp


CPP = Language(tree_sitter_cpp.language())
CLOSING_PUNCTUATION = rb"[),;\]} \t]*"


class Edit(NamedTuple):
    start: int
    end: int
    replacement: bytes


class Finding(NamedTuple):
    offset: int
    first_line: int
    last_line: int
    anchor_line: int
    edits: tuple[Edit, ...]


class Call(NamedTuple):
    opening: int
    first_argument: int
    first_argument_end: int
    last_argument_end: int
    closing: int
    sole_call_argument: bool


def calls(source, root):
    pending = [(root, None)]
    while pending:
        node, parent_type = pending.pop()
        kind = node.type
        children = node.named_children
        pending.extend((child, kind) for child in children)
        if kind != "argument_list" or node.has_error:
            continue
        closing = node.end_byte - 1
        if source[closing:closing + 1] != b")":
            continue
        args = [arg for arg in children if not arg.is_extra]
        if not args or args[-1].end_byte > closing:
            continue
        first = args[0]
        sole = len(children) == 1 and parent_type == "call_expression"
        yield Call(node.start_byte, first.start_byte, first.end_byte,
            args[-1].end_byte, closing, sole)


def width(text, tab_width, start=0):
    remainder = start % tab_width
    line = " " * remainder + text.decode("utf-8", "surrogateescape")
    return start - remainder + len(line.expandtabs(tab_width))


def inspect(source, layout, root=None):
    if root is None:
        root = Parser(CPP).parse(source).root_node
    line_starts = [0, *(line.end() for line in re.finditer(b"\n", source))]
    tab_width = layout["tab_width"]
    limit = layout["columns"]
    decisions = layout["closing_parenthesis_decisions"]
    partial_joins = {}
    wrapped_indents = {}

    def line(offset):
        return bisect_right(line_starts, offset)

    def fits(text):
        return width(text, tab_width) <= limit

    for call in sorted(calls(source, root), key=lambda candidate: candidate.closing):
        end = call.last_argument_end
        close = call.closing
        argument_line, close_line = line(end - 1), line(close)
        if argument_line >= close_line:
            continue
        open_line = line(call.opening)
        sole_inline = call.sole_call_argument and argument_line == open_line
        line_end = line_starts[close_line] if close_line < len(line_starts) else len(source)
        tail = source[close:line_end].rstrip(b"\r\n")
        before_close = source[line_starts[close_line - 1]:close]
        prefix = source[line_starts[argument_line - 1]:end]
        gap = source[end:close]
        standalone = not before_close.strip()
        closing_tail = re.fullmatch(CLOSING_PUNCTUATION, tail) is not None
        whitespace = not gap.strip()
        columns = width(prefix, tab_width)
        if previous := partial_joins.get(argument_line):
            previous_close, previous_columns = previous
            if previous_close < end:
                columns = max(columns, width(source[previous_close:end],
                    tab_width, previous_columns))
        can_join = (whitespace
            and not (line(call.first_argument) == open_line
                and line(call.first_argument_end - 1) > open_line)
            and width(tail, tab_width, columns) <= limit)
        facts = 8 * standalone + 4 * closing_tail + 2 * whitespace + can_join
        action = decisions[facts]
        if action == "2":
            partial_joins[close_line] = (close, columns)
            edits = (Edit(end, close, b""),)
        elif sole_inline:
            before_arg = source[call.opening + 1:call.first_argument]
            opening_prefix = source[line_starts[open_line - 1]:call.opening + 1]
            indent = wrapped_indents.get(open_line,
                opening_prefix[:len(opening_prefix) - len(opening_prefix.lstrip(b" \t"))])
            argument = source[call.first_argument:end]
            attached = standalone and closing_tail
            continuation = indent + b"\t" + argument + (tail if attached else b"")
            newline = b"\r\n" if b"\r\n" in gap else b"\n"
            if (whitespace and not before_arg.strip() and fits(opening_prefix)
                    and fits(indent + opening_prefix.lstrip(b" \t"))
                    and fits(continuation) and (attached or fits(indent + tail))):
                edits = (Edit(call.opening + 1, call.first_argument, newline + indent + b"\t"),
                    Edit(end, close, b"" if attached else newline + indent))
                wrapped_indents[close_line] = indent
            else:
                edits = ()
        elif action == "0":
            continue
        else:
            edits = ()
        first_line = open_line if sole_inline else argument_line
        yield Finding(close, first_line, close_line, first_line, edits)


def select(findings, ranges):
    if ranges is None:
        yield from findings
        return
    merged = []
    for first, last in sorted(ranges):
        if not 1 <= first <= last:
            raise ValueError(f"invalid line range: {first}:{last}")
        if merged and first <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(last, merged[-1][1]))
        else:
            merged.append((first, last))
    ends = [last for _, last in merged]
    for finding in findings:
        index = bisect_left(ends, finding.first_line)
        if index < len(merged) and merged[index][0] <= finding.last_line:
            yield finding._replace(anchor_line=max(finding.first_line, merged[index][0]))


def encode(finding):
    return {"offset": finding.offset, "first_line": finding.first_line,
        "last_line": finding.last_line, "anchor_line": finding.anchor_line,
        "edits": [[edit.start, edit.end, edit.replacement.decode("ascii")]
            for edit in finding.edits]}


def apply(source, findings):
    chunks = []
    offset = 0
    for start, end, replacement in sorted(edit for item in findings for edit in item.edits):
        if not offset <= start <= end <= len(source):
            raise ValueError(f"overlapping or invalid closing-parenthesis edit: {start}:{end}")
        chunks.extend((source[offset:start], replacement))
        offset = end
    chunks.append(source[offset:])
    return b"".join(chunks)


def parse_ranges(text):
    ranges = json.loads(text)
    if not isinstance(ranges, list) or any(
            not isinstance(pair, list) or len(pair) != 2
            or any(type(line) is not int for line in pair)
            or not 1 <= pair[0] <= pair[1] for pair in ranges):
        raise argparse.ArgumentTypeError("expected inclusive line ranges such as [[4,7]]")
    return ranges


def main():
    parser = argparse.ArgumentParser(description="Inspect C++ bytes for detached call closers.")
    parser.add_argument("--catalog", type=Path, default=Path(__file__).with_name("catalog.json"))
    parser.add_argument("--fix", action="store_true", help="write fixed C++ bytes instead of JSON")
    parser.add_argument("--line-ranges", type=parse_ranges,
        help="select edits and diagnostics touching the supplied inclusive line ranges")
    args = parser.parse_args()
    layout = json.loads(args.catalog.read_bytes())["layout"]
    source = sys.stdin.buffer.read()
    findings = list(select(inspect(source, layout), args.line_ranges))
    if args.fix:
        sys.stdout.buffer.write(apply(source, findings))
    else:
        print(json.dumps([encode(result) for result in findings]))


if __name__ == "__main__":
    main()
