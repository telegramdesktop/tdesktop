from bisect import bisect_left, bisect_right
from pathlib import Path
import argparse
import re

from tree_sitter import Language, Parser
import tree_sitter_cpp


CPP = Language(tree_sitter_cpp.language())
CALL_MESSAGE = ("Put each argument of the multiline call on its own line"
    " after the opening parenthesis.")
CLOSE_INLINE_MESSAGE = "Close the inline argument list on the same line as its argument."
SUM_MESSAGE = "Start each continuation term on its own line with + or -."


def starts_of_lines(source):
    return [0] + [match.end() for match in re.finditer(b"\n", source)]


def changed_nodes(root, added_lines, line_at):
    pending = [root]
    while pending:
        node = pending.pop()
        index = bisect_left(added_lines, line_at(node.start_byte))
        last = line_at(max(node.start_byte, node.end_byte - 1))
        if index == len(added_lines) or added_lines[index] > last:
            continue
        yield node
        pending.extend(node.named_children)


def inline_match(node, arguments, opening_line, line_at):
    function = node.child_by_field_name("function")
    field = function.child_by_field_name("field")
    if function.text == b"v::match":
        branches = arguments[1:]
    elif field is not None and field.text == b"match":
        branches = arguments
    else:
        return False
    return (line_at(branches[0].start_byte) == opening_line
        and all(branch.type == "lambda_expression" for branch in branches)
        and all(line_at(first.start_byte) < line_at(second.start_byte)
            for first, second in zip(branches, branches[1:])))


def split_inline_argument(call, line_at):
    if call is None or call.has_error:
        return None
    children = call.named_children
    if len(children) != 1 or children[0].is_extra:
        return None
    argument = children[0]
    opening_line = line_at(call.start_byte)
    if (line_at(argument.end_byte - 1) == opening_line
        and opening_line < line_at(call.end_byte - 1)):
        return argument
    return None


def call_layout_error(node, added_lines, line_at):
    call = node.child_by_field_name("arguments")
    if call is None or call.has_error:
        return None
    opening_line = line_at(call.start_byte)
    closing_line = line_at(call.end_byte - 1)
    if opening_line == closing_line:
        return None
    children = call.named_children
    arguments = [child for child in children if not child.is_extra]
    if not arguments:
        return None
    if len(arguments) == 1:
        change = bisect_left(added_lines, opening_line)
        if (split_inline_argument(call, line_at) is not None
            and change < len(added_lines) and added_lines[change] <= closing_line):
            return closing_line, CLOSE_INLINE_MESSAGE
        return None
    if inline_match(node, arguments, opening_line, line_at):
        return None
    previous_line = opening_line
    for argument in arguments:
        argument_line = line_at(argument.start_byte)
        if argument_line <= previous_line:
            return argument_line, CALL_MESSAGE
        previous_line = line_at(argument.end_byte - 1)
    return None


def additive_operator(node):
    if node is not None and node.type == "binary_expression":
        operator = node.child_by_field_name("operator")
        if operator is not None and operator.type in ("+", "-"):
            return operator
    return None


def packed_terms(node, line_at):
    if node.has_error or line_at(node.start_byte) == line_at(node.end_byte - 1):
        return
    parent = node.parent
    if additive_operator(parent) is not None and parent.child_by_field_name("left") == node:
        return
    while (operator := additive_operator(node)) is not None:
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        operator_line = line_at(operator.start_byte)
        if (operator_line == line_at(left.end_byte - 1)
                or operator_line != line_at(right.start_byte)):
            yield operator_line
        node = left


def trailing_logical_operator(node, source, added_lines, line_at):
    if node.has_error:
        return None
    operator = node.child_by_field_name("operator")
    if operator is None or operator.type not in ("&&", "||"):
        return None
    left = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    operator_line = line_at(operator.start_byte)
    right_line = line_at(right.start_byte)
    if (operator_line != line_at(left.end_byte - 1)
            or operator_line == right_line
            or source[operator.end_byte:right.start_byte].strip()):
        return None
    change = bisect_left(added_lines, operator_line)
    if change < len(added_lines) and added_lines[change] <= right_line:
        return operator_line, (
            f"Start the continuation with {operator.type} and its right operand.")
    return None


def layout_errors(source, added_lines):
    line_starts = starts_of_lines(source)

    def line_at(offset):
        return bisect_right(line_starts, offset)

    tree = Parser(CPP).parse(source)
    for node in changed_nodes(tree.root_node, added_lines, line_at):
        if node.type == "call_expression":
            error = call_layout_error(node, added_lines, line_at)
            if error is not None:
                yield error
        elif node.type == "binary_expression":
            if additive_operator(node) is not None:
                for line in packed_terms(node, line_at):
                    yield line, SUM_MESSAGE
            else:
                error = trailing_logical_operator(node, source, added_lines, line_at)
                if error is not None:
                    yield error


def expand_split_inline_arguments(source):
    line_starts = starts_of_lines(source)

    def line_at(offset):
        return bisect_right(line_starts, offset)

    tree = Parser(CPP).parse(source)
    matches = []
    for node in changed_nodes(tree.root_node, range(1, len(line_starts) + 1), line_at):
        if node.type == "call_expression":
            call = node.child_by_field_name("arguments")
            argument = split_inline_argument(call, line_at)
            if argument is not None:
                matches.append((call, argument))

    edits, closing_indents = [], {}
    for call, argument in sorted(matches, key=lambda match: match[0].start_byte):
        opening = call.start_byte
        closing = call.end_byte - 1
        before = source[opening + 1:argument.start_byte]
        after = source[argument.end_byte:closing]
        if before.strip() or after.strip():
            continue
        opening_line = line_at(opening)
        prefix = source[line_starts[opening_line - 1]:opening]
        leading = prefix[:len(prefix) - len(prefix.lstrip(b" \t"))]
        indent = closing_indents.get(opening_line, leading)
        newline = b"\r\n" if b"\r\n" in after else b"\n"
        closing_indents[line_at(closing)] = indent
        edits.extend((
            (opening + 1, argument.start_byte, newline + indent + b"\t"),
            (argument.end_byte, closing, newline + indent),
        ))

    chunks, start = [], 0
    for first, last, replacement in edits:
        chunks.extend((source[start:first], replacement))
        start = last
    chunks.append(source[start:])
    return b"".join(chunks)


def main():
    parser = argparse.ArgumentParser(description="Expand split inline C++ arguments.")
    parser.add_argument("--fix", nargs="+", type=Path, required=True, metavar="FILE")
    for path in parser.parse_args().fix:
        source = path.read_bytes()
        formatted = expand_split_inline_arguments(source)
        if formatted != source:
            path.write_bytes(formatted)


if __name__ == "__main__":
    main()
