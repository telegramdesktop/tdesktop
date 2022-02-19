import sys
import os
import argparse
import typing as tp
import re


def read_top_log(lines: tp.List[str]) -> tp.List[str]:
    top_lines = []
    pattern = re.compile("^v. [0-9]+\.[0-9]+\.[0-9]+$")
    for i, line in enumerate(lines):
        if pattern.match(line) and i > 0:
            break
        top_lines.append(line)
    return top_lines


def read_version_log(lines: tp.List[str], version: str) -> tp.List[str]:
    top_lines = []
    pattern = re.compile("^v. [0-9]+\.[0-9]+\.[0-9]+$")
    was_found = False
    for i, line in enumerate(lines):
        matched = pattern.match(line)
        if matched and was_found:
            break
        elif matched and line[2:].strip() == version.strip():  # Skip `v.`
            was_found = True
        if was_found:
            top_lines.append(line)
    return top_lines


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input_file", type=str, default=os.curdir + "../../ptelegram_changelog.md")
    parser.add_argument("--output_file", type=str, default="ptelegram_cur_changelog.md")
    parser.add_argument("--version", type=str, default=None)

    args = parser.parse_args()
    with open(args.input_file) as reader:
        if args.version is None:
            top_lines = read_top_log(reader.readlines())
        else:
            top_lines = read_version_log(reader.readlines(), args.version)
        with open(args.output_file, "w") as writer:
            for line in top_lines:
                writer.write(f"{line}")
