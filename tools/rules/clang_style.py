import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


CHECKS = "tdesktop-*"


def git(*args):
    return subprocess.check_output(["git", *args])


def tidy_binary():
    if configured := os.environ.get("CLANG_TIDY"):
        return shutil.which(configured)
    return (shutil.which("clang-tidy")
        or shutil.which("clang-tidy", path="/opt/homebrew/opt/llvm/bin"))


def check_staged(sources):
    if not sources:
        return 0
    root = Path(os.fsdecode(git("rev-parse", "--show-toplevel").rstrip(b"\n")))
    configured = os.environ.get("STYLECHECK_PLUGIN")
    plugin = Path(configured) if configured else (
        root.parent / "stylecheck/build/libtdesktopstylecheck.so")
    if not configured and not plugin.is_file():
        return 0
    try:
        return run(root, plugin.resolve(), sources)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"clang-tidy style check could not run: {error}", file=sys.stderr)
        return 1


def run(root, plugin, sources, *, database=None, export_fixes=None):
    if not plugin.is_file():
        raise ValueError(f"stylecheck plugin not found at {plugin}")
    tidy = tidy_binary()
    if not tidy:
        raise ValueError("clang-tidy not found; set CLANG_TIDY")
    if database is None:
        database = Path(os.environ.get("STYLECHECK_DB", root / "out/clang-tidy"))
    commands = database / "compile_commands.json"
    if not commands.is_file():
        raise ValueError(f"{commands} is missing; regenerate the database with "
            "python3 tools/clangtidy/compile_db.py")
    known_sources = {}
    for entry in json.loads(commands.read_bytes()):
        name = Path(os.path.abspath(Path(entry["directory"]) / entry["file"]))
        known_sources.setdefault(name.resolve(), name)
    manifest = database / "header_sources.json"
    headers = {}
    if any(path.endswith(".h") for path in sources) and manifest.is_file():
        headers = json.loads(manifest.read_bytes())
    units = set()
    line_filter = []
    roots = {root}
    for path, (lines, _) in sources.items():
        header = path.endswith(".h")
        unit = headers.get(path, path[:-2] + ".cpp") if header else path
        name = known_sources.get((root / unit).resolve())
        if name is None:
            if header:
                continue
            raise ValueError(f"{path} is missing from {commands}; "
                "reconfigure the build and regenerate the database")
        units.add(name)
        parts = Path(unit).parts
        if name.parts[-len(parts):] == parts:
            roots.add(name.parents[len(parts) - 1])
        if not header:
            line_filter.append({"name": str(name),
                "lines": [[number, number] for number in sorted(lines)]})
    if not units:
        return 0
    header_filter = [{"name": path,
        "lines": [[number, number] for number in sorted(lines)]}
        for path, (lines, _) in sources.items() if path.endswith(".h")]

    with tempfile.TemporaryDirectory(prefix="tdesktop-stylecheck-") as folder:
        overlay = write_overlay(sorted(roots), Path(folder), sources)
        command = [tidy, f"--load={plugin}", f"--checks=-*,{CHECKS}",
            "--config={}", "--quiet",
            f"-p={database.resolve()}", f"--vfsoverlay={overlay}",
            "--line-filter=" + json.dumps(line_filter + header_filter)]
        if export_fixes is None:
            command.append(f"--warnings-as-errors={CHECKS}")
        else:
            command.append(f"--export-fixes={export_fixes}")
        if header_filter:
            command.append("--header-filter=.*")
        command.extend(map(str, sorted(units)))
        env = dict(os.environ)
        env.setdefault("STYLECHECK_PYTHON", sys.executable)
        if export_fixes is not None:
            result = subprocess.run(command, capture_output=True, text=True, env=env)
            if result.returncode:
                raise ValueError(result.stdout + result.stderr)
            return 0
        return 1 if subprocess.run(command, stdout=sys.stderr, env=env).returncode else 0


def write_overlay(roots, folder, sources):
    files = {path: blob if blob is not None else git("show", ":" + path)
        for path, (_, blob) in sources.items()}
    changed = git("diff", "--name-only", "-z", "--no-renames",
        "--ignore-submodules=all", "--diff-filter=MD")
    for raw in changed.split(b"\0"):
        if raw:
            path = os.fsdecode(raw)
            if path not in files:
                files[path] = git("show", ":" + path)

    entries = []
    for index, (path, content) in enumerate(files.items()):
        staged = folder / str(index)
        staged.write_bytes(content)
        entries.extend({"type": "file", "name": str(root / path),
            "external-contents": str(staged)} for root in roots)
    overlay = folder / "overlay.json"
    overlay.write_text(json.dumps({"version": 0, "use-external-names": False,
        "roots": entries}), encoding="utf-8")
    return overlay
