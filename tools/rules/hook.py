#!/usr/bin/env python3
"""Refuse what the house rules ban: as an edit hook, or --staged."""
import ast
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SOURCES = (".cpp", ".h", ".mm", ".cxx")
LICENSE = "This file is part of "
HARNESS = "/SourceFiles/test/"
LAW_FIXTURES = "tools/rules/laws/fixtures/"
URL = re.compile(r"https?://")
PLAIN_LINES = 2
CPP_TOKEN = re.compile(
    r'(?P<comment>//(?:[^\r\n]|(?<=\\)\r?\n)*|/\*[\s\S]*?(?:\*/|\Z))'
    r'|(?P<literal>R"(?P<delimiter>[^ ()\\\t\v\f\r\n]{0,16})'
    r'\([\s\S]*?\)(?P=delimiter)"'
    r'|"(?:[^"\\\r\n]|\\(?:\r\n|[\s\S]))*"'
    r"|'(?:[^'\\\r\n]|\\(?:\r\n|[\s\S]))*')"
    r"|\b\d[\w.]*(?:'[\w.]+)*")


def clang_source(path):
    return path.startswith("Telegram/SourceFiles/") and path.endswith((".cpp", ".h"))


def product_source(path):
    relative = path.replace("\\", "/").removeprefix(ROOT.replace("\\", "/") + "/")
    return path.endswith(SOURCES) and not relative.startswith(LAW_FIXTURES)


def banned():
    """Name and replacement of every entry in CustomFunctions."""
    with open(os.path.join(ROOT, ".clang-tidy"), encoding="utf-8") as stream:
        return re.findall(r"\^::(\w+)\$,([^,;]+),", stream.read())


def lexical_views(text):
    literals_masked, code_only, previous = [], [], 0
    for token in CPP_TOKEN.finditer(text):
        if token["literal"] is None and token["comment"] is None:
            continue
        start, end = token.span()
        gap = text[previous:start]
        fragment = token.group()
        if token["literal"] is not None:
            masked = re.sub(r"[^\r\n]", "\0", fragment)
            literals_masked.extend((gap, masked))
        else:
            masked = re.sub(r"[^\r\n]", " ", fragment)
            literals_masked.extend((gap, fragment))
        code_only.extend((gap, masked))
        previous = end
    suffix = text[previous:]
    return "".join(literals_masked) + suffix, "".join(code_only) + suffix


def comment_start(body):
    """Offset of the first comment marker outside literals, or -1."""
    found = [at for at in (body.find("//"), body.find("/*")) if at >= 0]
    return min(found) if found else -1


def comment_text(line):
    """What a moved comment keeps: its text, not the code beside it."""
    at = comment_start(lexical_views(line)[0])
    return line[at:].strip() if at >= 0 else line.strip()


def classify(body, inside):
    """("full" | "tail" | None, inside a /* */ block after this line)."""
    if inside:
        return "full", "*/" not in body
    at = comment_start(body)
    if at < 0:
        return None, False
    opens = body[at:].startswith("/*") and "*/" not in body[at + 2:]
    code = body[:at].strip()
    if code == "}" or code.startswith("#"):
        return None, opens  # a closer or directive may carry its label
    return ("tail" if code else "full"), opens


def blocks(numbered, syntax_lines):
    """Comment blocks as (line number, lines): runs of whole-line
    comments, and every trailing comment on its own."""
    out, run, first, previous, inside = [], [], 0, None, False
    for number, line in numbered:
        gap = previous is not None and number != previous + 1
        was = inside and not gap
        kind, inside = classify(syntax_lines[number - 1], was)
        if run and (gap or kind != "full"):
            out.append((first, run))
            run = []
        if kind == "full" or (kind == "tail" and inside):
            first = first if run else number
            run.append(line)
            if was and not inside:
                out.append((first, run))  # a closed /* */ ends its block
                run = []
        elif kind == "tail":
            out.append((number, [line]))
        previous = number
    if run:
        out.append((first, run))
    return out


def fault(block):
    """Why this comment block breaks the rule, or None."""
    if any(LICENSE in line for line in block):
        return None
    if any(URL.search(line) for line in block):
        return None
    return ("a comment is one line; longer only to carry a reference URL"
        if len(block) > 1 else None)


def written(payload):
    """The text this call put into the file, across all the edit shapes."""
    tool = payload.get("tool_input") or {}
    parts = [tool.get("new_string") or "", tool.get("content") or ""]
    parts += [edit.get("new_string") or ""
        for edit in tool.get("edits") or []]
    return "\n".join(parts)


def replaced(payload, path):
    """What was there before, so moved code is not read as freshly written."""
    tool = payload.get("tool_input") or {}
    parts = [tool.get("old_string") or ""]
    parts += [edit.get("old_string") or ""
        for edit in tool.get("edits") or []]
    if tool.get("content"):
        # Relative to the file, so a submodule answers for its own files.
        parts.append(subprocess.run(
            ["git", "show", "HEAD:./" + os.path.basename(path)],
            cwd=os.path.dirname(path) or ".", capture_output=True,
            text=True).stdout)
    return "\n".join(parts)


def check_edit(payload, path):
    text = written(payload)
    syntax, code = lexical_views(text)
    found = [(name, into) for name, into in banned()
        if re.search(r"\b" + name + r"\s*\(", code)]
    for name, into in found:
        print(f"{name} is banned in this repository -- write {into} instead.",
            file=sys.stderr)
    was = replaced(payload, path)
    numbers = enumerate(text.split("\n"), 1)
    for _, block in ([] if HARNESS in path
            else blocks(numbers, syntax.split("\n"))):
        why = fault(block)
        if why and "\n".join(block) not in was:
            found.append(True)
            print(f"{len(block)}-line comment: {why}.", file=sys.stderr)
    if not found:
        return 0
    print(f"The rules live in .clang-tidy and AGENTS.md. Fix"
        f" {os.path.basename(path)} now, before anything else.",
        file=sys.stderr)
    return 2


def staged_changes():
    # No cwd: the hook answers for the repository it was invoked in.
    diff = subprocess.check_output([
        "git", "-c", "core.quotePath=true", "diff", "--cached", "-U0", "-M",
        "--no-color", "--no-ext-diff", "--no-textconv",
        "--src-prefix=a/", "--dst-prefix=b/", "--ignore-submodules=all",
    ], text=True)
    path, number, added, dropped, in_hunk = "", 0, {}, set(), False
    clang_file, clang_deletions = False, {}
    for line in diff.split("\n"):
        if line.startswith("diff --git "):
            path, in_hunk = "", False
        elif not in_hunk and line.startswith("+++ "):
            name = line[4:].rstrip("\t")
            if name.startswith('"'):
                name = os.fsdecode(ast.literal_eval("b" + name))
            path = name[2:] if name.startswith("b/") else ""
            clang_file = clang_source(path)
        elif line.startswith("@@"):
            in_hunk = True
            start, count = re.search(r"\+(\d+)(?:,(\d+))?", line).groups()
            number = int(start) - 1
            if clang_file and count == "0":
                clang_deletions.setdefault(path, set()).update(
                    (max(1, int(start)), int(start) + 1))
        elif in_hunk and line.startswith("-"):
            dropped.add(comment_text(line[1:]))
        elif in_hunk and line.startswith(" "):
            number += 1
        elif in_hunk and line.startswith("+"):
            number += 1
            if product_source(path):
                added.setdefault(path, []).append((number, line[1:]))
    return added, dropped, clang_deletions


def staged():
    try:
        added, dropped, clang_deletions = staged_changes()
    except subprocess.CalledProcessError as error:
        return error.returncode
    if added:
        try:
            from cpp_layout import MAX_COLUMNS, TAB_WIDTH, layout_errors, line_width
        except ImportError:
            requirements = os.path.join(HERE, "requirements.txt")
            print(f'Install the C++ parser with: python3 -m pip install -r'
                f' "{requirements}"', file=sys.stderr)
            return 1
    plain, bad = 0, 0
    clang_sources = {}
    for path, lines in added.items():
        blob = subprocess.run(["git", "show", ":" + path], stdout=subprocess.PIPE)
        if blob.returncode:
            return blob.returncode
        syntax, code = lexical_views(blob.stdout.decode("utf-8", "surrogateescape"))
        code_lines = code.split("\n")
        added_lines = [number for number, _ in lines]
        if clang_source(path):
            clang_sources[path] = (
                set(added_lines).union(clang_deletions.pop(path, ())), blob.stdout)
        for number, message in sorted(set(layout_errors(blob.stdout, added_lines, path))):
            print(f"{path}:{number}: {message}", file=sys.stderr)
            bad += 1
        for number, line in lines:
            width = line_width(line)
            if width > MAX_COLUMNS:
                print(f"{path}:{number}: {width} columns (limit {MAX_COLUMNS},"
                    f" tab width {TAB_WIDTH}). Wrap the line.", file=sys.stderr)
                bad += 1
        for name, into in banned():
            for number in added_lines:
                if re.search(r"\b" + name + r"\s*\(", code_lines[number - 1]):
                    print(f"{path}:{number}: {name} is banned -- write"
                        f" {into} instead.", file=sys.stderr)
                    bad += 1
        for number, block in ([] if HARNESS in "/" + path
                else blocks(lines, syntax.split("\n"))):
            if any(LICENSE in one for one in block):
                continue
            if all(comment_text(one) in dropped for one in block):
                continue
            why = fault(block)
            if why:
                print(f"{path}:{number}: {why}.", file=sys.stderr)
                bad += 1
            elif len(block) == 1:
                plain += 1
    if plain > PLAIN_LINES:
        print(f"this commit adds {plain} comment lines; {PLAIN_LINES} is the"
            " budget.", file=sys.stderr)
        bad += 1
    if bad:
        return 1
    for path, lines in clang_deletions.items():
        clang_sources[path] = (lines, None)
    from clang_style import check_staged
    return check_staged(clang_sources)


def main():
    if "--staged" in sys.argv:
        return staged()
    try:
        payload = json.load(sys.stdin)
    except (ValueError, OSError):
        return 0
    path = ((payload.get("tool_response") or {}).get("filePath")
        or (payload.get("tool_input") or {}).get("file_path") or "")
    return check_edit(payload, path) if product_source(path) else 0


if __name__ == "__main__":
    sys.exit(main())
