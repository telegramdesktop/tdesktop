from bisect import bisect_left, bisect_right
import re

from tree_sitter import Language, Parser
import tree_sitter_cpp


CPP = Language(tree_sitter_cpp.language())
CALL_MESSAGE = ("Put each argument of the multiline call on its own line"
    " after the opening parenthesis.")
SUM_MESSAGE = "Start each continuation term on its own line with + or -."


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


def packed_call(node, line_at):
    call = node.child_by_field_name("arguments")
    if call is None or call.has_error:
        return None
    opening_line = line_at(call.start_byte)
    if opening_line == line_at(call.end_byte - 1):
        return None
    arguments = [child for child in call.named_children if not child.is_extra]
    if len(arguments) < 2:
        return None
    previous_line = opening_line
    for argument in arguments:
        argument_line = line_at(argument.start_byte)
        if argument_line <= previous_line:
            return argument_line
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


def layout_errors(source, added_lines):
    line_starts = [match.end() for match in re.finditer(b"\n", source)]

    def line_at(offset):
        return bisect_right(line_starts, offset) + 1

    tree = Parser(CPP).parse(source)
    for node in changed_nodes(tree.root_node, added_lines, line_at):
        if node.type == "call_expression":
            if (line := packed_call(node, line_at)) is not None:
                yield line, CALL_MESSAGE
        elif additive_operator(node) is not None:
            for line in packed_terms(node, line_at):
                yield line, SUM_MESSAGE
