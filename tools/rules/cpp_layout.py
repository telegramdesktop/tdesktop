from bisect import bisect_left, bisect_right
from pathlib import Path
import argparse
import json
import os
import re

from tree_sitter import Parser

from laws import closing_parenthesis


CATALOG = Path(os.environ.get("STYLECHECK_CATALOG",
    Path(__file__).with_name("laws") / "catalog.json"))
LAYOUT = json.loads(CATALOG.read_bytes())["layout"]
MAX_COLUMNS = LAYOUT["columns"]
TAB_WIDTH = LAYOUT["tab_width"]
CPP = closing_parenthesis.CPP
CALL_MESSAGE = ("Put each argument of the multiline call on its own line"
    " after the opening parenthesis.")
CLOSE_ARGUMENT_MESSAGE = "Keep the closing parenthesis with the final argument."
LAMBDA_PARAMETER_MESSAGE = "Keep the sole lambda parameter on the same line as the capture."
LAMBDA_BODY_MESSAGE = "Keep the short lambda body on one line."
PIPELINE_MESSAGE = "Wrap before the producer closing parenthesis and keep the short RPL stage together."
PIPELINE_BAR_MESSAGE = "Keep the RPL pipe with the closing parenthesis of the preceding stage."
MEMBER_CALL_MESSAGE = "Keep the method name with its receiver and wrap the arguments."
SUM_MESSAGE = "Start each continuation term on its own line with + or -."
LOGICAL_MESSAGE = "Start the continuation with {operator} and its right operand."
ASSIGNMENT_MESSAGE = "Start the continuation with = and its right operand."
EMPTY_MARKED_MESSAGE = "Use tr::marked() to create empty TextWithEntities."
INCLUDE_ORDER_MESSAGE = "Sort the include block with the Python formatter."
INLINE_METHOD_MESSAGE = "Define the method outside the class body."
INITIALIZER_ENTRY_MESSAGE = "Put each entry of the multiline initializer on its own line."
TERNARY_BRACE_MESSAGE = "Align the closing brace with the ternary branch marker."
SUM_OPERATORS = ("+", "-")
LOGICAL_OPERATORS = ("&&", "||")


def starts_of_lines(source):
    return [0] + [match.end() for match in re.finditer(b"\n", source)]


def line_width(line):
    return len(line.rstrip("\r\n").expandtabs(TAB_WIDTH))


def fits_width(line):
    return line_width(line.decode("utf-8", "surrogateescape")) <= MAX_COLUMNS


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


def inline_lambda_branches(node, arguments, opening_line, line_at):
    function = node.child_by_field_name("function")
    field = function.child_by_field_name("field")
    if function.text == b"v::match":
        branches = arguments[1:]
    elif ((field is not None and field.text == b"match")
            or (function.text == b"enumerate" and len(arguments) == 2)):
        branches = arguments
    else:
        return False
    return (line_at(branches[0].start_byte) == opening_line
        and all(branch.type == "lambda_expression" for branch in branches)
        and all(line_at(first.start_byte) < line_at(second.start_byte)
            for first, second in zip(branches, branches[1:])))


def inline_rpl_handler(node, arguments, opening_line, line_at):
    if len(arguments) != 2:
        return False
    handler, lifetime = arguments
    return (node.child_by_field_name("function").text == b"rpl::on_next"
        and handler.type == "lambda_expression"
        and lifetime.type != "lambda_expression"
        and line_at(handler.start_byte) == opening_line
        and line_at(handler.end_byte - 1) > opening_line
        and line_at(lifetime.start_byte) == line_at(lifetime.end_byte - 1))


def inline_final_lambda(arguments, opening_line, source, line_starts, line_at):
    if len(arguments) != 2:
        return False
    value, callback = arguments
    if value.type == "lambda_expression" or callback.type != "lambda_expression":
        return False
    body = callback.child_by_field_name("body")
    return (body is not None
        and line_at(body.start_byte) == opening_line
        and fits_width(source[line_starts[opening_line - 1]:body.start_byte + 1]))


def call_layout_error(node, source, line_starts, line_at):
    call = node.child_by_field_name("arguments")
    if call is None or call.has_error:
        return None
    opening_line = line_at(call.start_byte)
    closing_line = line_at(call.end_byte - 1)
    if opening_line == closing_line:
        return None
    children = call.named_children
    arguments = [child for child in children if not child.is_extra]
    if len(arguments) <= 1:
        return None
    if (inline_lambda_branches(node, arguments, opening_line, line_at)
            or inline_rpl_handler(node, arguments, opening_line, line_at)
            or inline_final_lambda(
                arguments, opening_line, source, line_starts, line_at)):
        return None
    previous_line = opening_line
    for argument in arguments:
        argument_line = line_at(argument.start_byte)
        if argument_line <= previous_line:
            return argument_line, CALL_MESSAGE
        previous_line = line_at(argument.end_byte - 1)
    return None


def binary_operator(node, types):
    if node is not None and node.type == "binary_expression":
        operator = node.child_by_field_name("operator")
        if operator is not None and operator.type in types:
            return operator
    return None


def term_lines(node, operator, line_at):
    left = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    return (line_at(left.end_byte - 1), line_at(operator.start_byte),
        line_at(right.start_byte))


def packed_terms(node, line_at):
    if node.has_error or line_at(node.start_byte) == line_at(node.end_byte - 1):
        return
    parent = node.parent
    if (binary_operator(parent, SUM_OPERATORS) is not None
            and parent.child_by_field_name("left") == node):
        return
    while (operator := binary_operator(node, SUM_OPERATORS)) is not None:
        left_line, operator_line, right_line = term_lines(node, operator, line_at)
        if operator_line == left_line or operator_line != right_line:
            yield operator_line
        node = node.child_by_field_name("left")


def packed_logical_term(node, source, added_lines, line_at):
    if node.has_error:
        return None
    operator = binary_operator(node, LOGICAL_OPERATORS)
    if operator is None:
        return None
    chain = node
    while binary_operator(chain.parent, LOGICAL_OPERATORS) is not None:
        chain = chain.parent
    if line_at(chain.start_byte) == line_at(chain.end_byte - 1):
        return None
    left_line, operator_line, right_line = term_lines(node, operator, line_at)
    if operator_line != left_line and operator_line == right_line:
        return None
    right = node.child_by_field_name("right")
    if operator_line != right_line and source[operator.end_byte:right.start_byte].strip():
        return None
    change = bisect_left(added_lines, left_line)
    if change < len(added_lines) and added_lines[change] <= right_line:
        return operator_line, LOGICAL_MESSAGE.format(operator=operator.type)
    return None


def assignment_layout_error(node, source, added_lines, line_at):
    if node.has_error:
        return None
    if node.type == "init_declarator":
        left = node.child_by_field_name("declarator")
        right = node.child_by_field_name("value")
    else:
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
    equal = next((child for child in node.children if child.type == "="), None)
    if left is None or right is None or equal is None:
        return None
    line = line_at(equal.start_byte)
    continuation = line_at(right.start_byte)
    if (line != line_at(left.end_byte - 1) or line >= continuation
            or source[equal.end_byte:right.start_byte].strip()):
        return None
    change = bisect_left(added_lines, line)
    if change < len(added_lines) and added_lines[change] <= continuation:
        return line, ASSIGNMENT_MESSAGE
    return None


def empty_marked_temporary(node):
    if node.has_error:
        return None
    if node.type == "call_expression":
        name = node.child_by_field_name("function")
        arguments = node.child_by_field_name("arguments")
    else:
        name = node.child_by_field_name("type")
        arguments = node.child_by_field_name("value")
    if (name is not None and name.text == b"TextWithEntities"
            and arguments is not None and not arguments.named_child_count):
        return name
    return None


def include_blocks(source, root, line_starts, line_at):
    pending = [root]
    while pending:
        block = []
        for node in pending.pop().named_children:
            path = node.child_by_field_name("path") if node.type == "preproc_include" else None
            if path is not None and path.type == "string_literal":
                start = line_starts[line_at(node.start_byte) - 1]
                end = node.end_byte
                line = source[start:end]
                if (line.endswith(b"\n") and b"\n" not in line[:-1]
                        and not source[start:node.start_byte].strip()
                        and (not block or not source[block[-1][1]:start].strip())):
                    block.append((start, end, path.text, line))
                    continue
            if block and node.type == "comment" and node.start_byte < block[-1][1]:
                continue
            if block:
                yield block
                block = []
            if node.type in ("preproc_if", "preproc_ifdef", "preproc_elif", "preproc_else"):
                pending.append(node)
        if block:
            yield block


def include_sort_key(name):
    first, *parts = name[1:-1].split(b"/")
    return first, tuple((index == len(parts) - 1, part) for index, part in enumerate(parts))


def include_edits(source, root, path, line_starts, line_at):
    own = os.fsencode(Path(path).with_suffix(".h").as_posix()) if path else None
    for block in include_blocks(source, root, line_starts, line_at):
        groups = [[], [], []]
        for _, _, name, line in block:
            header = name[1:-1]
            if own is not None and (own == header or own.endswith(b"/" + header)):
                group = 0
            elif name.startswith(b'"styles/style_'):
                group = 2
            else:
                group = 1
            groups[group].append((name, line))
        newline = b"\r\n" if block[0][3].endswith(b"\r\n") else b"\n"
        formatted = newline.join(b"".join(line for _, line in sorted(
            group, key=lambda entry: include_sort_key(entry[0]))) for group in groups if group)
        first, last = block[0][0], block[-1][1]
        original = source[first:last]
        if formatted != original:
            yield first, last, formatted


def fix_includes(source, path):
    line_starts = starts_of_lines(source)

    def line_at(offset):
        return bisect_right(line_starts, offset)

    root = Parser(CPP).parse(source).root_node
    return apply_layout_edits(source, include_edits(source, root, path, line_starts, line_at))


def inline_method_error(node, added_lines, line_at):
    owner = node.parent
    if owner is None or owner.type != "field_declaration_list":
        return None
    owner = owner.parent
    if owner is None or owner.type != "class_specifier":
        return None
    while (owner := owner.parent) is not None:
        if owner.type in ("class_specifier", "struct_specifier", "function_definition",
                "lambda_expression", "template_declaration"):
            return None
    body = node.child_by_field_name("body")
    if body is None or body.type != "compound_statement":
        return None
    first = line_at(node.start_byte)
    body_line = line_at(body.start_byte)
    change = bisect_left(added_lines, first)
    if (body_line < line_at(body.end_byte - 1)
            and change < len(added_lines) and added_lines[change] <= body_line):
        return first, INLINE_METHOD_MESSAGE
    return None


def initializer_layout_errors(node, source, line_starts, added_lines, line_at):
    if node.has_error:
        return
    first, last = line_at(node.start_byte), line_at(node.end_byte - 1)
    if first == last:
        return
    entries = [child for child in node.named_children if not child.is_extra]
    parent = node.parent
    call = parent.parent if parent is not None and parent.type == "argument_list" else None
    callee = call.child_by_field_name("function") if call is not None else None
    if (entries and all(entry.type == "initializer_pair" for entry in entries)
            or callee is not None and callee.text in (b"std::min", b"std::max")):
        for previous, entry in zip(entries, entries[1:]):
            line = line_at(entry.start_byte)
            if line == line_at(previous.end_byte - 1) and line in added_lines:
                yield line, INITIALIZER_ENTRY_MESSAGE
    if parent is None or parent.type != "compound_literal_expression":
        return
    marker = parent.prev_sibling
    if marker is None or marker.type not in ("?", ":") or line_at(marker.start_byte) != first:
        return
    branch_indent = source[line_starts[first - 1]:marker.start_byte]
    closer_indent = source[line_starts[last - 1]:node.end_byte - 1]
    if (not branch_indent.strip() and not closer_indent.strip()
            and branch_indent != closer_indent and (first in added_lines or last in added_lines)):
        yield last, TERNARY_BRACE_MESSAGE


def lambda_parameter_join(node, source, line_starts, line_at):
    declarator = node.child_by_field_name("declarator")
    body = node.child_by_field_name("body")
    if node.has_error or declarator is None or body is None:
        return None
    parameters = declarator.child_by_field_name("parameters")
    if parameters is None or parameters.named_child_count != 1:
        return None
    parameter = parameters.named_children[0]
    if parameter.type not in ("parameter_declaration", "optional_parameter_declaration"):
        return None
    opening, closing = parameters.start_byte, parameters.end_byte - 1
    first, last = line_at(opening), line_at(body.start_byte)
    if (first == last or line_at(node.start_byte) != first
            or line_at(closing) != last
            or line_at(parameter.start_byte) != line_at(parameter.end_byte - 1)
            or source[opening + 1:parameter.start_byte].strip()
            or source[parameter.end_byte:closing].strip()):
        return None

    end = line_starts[last] if last < len(line_starts) else len(source)
    joined = (source[line_starts[first - 1]:opening + 1] + parameter.text
        + source[closing:end].rstrip(b"\r\n"))
    if fits_width(joined):
        return opening + 1, closing, parameter.text
    return None


def lambda_body_join(node, source, line_starts, line_at, parameter):
    body = node.child_by_field_name("body")
    if node.has_error or body is None or body.named_child_count != 1:
        return None
    statement = body.named_children[0]
    if statement.type not in ("expression_statement", "return_statement"):
        return None
    opening, closing = body.start_byte, body.end_byte - 1
    first, last = line_at(node.start_byte), line_at(closing)
    if (line_at(opening) == last
            or (line_at(opening) != first and parameter is None)
            or line_at(statement.start_byte) != line_at(statement.end_byte - 1)
            or source[opening + 1:statement.start_byte].strip()
            or source[statement.end_byte:closing].strip()):
        return None

    arguments = node.parent
    if arguments is not None and arguments.type == "argument_list":
        complete_call = (line_at(arguments.start_byte) == first
            and line_at(arguments.end_byte - 1) == last)
        others = [child for child in arguments.named_children
            if child != node and not child.is_extra]
        if others and not complete_call and (
                line_at(arguments.start_byte) == first
                or any(line_at(child.start_byte) == last for child in others)):
            return None

    start = opening + 1
    replacement = b" " + statement.text + b" "
    if parameter is not None:
        start, end, value = parameter
        replacement = value + source[end:opening + 1] + replacement
    eol = line_starts[last] if last < len(line_starts) else len(source)
    joined = (source[line_starts[first - 1]:start] + replacement
        + source[closing:eol].rstrip(b"\r\n"))
    if fits_width(joined):
        return start, closing, replacement
    return None


def pipeline_stage_join(node, source, line_starts, line_at):
    operator = binary_operator(node, ("|",))
    if operator is None or node.has_error:
        return None
    left = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    if left.type != "call_expression" or right.type != "call_expression":
        return None
    function = right.child_by_field_name("function")
    if not function.text.startswith(b"rpl::"):
        return None
    producer = left.child_by_field_name("arguments")
    stage = right.child_by_field_name("arguments")
    if producer.named_child_count or stage.named_child_count != 1:
        return None
    argument = stage.named_children[0]
    if argument.is_extra:
        return None

    end_producer = producer.end_byte - 1
    start_stage = stage.start_byte + 1
    first, last = line_at(end_producer), line_at(argument.start_byte)
    gap = source[start_stage:argument.start_byte]
    if (last != first + 1 or line_at(left.start_byte) != first
            or line_at(start_stage - 1) != first
            or line_at(stage.end_byte - 1) != last
            or source[producer.start_byte + 1:end_producer].strip()
            or source[producer.end_byte:operator.start_byte].strip()
            or source[operator.end_byte:right.start_byte].strip()
            or source[function.end_byte:stage.start_byte].strip()
            or gap.strip()):
        return None

    prefix = source[line_starts[first - 1]:end_producer]
    indent = prefix[:len(prefix) - len(prefix.lstrip(b" \t"))]
    end = line_starts[last] if last < len(line_starts) else len(source)
    stage_prefix = indent + source[end_producer:start_stage]
    continuation = stage_prefix + source[argument.start_byte:end].rstrip(b"\r\n")
    if fits_width(prefix) and fits_width(continuation):
        newline = b"\r\n" if b"\r\n" in gap else b"\n"
        return end_producer, argument.start_byte, newline + stage_prefix
    return None


def rpl_arguments(node):
    if node.type != "call_expression":
        return None
    function = node.child_by_field_name("function")
    if function is None or not function.text.startswith(b"rpl::"):
        return None
    return node.child_by_field_name("arguments")


def pipeline_arguments_wrap(arguments, source, line_starts, line_at, indent, newline):
    opening, closing = arguments.start_byte, arguments.end_byte - 1
    first, last = line_at(opening), line_at(closing)
    if first != last:
        prefix = source[line_starts[last - 1]:closing]
        if not prefix.strip():
            return closing, b""
        if fits_width(prefix.rstrip(b" \t")):
            return len(source[:closing].rstrip(b" \t")), newline + indent
        return None

    header = source[line_starts[first - 1]:opening + 1]
    if not fits_width(header):
        return None
    values = []
    start = opening + 1
    for argument in arguments.named_children:
        gap = source[start:argument.start_byte].strip()
        if argument.is_extra or gap != (b"," if values else b""):
            return None
        value = indent + b"\t" + argument.text
        if not fits_width(value):
            return None
        values.append(value)
        start = argument.end_byte
    if source[start:closing].strip() or any(
            not fits_width(value + b",") for value in values[:-1]):
        return None
    return opening + 1, newline + (b"," + newline).join(values) + (
        newline + indent if values else indent)


def leading_pipeline_layout(node, source, line_starts, line_at):
    operator = binary_operator(node, ("|",))
    if operator is None or node.has_error:
        return None
    left = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    preceding = left.child_by_field_name("right") if binary_operator(left, ("|",)) else left
    arguments = rpl_arguments(preceding)
    if arguments is None or rpl_arguments(right) is None:
        return None

    chain = node
    while binary_operator(chain.parent, ("|",)) is not None:
        chain = chain.parent
    if chain.parent is not None and chain.parent.type == "argument_list":
        return None

    closing = arguments.end_byte - 1
    previous, bar = line_at(closing), line_at(operator.start_byte)
    gap = source[closing + 1:operator.start_byte]
    if (previous >= bar or source[line_starts[bar - 1]:operator.start_byte].strip()
            or gap.strip()):
        return None

    first = line_at(chain.start_byte)
    prefix = source[line_starts[first - 1]:chain.start_byte]
    indent = prefix[:len(prefix) - len(prefix.lstrip(b" \t"))]
    newline = b"\r\n" if b"\r\n" in gap else b"\n"
    plan = pipeline_arguments_wrap(
        arguments, source, line_starts, line_at, indent, newline)
    edit = None
    end = line_starts[bar] if bar < len(line_starts) else len(source)
    suffix = source[operator.start_byte:end].rstrip(b"\r\n")
    if plan is not None:
        start, prefix = plan
        if start == closing:
            indent = source[line_starts[previous - 1]:closing]
        if fits_width(indent + b") " + suffix):
            edit = start, operator.start_byte, prefix + b") "
    return previous, bar, edit


def compact_argument(argument, source, line_at):
    if line_at(argument.start_byte) == line_at(argument.end_byte - 1):
        return argument.text
    if argument.type == "conditional_expression":
        parts = [argument.child_by_field_name(name)
            for name in ("condition", "consequence", "alternative")]
        if any(part is None or line_at(part.start_byte) != line_at(part.end_byte - 1)
                for part in parts):
            return None
        condition, yes, no = parts
        if (source[condition.end_byte:yes.start_byte].strip() != b"?"
                or source[yes.end_byte:no.start_byte].strip() != b":"):
            return None
        return condition.text + b" ? " + yes.text + b" : " + no.text
    if argument.type != "call_expression":
        return None
    arguments = argument.child_by_field_name("arguments")
    if (arguments.named_child_count != 1
            or line_at(arguments.start_byte) != line_at(argument.start_byte)):
        return None
    value = arguments.named_children[0]
    start, end = arguments.start_byte + 1, arguments.end_byte - 1
    if (value.is_extra
            or line_at(value.start_byte) != line_at(value.end_byte - 1)
            or source[start:value.start_byte].strip()
            or source[value.end_byte:end].strip()):
        return None
    return source[argument.start_byte:start] + value.text + b")"


def member_argument_wrap(arguments, source, line_starts, line_at, header, indent, newline):
    if arguments.named_child_count != 1:
        return None
    argument = arguments.named_children[0]
    opening, closing = arguments.start_byte, arguments.end_byte - 1
    if (argument.is_extra
            or source[opening + 1:argument.start_byte].strip()
            or source[argument.end_byte:closing].strip()):
        return None
    value = compact_argument(argument, source, line_at)
    if value is None:
        return None
    last = line_at(closing)
    end = line_starts[last] if last < len(line_starts) else len(source)
    suffix = source[closing:end].rstrip(b"\r\n")
    if fits_width(header + value + suffix):
        return value
    continuation = indent + b"\t" + value
    if fits_width(continuation + suffix):
        return newline + continuation
    return None


def member_call_layout(node, source, line_starts, line_at):
    function = node.child_by_field_name("function")
    if node.has_error or function is None or function.type != "field_expression":
        return None
    receiver = function.child_by_field_name("argument")
    operator = function.child_by_field_name("operator")
    arguments = node.child_by_field_name("arguments")
    if receiver is None or receiver.type not in ("identifier", "this"):
        return None
    if operator is None or arguments is None:
        return None
    first, arrow = line_at(receiver.start_byte), line_at(operator.start_byte)
    gap = source[receiver.end_byte:operator.start_byte]
    prefix = source[line_starts[first - 1]:receiver.start_byte]
    marker = prefix.lstrip(b" \t")
    indent = prefix[:len(prefix) - len(marker)]
    branch = (marker.rstrip(b" \t") in (b"?", b":")
        and node.parent is not None and node.parent.type == "conditional_expression")
    if (operator.type != "->" or marker) and not (operator.type == "." and branch):
        return None
    if (arrow != first + 1 or line_at(arguments.start_byte) != arrow
            or gap.strip()
            or any(child.is_extra for child in function.named_children)):
        return None
    head = source[operator.start_byte:arguments.start_byte + 1]
    header = prefix + receiver.text + head
    if not fits_width(header):
        return None
    newline = b"\r\n" if b"\r\n" in gap else b"\n"
    wrap = member_argument_wrap(
        arguments, source, line_starts, line_at, header, indent, newline)
    edit = ((receiver.end_byte, arguments.end_byte - 1, head + wrap)
        if wrap is not None else None)
    return first, arrow, edit


def layout_join(node, source, line_starts, line_at):
    if node.type == "lambda_expression":
        parameter = lambda_parameter_join(node, source, line_starts, line_at)
        body = lambda_body_join(node, source, line_starts, line_at, parameter)
        if body is not None:
            return body, LAMBDA_BODY_MESSAGE
        if parameter is not None:
            return parameter, LAMBDA_PARAMETER_MESSAGE
    elif node.type == "binary_expression":
        edit = pipeline_stage_join(node, source, line_starts, line_at)
        if edit is not None:
            return edit, PIPELINE_MESSAGE
    return None


def layout_errors(source, added_lines, path=""):
    line_starts = starts_of_lines(source)

    def line_at(offset):
        return bisect_right(line_starts, offset)

    tree = Parser(CPP).parse(source)
    for first, last, formatted in include_edits(
            source, tree.root_node, path, line_starts, line_at):
        change = bisect_left(added_lines, line_at(first))
        if change < len(added_lines) and added_lines[change] <= line_at(last - 1):
            difference = next((offset for offset, (left, right)
                in enumerate(zip(source[first:last], formatted)) if left != right),
                min(last - first, len(formatted)))
            yield line_at(first + difference), INCLUDE_ORDER_MESSAGE
    findings = closing_parenthesis.inspect(source, LAYOUT, tree.root_node)
    for finding in closing_parenthesis.select(findings, ((line, line) for line in added_lines)):
        yield finding.last_line, CLOSE_ARGUMENT_MESSAGE
    for node in changed_nodes(tree.root_node, added_lines, line_at):
        if node.type in ("call_expression", "compound_literal_expression"):
            name = empty_marked_temporary(node)
            if name is not None:
                yield line_at(name.start_byte), EMPTY_MARKED_MESSAGE
        joined = layout_join(node, source, line_starts, line_at)
        if joined is not None:
            edit, message = joined
            first, last = line_at(edit[0]), line_at(edit[1])
            change = bisect_left(added_lines, first)
            if change < len(added_lines) and added_lines[change] <= last:
                yield first, message
        if node.type == "call_expression":
            split = member_call_layout(node, source, line_starts, line_at)
            if split is not None:
                first, arrow, _ = split
                change = bisect_left(added_lines, first)
                if change < len(added_lines) and added_lines[change] <= arrow:
                    yield first, MEMBER_CALL_MESSAGE
            error = call_layout_error(node, source, line_starts, line_at)
            if error is not None:
                yield error
        elif node.type in ("init_declarator", "assignment_expression"):
            error = assignment_layout_error(node, source, added_lines, line_at)
            if error is not None:
                yield error
        elif node.type == "function_definition" and path.endswith((".cpp", ".cxx", ".mm")):
            error = inline_method_error(node, added_lines, line_at)
            if error is not None:
                yield error
        elif node.type == "initializer_list":
            yield from initializer_layout_errors(node, source, line_starts, added_lines, line_at)
        elif node.type == "binary_expression":
            split = leading_pipeline_layout(node, source, line_starts, line_at)
            if split is not None:
                previous, bar, _ = split
                change = bisect_left(added_lines, previous)
                if change < len(added_lines) and added_lines[change] <= bar:
                    yield bar, PIPELINE_BAR_MESSAGE
            if binary_operator(node, SUM_OPERATORS) is not None:
                for line in packed_terms(node, line_at):
                    yield line, SUM_MESSAGE
            else:
                error = packed_logical_term(node, source, added_lines, line_at)
                if error is not None:
                    yield error


def apply_layout_edits(source, edits):
    chunks, start = [], 0
    for first, last, replacement in sorted(edits):
        chunks.extend((source[start:first], replacement))
        start = last
    chunks.append(source[start:])
    return b"".join(chunks)


def join_short_layouts(source):
    line_starts = starts_of_lines(source)

    def line_at(offset):
        return bisect_right(line_starts, offset)

    tree = Parser(CPP).parse(source)
    edits = []
    for node in changed_nodes(tree.root_node, range(1, len(line_starts) + 1), line_at):
        joined = layout_join(node, source, line_starts, line_at)
        if joined is not None:
            edits.append(joined[0])
        elif node.type == "call_expression":
            split = member_call_layout(node, source, line_starts, line_at)
            if split is not None and split[2] is not None:
                edits.append(split[2])
        elif node.type == "binary_expression":
            split = leading_pipeline_layout(node, source, line_starts, line_at)
            if split is not None and split[2] is not None:
                edits.append(split[2])
    return apply_layout_edits(source, edits)


def main():
    parser = argparse.ArgumentParser(description="Fix supported C++ line layouts.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--fix", nargs="+", type=Path, metavar="FILE")
    mode.add_argument("--fix-includes", nargs="+", type=Path, metavar="FILE")
    args = parser.parse_args()
    for path in args.fix or args.fix_includes:
        source = path.read_bytes()
        formatted = fix_includes(source, path)
        if args.fix:
            joined = join_short_layouts(formatted)
            formatted = closing_parenthesis.apply(joined,
                closing_parenthesis.inspect(joined, LAYOUT))
        if formatted != source:
            path.write_bytes(formatted)


if __name__ == "__main__":
    main()
