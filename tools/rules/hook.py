#!/usr/bin/env python3
"""Refuse a function .clang-tidy bans: as an edit hook, or --staged."""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SOURCES = (".cpp", ".h", ".mm", ".cxx")


def banned():
    """Name and replacement of every entry in CustomFunctions."""
    with open(os.path.join(ROOT, ".clang-tidy"), encoding="utf-8") as stream:
        return re.findall(r"\^::(\w+)\$,([^,;]+),", stream.read())


def written(payload):
    """The text this call put into the file, across all the edit shapes."""
    tool = payload.get("tool_input") or {}
    parts = [tool.get("new_string") or "", tool.get("content") or ""]
    parts += [edit.get("new_string") or ""
        for edit in tool.get("edits") or []]
    return "\n".join(parts)


def code_only(text):
    """The same text with comments and string literals taken out."""
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)


def complain(path, found, where):
    for name, into in found:
        print(f"{where}{name} is banned in this repository -- write {into}"
            " instead.", file=sys.stderr)
    print(f"The rule lives in .clang-tidy. Fix {os.path.basename(path)} now,"
        " before anything else.", file=sys.stderr)


def staged():
    """Every banned name added by the staged change, with its line."""
    diff = subprocess.run(["git", "diff", "--cached", "-U0"], cwd=ROOT,
        capture_output=True, text=True).stdout
    path, number, bad = "", 0, []
    for line in diff.split("\n"):
        if line.startswith("+++ b/"):
            path, number = line[6:], 0
        elif line.startswith("@@"):
            number = int(re.search(r"\+(\d+)", line).group(1)) - 1
        elif line.startswith("+") and not line.startswith("+++"):
            number += 1
            if not path.endswith(SOURCES):
                continue
            found = [(name, into) for name, into in banned()
                if re.search(r"\b" + name + r"\s*\(", code_only(line[1:]))]
            if found:
                complain(path, found, f"{path}:{number}: ")
                bad += found
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
    if not path.endswith(SOURCES):
        return 0
    text = code_only(written(payload))
    found = [(name, into) for name, into in banned()
        if re.search(r"\b" + name + r"\s*\(", text)]
    if not found:
        return 0
    complain(path, found, "")
    return 2


if __name__ == "__main__":
    sys.exit(main())
