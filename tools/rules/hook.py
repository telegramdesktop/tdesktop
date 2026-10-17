#!/usr/bin/env python3
"""Refuse what the house rules ban: as an edit hook, or --staged."""
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
MARKER = "// WHY:"
MARKED_LINES = 3
PLAIN_LINES = 2
MARKED_BLOCKS = 1


def banned():
    """Name and replacement of every entry in CustomFunctions."""
    with open(os.path.join(ROOT, ".clang-tidy"), encoding="utf-8") as stream:
        return re.findall(r"\^::(\w+)\$,([^,;]+),", stream.read())


def hollow(match):
    """The literal's quotes around filler, so offsets stay put."""
    quoted = match.group()
    return quoted[0] + "_" * (len(quoted) - 2) + quoted[-1]


def blanked(line):
    line = re.sub(r'"(?:[^"\\]|\\.)*"', hollow, line)
    return re.sub(r"'(?:[^'\\]|\\.)*'", hollow, line)


def without_comments(text):
    text = "\n".join(blanked(line) for line in text.split("\n"))
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def comment_start(line):
    """Offset of the first comment marker outside literals, or -1."""
    body = blanked(line)
    found = [at for at in (body.find("//"), body.find("/*")) if at >= 0]
    return min(found) if found else -1


def comment_text(line):
    """What a moved comment keeps: its text, not the code beside it."""
    at = comment_start(line)
    return line[at:].strip() if at >= 0 else line.strip()


def classify(line, inside):
    """("full" | "tail" | None, inside a /* */ block after this line)."""
    body = blanked(line)
    if inside:
        return "full", "*/" not in body
    at = comment_start(line)
    if at < 0:
        return None, False
    opens = body[at:].startswith("/*") and "*/" not in body[at + 2:]
    code = body[:at].strip()
    if code == "}" or code.startswith("#"):
        return None, opens  # a closer or directive may carry its label
    return ("tail" if code else "full"), opens


def blocks(numbered):
    """Comment blocks as (line number, lines): runs of whole-line
    comments, and every trailing comment on its own."""
    out, run, first, previous, inside = [], [], 0, None, False
    for number, line in numbered:
        gap = previous is not None and number != previous + 1
        was = inside and not gap
        kind, inside = classify(line, was)
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
    if block[0].strip().startswith(MARKER):
        return (f"a {MARKER} block runs {MARKED_LINES} lines at most"
            if len(block) > MARKED_LINES else None)
    return (f"a comment is one line; up to {MARKED_LINES} only when it opens"
        f" with {MARKER}" if len(block) > 1 else None)


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
    found = [(name, into) for name, into in banned()
        if re.search(r"\b" + name + r"\s*\(", without_comments(text))]
    for name, into in found:
        print(f"{name} is banned in this repository -- write {into} instead.",
            file=sys.stderr)
    was = replaced(payload, path)
    for _, block in ([] if HARNESS in path
            else blocks(list(enumerate(text.split("\n"))))):
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


def staged():
    # No cwd: the hook answers for the repository it was invoked in.
    diff = subprocess.run(["git", "diff", "--cached", "-U0", "-M"],
        capture_output=True, text=True).stdout
    path, number, added, dropped, bad = "", 0, {}, set(), 0
    for line in diff.split("\n"):
        if line.startswith("+++ b/"):
            path, number = line[6:], 0
        elif line.startswith("@@"):
            number = int(re.search(r"\+(\d+)", line).group(1)) - 1
        elif line.startswith("-") and not line.startswith("---"):
            dropped.add(comment_text(line[1:]))
        elif line.startswith("+") and not line.startswith("+++"):
            number += 1
            if path.endswith(SOURCES):
                added.setdefault(path, []).append((number, line[1:]))
    plain, marked = 0, 0
    for path, lines in added.items():
        for name, into in banned():
            for number, line in lines:
                if re.search(r"\b" + name + r"\s*\(", without_comments(line)):
                    print(f"{path}:{number}: {name} is banned -- write"
                        f" {into} instead.", file=sys.stderr)
                    bad += 1
        for number, block in ([] if HARNESS in "/" + path
                else blocks(lines)):
            if any(LICENSE in one for one in block):
                continue
            if all(comment_text(one) in dropped for one in block):
                continue
            why = fault(block)
            if why:
                print(f"{path}:{number}: {why}.", file=sys.stderr)
                bad += 1
            elif block[0].strip().startswith(MARKER):
                marked += 1
            else:
                plain += len(block)
    if plain > PLAIN_LINES:
        print(f"this commit adds {plain} comment lines; {PLAIN_LINES} is the"
            " budget, and one marked block on top of it.", file=sys.stderr)
        bad += 1
    if marked > MARKED_BLOCKS:
        print(f"this commit takes {marked} {MARKER} exceptions;"
            f" {MARKED_BLOCKS} is the budget.", file=sys.stderr)
        bad += 1
    return 1 if bad else 0


def main():
    if "--staged" in sys.argv:
        return staged()
    try:
        payload = json.load(sys.stdin)
    except (ValueError, OSError):
        return 0
    path = ((payload.get("tool_response") or {}).get("filePath")
        or (payload.get("tool_input") or {}).get("file_path") or "")
    return check_edit(payload, path) if path.endswith(SOURCES) else 0


if __name__ == "__main__":
    sys.exit(main())
