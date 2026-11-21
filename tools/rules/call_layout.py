from bisect import bisect_left, bisect_right
import re

from tree_sitter import Language, Parser
import tree_sitter_cpp


CPP = Language(tree_sitter_cpp.language())


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


def packed_calls(source, added_lines):
    line_starts = [match.end() for match in re.finditer(b"\n", source)]

    def line_at(offset):
        return bisect_right(line_starts, offset) + 1

    tree = Parser(CPP).parse(source)
    for node in changed_nodes(tree.root_node, added_lines, line_at):
        if node.type != "call_expression":
            continue
        call = node.child_by_field_name("arguments")
        if call is None or call.has_error:
            continue
        opening_line = line_at(call.start_byte)
        if opening_line == line_at(call.end_byte - 1):
            continue
        arguments = [child for child in call.named_children if not child.is_extra]
        if len(arguments) < 2:
            continue
        previous_line = opening_line
        for argument in arguments:
            argument_line = line_at(argument.start_byte)
            if argument_line <= previous_line:
                yield argument_line
                break
            previous_line = line_at(argument.end_byte - 1)
