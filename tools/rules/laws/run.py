#!/usr/bin/env python3
import argparse
from collections import Counter
from contextlib import chdir
from difflib import SequenceMatcher, unified_diff
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from jsonschema import Draft202012Validator, ValidationError
import yaml

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parent.parent
SOURCE = "Telegram/SourceFiles/law_fixture.cpp"
ENGINES = ("python", "clang")
sys.path.insert(0, str(TOOLS))

import clang_style
import cpp_layout
import hook
from laws import closing_parenthesis


def read_json(path):
    return json.loads(path.read_bytes())


def fixture(catalog, name):
    path = (catalog.parent / name).resolve()
    if not path.is_relative_to(catalog.parent.resolve()) or not path.is_file():
        raise ValueError(f"invalid fixture path: {name}")
    return path


def load_catalog(path):
    catalog = read_json(path)
    schema = read_json(HERE / "schema.json")
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(catalog)
    rules = {}
    matchers = {engine: {} for engine in ENGINES}
    for rule in catalog["rules"]:
        name = rule["id"]
        if name in rules:
            raise ValueError(f"duplicate rule ID: {name}")
        rules[name] = rule
        for engine, implementation in rule["implementations"].items():
            match = implementation["match"]
            if engine == "python":
                re.compile(match)
            if match in matchers[engine]:
                raise ValueError(f"{engine}: duplicate diagnostic mapping {match}")
            matchers[engine][match] = name
    cases = set()
    for case in catalog["cases"]:
        name = case["id"]
        if name in cases:
            raise ValueError(f"duplicate case ID: {name}")
        cases.add(name)
        files = [case[key] for key in ("before", "after", "parent") if key in case]
        for file in files + case.get("headers", []):
            fixture(path, file)
        edits = False
        for engine, expected in case["expect"].items():
            for item in expected:
                rule = rules.get(item["rule"])
                if not rule or engine not in rule["implementations"]:
                    raise ValueError(f"{name}: unknown {engine} rule {item['rule']}")
                mode = case.get("mode", "staged")
                if mode not in rule["implementations"][engine].get("modes", ["staged"]):
                    raise ValueError(f"{name}: {item['rule']} is unavailable in {mode} mode")
                if "line" in item and mode != "staged":
                    raise ValueError(f"{name}: only staged diagnostics carry line numbers")
                if "line" not in item and engine == "clang":
                    raise ValueError(f"{name}: clang diagnostics require a line number")
                edits |= item["fix"]
        if edits != ("after" in case):
            raise ValueError(f"{name}: after.cpp and expected fixes must occur together")
        if "bend_call" in case and (not edits or set(case["expect"]) != set(ENGINES)):
            raise ValueError(f"{name}: Bend fixtures need an edit and both engines")
        if edits and fixture(path, case["before"]).read_bytes() == (
                fixture(path, case["after"]).read_bytes()):
            raise ValueError(f"{name}: expected edits must change bytes")
    covered = {(engine, diag["rule"], case.get("mode", "staged"))
        for case in catalog["cases"]
        for engine, diagnostics in case["expect"].items() for diag in diagnostics}
    missing = {(engine, name, mode) for name, rule in rules.items()
        for engine, implementation in rule["implementations"].items()
        for mode in implementation.get("modes", ["staged"])} - covered
    if missing:
        raise ValueError(f"rules without a diagnostic fixture: {sorted(missing)}")
    return catalog, matchers


def git(*args):
    subprocess.run(["git", *args], check=True, stdout=subprocess.DEVNULL)


def prepare(root, case, catalog, before, parent):
    source = root / SOURCE
    with chdir(root):
        git("init", "-q", "--template=")
        git("config", "core.autocrlf", "false")
        git("config", "core.hooksPath", os.devnull)
        source.parent.mkdir(parents=True)
        for file in case.get("headers", []):
            header = fixture(catalog, file)
            shutil.copyfile(header, source.parent / header.name)
        if parent is not None:
            source.write_bytes(parent)
            git("add", "--", "Telegram")
            git("-c", "user.name=Style laws", "-c", "user.email=laws@example.invalid",
                "-c", "commit.gpgsign=false", "commit", "-qm", "Fixture parent")
        source.write_bytes(before)
        git("add", "--", "Telegram")
        added, _, deleted = hook.staged_changes()
        lines = [line for line, _ in added.get(SOURCE, ())]
        if parent is not None and lines != case["changed"]:
            raise ValueError(f"staged lines: expected {case['changed']}, got {lines}")
        return set(lines).union(deleted.get(SOURCE, ()))


def mapped(matchers, message):
    matches = [rule for pattern, rule in matchers.items() if re.fullmatch(pattern, message)]
    if len(matches) != 1:
        raise ValueError(f"expected one rule mapping for {message!r}, got {matches}")
    return matches[0]


def run_hook(root, env):
    return subprocess.run(
        [sys.executable, TOOLS / "hook.py", "--staged"], cwd=root, env=env,
        capture_output=True, text=True)


def python_result(root, mappings, source, catalog):
    env = os.environ.copy()
    env.pop("STYLECHECK_PLUGIN", None)
    env["STYLECHECK_CATALOG"] = str(catalog)
    result = run_hook(root, env)
    output = result.stderr.splitlines()
    if result.returncode != bool(output):
        raise ValueError(f"Python hook exited {result.returncode}: {result.stderr}")
    path = root / SOURCE
    subprocess.run([sys.executable, TOOLS / "cpp_layout.py", "--fix", path],
        env=env, check=True)
    fixed = path.read_bytes()
    opcodes = SequenceMatcher(None, source.splitlines(keepends=True),
        fixed.splitlines(keepends=True), autojunk=False).get_opcodes()
    edited = {line for op, first, end, _, _ in opcodes if op != "equal"
        for line in range(first + 1, end + 1)}
    edited.update(first + 1 for op, first, _, _, _ in opcodes if op == "insert")
    diagnostics = []
    for report in output:
        if report.startswith(SOURCE + ":"):
            _, number, message = report.split(":", 2)
            line = int(number)
        else:
            line, message = None, report
        diagnostics.append((mapped(mappings, message.strip()), line, line in edited))
    return diagnostics, fixed


def python_nonstaged(root, mode, mappings, source):
    if mode == "edit":
        path = root / SOURCE
        payload = json.dumps({"tool_input": {"file_path": str(path),
            "new_string": source.decode("utf-8")}})
        result = subprocess.run([sys.executable, TOOLS / "hook.py"], input=payload,
            cwd=root, capture_output=True, text=True)
        lines = result.stderr.splitlines()
        if lines and lines[-1].startswith("The rules live in .clang-tidy and AGENTS.md."):
            lines.pop()
        failure = 2
    else:
        message = root / "message.txt"
        message.write_bytes(source)
        with chdir(root):
            git("init", "-q", "--template=")
            staged = root / "tools/rules/probe.txt"
            staged.parent.mkdir(parents=True)
            staged.write_text("Changed hook.\n", encoding="utf-8")
            git("add", "--", staged)
        result = subprocess.run([sys.executable, TOOLS / "commit_msg.py", message],
            cwd=root, capture_output=True, text=True)
        lines = result.stderr.splitlines()
        failure = 1
    if result.returncode != (failure if lines else 0):
        raise ValueError(f"{mode} hook exited {result.returncode}: {result.stderr}")
    return [(mapped(mappings, line), None, False) for line in lines], source


def clang_result(root, mappings, source, case, catalog, plugin, lines, staged):
    path = root / SOURCE
    db = root / "out/clang-tidy"
    db.mkdir(parents=True)
    (db / "compile_commands.json").write_text(json.dumps([{
        "directory": str(root),
        "file": str(path),
        "arguments": ["clang++", *case.get("cxx", ["-std=c++20"]),
            "-c", str(path)],
    }]), encoding="utf-8")
    export = root / "fixes.yaml"
    with chdir(root):
        clang_style.run(root, plugin, {SOURCE: (lines, source)},
            database=db, export_fixes=export)
    report = yaml.safe_load(export.read_bytes()) if export.exists() else {}
    diagnostics, replacements = [], set()
    for item in (report or {}).get("Diagnostics", ()):
        name = item["DiagnosticName"]
        message = item["DiagnosticMessage"]
        if Path(message["FilePath"]).resolve() != path:
            raise ValueError(f"unexpected clang-tidy location: {message}")
        edits = [edit for part in [message, *item.get("Notes", ())]
            for edit in part.get("Replacements", ())]
        line = source.count(b"\n", 0, message["FileOffset"]) + 1
        diagnostics.append((mappings.get(name, name), line, bool(edits)))
        for edit in edits:
            if Path(edit["FilePath"]).resolve() != path:
                raise ValueError(f"edit outside fixture: {edit}")
            first = edit["Offset"]
            replacements.add((first, first + edit["Length"],
                edit["ReplacementText"].encode("utf-8")))
    if staged and not case["expect"].get("python"):
        env = dict(os.environ, STYLECHECK_PLUGIN=str(plugin), STYLECHECK_DB=str(db),
            STYLECHECK_CATALOG=str(catalog))
        result = run_hook(root, env)
        pattern = (re.escape(str(path))
            + r":(\d+):\d+: error: .*? \[(tdesktop-[\w-]+)(?:,[^\]]*)?\]")
        errors = Counter((mappings.get(name, name), int(line))
            for line, name in re.findall(pattern, result.stderr))
        expected = Counter((rule, line) for rule, line, _ in diagnostics)
        if result.returncode != bool(expected) or errors != expected:
            raise ValueError(f"staged hook: expected {dict(expected)}, got:\n{result.stderr}")
    last = 0
    edits = sorted(replacements)
    for start, end, _ in edits:
        if not last <= start <= end <= len(source):
            raise ValueError(f"invalid or overlapping clang-tidy edit: {start}:{end}")
        last = end
    return diagnostics, cpp_layout.apply_layout_edits(source, edits)


def tokens(source):
    root = cpp_layout.Parser(cpp_layout.CPP).parse(source).root_node
    return token_nodes(root)


def token_nodes(root):
    pending = [root]
    while pending:
        node = pending.pop()
        if not node.child_count or node.type in (
                "string_literal", "raw_string_literal", "char_literal", "comment"):
            if node.text.strip():
                yield node.type, node.text
        else:
            pending.extend(reversed(node.children))


def run_engine(engine, case, catalog, mappings, plugin, source, parent=None):
    with TemporaryDirectory(prefix="tdesktop-law-") as folder:
        root = Path(folder).resolve()
        if (mode := case.get("mode", "staged")) != "staged":
            return python_nonstaged(root, mode, mappings, source)
        lines = prepare(root, case, catalog, source, parent)
        if engine == "python":
            return python_result(root, mappings, source, catalog)
        if parent is not None:
            (root / SOURCE).write_bytes(parent)
        return clang_result(root, mappings, source, case, catalog,
            plugin, lines, staged=parent is not None)


def check_case(case, catalog, matchers, engines, plugin, source_rewrites,
        layout, closer_result):
    before = fixture(catalog, case["before"]).read_bytes()
    after = fixture(catalog, case["after"]).read_bytes() if "after" in case else before
    parent = fixture(catalog, case["parent"]).read_bytes() if "parent" in case else None
    if "spec" in case:
        ranges = [(line, line) for line in case["changed"]] if parent is not None else None
        findings = list(closing_parenthesis.select(
            closing_parenthesis.inspect(before, layout), ranges))
        closer_result.validate([closing_parenthesis.encode(finding) for finding in findings])
        if any(before[item.offset:item.offset + 1] != b")"
                or before.count(b"\n", 0, item.offset) + 1 != item.last_line
                or not item.first_line <= item.anchor_line <= item.last_line
                for item in findings):
            raise ValueError("spec: invalid location or line interval")
        diagnostics = Counter((case["spec"], finding.last_line, bool(finding.edits))
            for finding in findings)
        for expected in case["expect"].values():
            want = Counter((item["rule"], item.get("line"), item["fix"]) for item in expected)
            if diagnostics != want:
                raise ValueError(f"spec: expected {dict(want)}, got {dict(diagnostics)}")
        if closing_parenthesis.apply(before, findings) != after:
            raise ValueError("spec: fixed bytes differ")
        if after != before and list(closing_parenthesis.inspect(after, layout)):
            raise ValueError("spec: fixed source is not clean")

    def run(engine, source, parent=None):
        return run_engine(engine, case, catalog, matchers[engine], plugin, source, parent)

    for engine in engines:
        expected = case["expect"][engine]
        diagnostics, fixed = run(engine, before, parent)
        want = Counter((item["rule"], item.get("line"), item["fix"]) for item in expected)
        if Counter(diagnostics) != want:
            raise ValueError(f"{engine}: expected {list(want.elements())}, got {diagnostics}")
        expected_bytes = after if any(item["fix"] for item in expected) else before
        if fixed != expected_bytes:
            diff = "".join(unified_diff(
                expected_bytes.decode().splitlines(keepends=True),
                fixed.decode().splitlines(keepends=True), "expected", "actual"))
            raise ValueError(f"{engine}: fixed bytes differ\n{diff}")
        if (fixed != before and not any(item["fix"] and item["rule"] in source_rewrites
                for item in expected) and list(tokens(before)) != list(tokens(fixed))):
            raise ValueError("fix changes C++ tokens or comments")
    if after != before:
        for engine in engines:
            diagnostics, fixed = run(engine, after)
            if fixed != after or diagnostics:
                raise ValueError(f"{engine}: fixed source is not clean: {diagnostics}")


def clang_missing(plugin):
    missing = []
    if not clang_style.tidy_binary():
        missing.append("clang-tidy")
    if not plugin.is_file():
        missing.append(f"plugin ({plugin})")
    elif not plugin.with_name("LayoutConfig.h").is_file():
        missing.append(f"plugin layout header ({plugin.with_name('LayoutConfig.h')})")
    return ", ".join(missing)


def default_plugin():
    return Path(os.environ.get("STYLECHECK_PLUGIN",
        ROOT.parent / "stylecheck/build/libtdesktopstylecheck.so"))


def check_plugin_layout(plugin, layout, catalog_path):
    header = plugin.with_name("LayoutConfig.h")
    if not header.is_file():
        raise ValueError(f"missing {header}; rebuild the plugin with --catalog")
    generated = header.read_text(encoding="utf-8")
    values = {name: int(value) for name, value in re.findall(
        r"^inline constexpr auto (kColumnLimit|kTabWidth) = (\d+);$", generated, re.MULTILINE)}
    configured = re.search(r'^inline constexpr auto kStylecheckCatalog = R"spec\((.*)\)spec";$',
        generated, re.MULTILINE)
    if (values != {"kColumnLimit": layout["columns"], "kTabWidth": layout["tab_width"]}
            or not configured or Path(configured[1]).resolve() != catalog_path.resolve()):
        raise ValueError(f"{header} disagrees with the catalog; rebuild the plugin")


def check_python_inventory(matchers):
    messages = [message for name, message in vars(cpp_layout).items()
        if name.endswith("_MESSAGE") and name != "LOGICAL_MESSAGE"]
    messages.extend(cpp_layout.LOGICAL_MESSAGE.format(operator=operator)
        for operator in cpp_layout.LOGICAL_OPERATORS)
    for message in messages:
        mapped(matchers, message)


def check_plugin_inventory(plugin, matchers, full):
    result = subprocess.run([clang_style.tidy_binary(), f"--load={plugin}",
        "--config={}", "--checks=-*,tdesktop-*", "--list-checks"],
        capture_output=True, text=True)
    if result.returncode:
        raise ValueError(result.stderr or result.stdout)
    active = set(re.findall(r"^[ \t]+(tdesktop-[\w-]+)[ \t]*$", result.stdout, re.MULTILINE))
    declared = set(matchers)
    unmapped = active - declared if full else set()
    if unmapped or declared - active:
        raise ValueError(f"clang rules missing from catalog: {sorted(unmapped)}; "
            f"catalog rules missing from plugin: {sorted(declared - active)}")


def replay(revisions, plugin, catalog, quick):
    db = Path(os.environ.get("STYLECHECK_DB", ROOT / "out/clang-tidy")).resolve()
    missing = clang_missing(plugin)
    if not (db / "compile_commands.json").is_file():
        missing = ", ".join(filter(None, [missing, f"compile DB ({db})"]))
    if missing:
        print(f"SKIP replay: missing {missing}")
        return not quick
    check_plugin_layout(plugin, read_json(catalog)["layout"], catalog)

    def query(*args, env=None):
        return subprocess.check_output(["git", *args], cwd=ROOT, env=env,
            text=True).strip()

    commits = query("rev-list", "--reverse", "--first-parent", revisions).splitlines()
    if not commits:
        raise ValueError(f"empty replay range: {revisions}")
    objects = query("rev-parse", "--path-format=absolute", "--git-path", "objects")
    hooks = query("rev-parse", "--path-format=absolute", "--git-path", "hooks")
    pre_commit = Path(hooks) / "pre-commit"
    if not os.access(pre_commit, os.X_OK):
        raise ValueError(f"pre-commit hook is unavailable at {pre_commit}")
    failures = 0
    with TemporaryDirectory(prefix="tdesktop-laws-replay-") as folder:
        query("init", "-q", "--bare", "--template=", folder)
        env = dict(os.environ, GIT_DIR=folder, GIT_WORK_TREE=str(ROOT),
            GIT_INDEX_FILE=str(Path(folder) / "index"),
            GIT_ALTERNATE_OBJECT_DIRECTORIES=objects,
            STYLECHECK_PLUGIN=str(plugin), STYLECHECK_DB=str(db),
            STYLECHECK_CATALOG=str(catalog))
        for commit in commits:
            parent = query("rev-parse", commit + "^")
            query("update-ref", "HEAD", parent, env=env)
            query("read-tree", commit, env=env)
            result = subprocess.run([pre_commit], cwd=ROOT, env=env,
                capture_output=True, text=True)
            failures += bool(result.returncode)
            subject = query("show", "-s", "--format=%s", commit)
            print(f"{commit[:10]} exit={result.returncode} {subject}", flush=True)
            if result.stdout or result.stderr:
                print(result.stdout + result.stderr, end="", flush=True)
    print(f"{revisions}: {len(commits) - failures}/{len(commits)} passed")
    return bool(failures)


def main():
    parser = argparse.ArgumentParser(description="Run the Git hook and C++ style contracts.")
    parser.add_argument("--catalog", type=Path, default=HERE / "catalog.json")
    parser.add_argument("--plugin", type=Path, default=default_plugin())
    parser.add_argument("--engine", choices=ENGINES, action="append")
    parser.add_argument("--quick", action="store_true", help="allow unavailable engines")
    parser.add_argument("--replay", metavar="BASE..TIP",
        help="run the installed pre-commit hook against each commit and its first parent")
    parser.add_argument("cases", nargs="*", help="case IDs (default: all)")
    args = parser.parse_args()
    plugin = args.plugin.resolve()
    if args.replay:
        if args.engine or args.cases:
            parser.error("--replay cannot be combined with engine or case selection")
        try:
            return replay(args.replay, plugin, args.catalog.resolve(), args.quick)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            parser.error(str(error))
    try:
        catalog, matchers = load_catalog(args.catalog)
    except (OSError, ValueError, ValidationError) as error:
        parser.error(str(error))
    selected = set(args.engine or ENGINES)
    default_catalog = args.catalog.resolve() == HERE / "catalog.json"
    if default_catalog and "python" in selected:
        try:
            check_python_inventory(matchers["python"])
        except ValueError as error:
            parser.error(str(error))
    known = {case["id"] for case in catalog["cases"]}
    if unknown := set(args.cases) - known:
        parser.error(f"unknown cases: {', '.join(sorted(unknown))}")
    needs_clang = "clang" in selected and any("clang" in case["expect"]
        and (not args.cases or case["id"] in args.cases) for case in catalog["cases"])
    missing = clang_missing(plugin) if needs_clang else ""
    if needs_clang and not missing:
        try:
            check_plugin_layout(plugin, catalog["layout"], args.catalog)
            check_plugin_inventory(plugin, matchers["clang"], full=default_catalog)
        except (OSError, ValueError) as error:
            parser.error(str(error))
    passed, skipped = Counter(), Counter()
    closer_result = Draft202012Validator(read_json(HERE / "closing_parenthesis.schema.json"))
    source_rewrites = {rule["id"] for rule in catalog["rules"]
        if rule.get("fix_kind") == "source-rewrite"}
    failed = False
    for case in catalog["cases"]:
        name = case["id"]
        if args.cases and name not in args.cases:
            continue
        engines = [engine for engine in ENGINES if engine in case["expect"]
            and engine in selected]
        if missing and "clang" in engines:
            print(f"SKIP {name} (clang): missing {missing}", flush=True)
            skipped["clang"] += 1
            engines.remove("clang")
        if not engines:
            continue
        try:
            check_case(case, args.catalog.resolve(), matchers, engines, plugin,
                source_rewrites, catalog["layout"], closer_result)
        except (OSError, ValueError, ValidationError, subprocess.CalledProcessError) as error:
            print(f"FAIL {name}: {error}", flush=True)
            failed = True
        else:
            print(f"PASS {name} ({', '.join(engines)})", flush=True)
            passed.update(engines)
    for engine in sorted(selected):
        print(f"{engine}: {passed[engine]} passed, {skipped[engine]} skipped")
    return failed or (bool(skipped) and not args.quick) or not passed


if __name__ == "__main__":
    sys.exit(main())
