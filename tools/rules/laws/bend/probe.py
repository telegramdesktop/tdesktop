import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from typing import NamedTuple

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from run import (
    check_plugin_layout, clang_missing, clang_style, cpp_layout, default_plugin,
    fixture, load_catalog, run_engine, token_nodes,
)


class Call(NamedTuple):
    opening: int
    first: int
    last: int
    closer: int
    tokens: tuple[bytes, ...]


def tree_sitter_call(source, name):
    root = cpp_layout.Parser(cpp_layout.CPP).parse(source).root_node
    pending = [root]
    matches = []
    while pending:
        node = pending.pop()
        pending.extend(node.named_children)
        if node.type != "call_expression":
            continue
        if node.child_by_field_name("function").text != name:
            continue
        args = node.child_by_field_name("arguments")
        written = [arg for arg in args.named_children if not arg.is_extra]
        matches.append(Call(args.start_point.row + 1, written[0].start_point.row + 1,
            written[-1].end_point.row + 1, args.end_point.row + 1,
            tuple(text for _, text in token_nodes(node))))
    call, = matches
    return call


def clang_call(compiler, path, flags, source, name):
    command = [compiler, *flags, "-fsyntax-only", "-Xclang"]
    dump = subprocess.check_output([*command, "-ast-dump=json", path])
    pending = [json.loads(dump)]
    matches = []
    while pending:
        node = pending.pop()
        children = node.get("inner", [])
        pending.extend(children)
        if node.get("kind") != "CallExpr":
            continue
        extent = node["range"]
        begin = extent["begin"]["offset"]
        callee_end = children[0]["range"]["end"]
        head_end = callee_end["offset"] + callee_end["tokLen"]
        if source[begin:head_end] == name:
            matches.append((extent, head_end, children[1:]))
    (extent, head_end, arguments), = matches
    opening = source.index(b"(", head_end)
    first = arguments[0]["range"]["begin"]["offset"]
    last = arguments[-1]["range"]["end"]["offset"]
    end = extent["end"]["offset"]

    lexed = subprocess.run([*command, "-dump-tokens", path], check=True,
        capture_output=True).stderr
    token = re.compile(rb"^\w+\s+'(.*?)'\s+.*?Loc=<" + re.escape(os.fsencode(path))
        + rb":(\d+):(\d+)>")
    starts = cpp_layout.starts_of_lines(source)
    tokens = []
    for line in lexed.splitlines():
        if match := token.search(line):
            row, col = map(int, match.groups()[1:])
            offset = starts[row - 1] + col - 1
            if extent["begin"]["offset"] <= offset <= end:
                tokens.append(match[1])

    def row(offset):
        return source.count(b"\n", 0, offset) + 1

    return Call(row(opening), row(first), row(last), row(end), tuple(tokens))


def probe_case(root, case, catalog, compiler, limit, matchers, plugin):
    name = case["bend_call"].encode("ascii")
    for relative in case.get("headers", []):
        header = fixture(catalog, relative)
        shutil.copyfile(header, root / header.name)
    flags = case.get("cxx", ["-std=c++20"])
    original = fixture(catalog, case["before"]).read_bytes()
    expected = fixture(catalog, case["after"]).read_bytes()
    engines = [engine for engine, diagnostics in case["expect"].items()
        if any(item["fix"] for item in diagnostics)]
    for engine in engines:
        _, candidate = run_engine(engine, case, catalog, matchers[engine], plugin, original)
        if candidate != expected:
            raise ValueError(f"{case['id']}: {engine} produced different fixed bytes")
    calls = []
    for version, source in (("before", original), ("after", candidate)):
        path = root / "case.cpp"
        path.write_bytes(source)
        syntactic = tree_sitter_call(source, name)
        semantic = clang_call(compiler, path, flags, source, name)
        if syntactic != semantic:
            raise ValueError(f"{case['id']} {version}: parser disagreement "
                f"{syntactic!r} != {semantic!r}")
        calls.append(syntactic)
    before, after = calls
    width = max(cpp_layout.line_width(line.decode("utf-8"))
        for line in candidate.splitlines()[after.first - 1:after.closer])
    symbols = dict(zip(dict.fromkeys(before.tokens), range(1, len(before.tokens) + 1)))

    def ids(call):
        return "[" + ", ".join(str(symbols[token]) for token in call.tokens) + "]"

    args = (f"{ids(before)}, {before.opening}n, {before.last - before.first}n, "
        f"{before.closer - before.last}n, {width}n, {limit}n")
    expected = f"{ids(after)}, {after.first}n, {after.last}n, {after.closer}n"
    law = case["id"].replace("-", "_")
    return (f"law {law}:\n"
        f"  {{Wrap.propose({args}) == Some{{Wrap.layout{{{expected}}}}}"
        f" : Maybe<Wrap.Layout>}}\n\ndef {law}():\n  {{==}}\n")


def main():
    catalog_path = HERE.parent / "catalog.json"
    catalog, matchers = load_catalog(catalog_path)
    plugin = default_plugin().resolve()
    if missing := clang_missing(plugin):
        sys.exit(f"The fixture probe needs {missing}.")
    check_plugin_layout(plugin, catalog["layout"], catalog_path)
    tidy = clang_style.tidy_binary()
    sibling = Path(tidy).with_name("clang++") if tidy else None
    compiler = sibling if sibling and sibling.is_file() else shutil.which("clang++")
    bend = shutil.which("bend")
    if not compiler or not bend:
        sys.exit("The fixture probe requires clang++ and Bend 2.")
    with TemporaryDirectory(prefix="tdesktop-bend-fixtures-") as folder:
        root = Path(folder).resolve()
        shutil.copyfile(HERE / "wrap.bend", root / "wrap.bend")
        laws = []
        for case in catalog["cases"]:
            if "bend_call" in case:
                laws.append(probe_case(root, case, catalog_path, compiler,
                    catalog["layout"]["columns"], matchers, plugin))
                print(f"MATCH Tree-sitter and Clang: {case['id']}", flush=True)
        if not laws:
            sys.exit("No Bend fixture cases were selected.")
        path = root / "fixture_proofs.bend"
        path.write_text("import Base\nimport wrap.bend as Wrap\n\n"
            + "\n".join(laws), encoding="utf-8")
        result = subprocess.run([bend, path, *sys.argv[1:]], check=True,
            text=True, stdout=subprocess.PIPE)
        print(result.stdout, end="")
        return "ALL PROOFS CHECK" not in result.stdout.splitlines()


if __name__ == "__main__":
    sys.exit(main())
