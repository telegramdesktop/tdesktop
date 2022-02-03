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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input_file", type=str, default=os.curdir + "../../ptelegram_changelog.md")
    parser.add_argument("--output_file", type=str, default="ptelegram_cur_changelog.md")

    args = parser.parse_args()
    with open(args.input_file) as reader:
        top_lines = read_top_log(reader.readlines())
        with open(args.output_file, "w") as writer:
            for line in top_lines:
                writer.write(f"{line}")
