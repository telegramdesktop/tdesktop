import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import venv


HERE = Path(__file__).resolve().parent
MARKER = "# Installed by Telegram Desktop tools/rules/install.py"
REQUIREMENTS = HERE / "requirements.txt"
CHECK_DEPENDENCIES = """
from importlib.metadata import version
import sys
from pathlib import Path

import tree_sitter
import tree_sitter_cpp

for requirement in Path(sys.argv[1]).read_text().splitlines():
    name, expected = requirement.split("==")
    if version(name) != expected:
        sys.exit(1)
"""


def git_path(*arguments):
    return Path(os.fsdecode(subprocess.check_output(
        ["git", "rev-parse", *arguments]).rstrip(b"\r\n"))).resolve()


def has_dependencies(python):
    return subprocess.run([python, "-c", CHECK_DEPENDENCIES, REQUIREMENTS],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def hook_python(hooks):
    python = Path(sys.executable)
    if has_dependencies(python):
        return python

    environment = hooks / "tdesktop-venv"
    python = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.is_file():
        venv.EnvBuilder(with_pip=True).create(environment)
    if not has_dependencies(python):
        subprocess.run([python, "-m", "pip", "install", "--disable-pip-version-check",
            "-r", REQUIREMENTS], check=True)
    if not has_dependencies(python):
        raise ValueError("could not load the C++ parser in the hook environment")
    return python


def write_file(path, content, mode):
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        staging = Path(stream.name)
        try:
            stream.write(content)
            stream.close()
            staging.chmod(mode)
            staging.replace(path)
        finally:
            staging.unlink(missing_ok=True)


def install_hook(hooks, name, command, legacy=None):
    hook = hooks / name
    previous = hooks / (name + ".tdesktop-previous")
    if hook.is_file():
        original = hook.read_bytes()
        if MARKER.encode() not in original and original != legacy:
            if previous.exists() and previous.read_bytes() != original:
                raise ValueError(f"existing hook backup conflicts with {hook}")
            if not previous.exists():
                shutil.copy2(hook, previous, follow_symlinks=False)

    script = f"""#!/bin/sh
{MARKER}
here=$(dirname "$0")
{command}
if [ -x "$here/{previous.name}" ]; then
    exec "$here/{previous.name}" "$@"
fi
"""
    write_file(hook, script.encode(), 0o755)


def install():
    root = git_path("--show-toplevel")
    common = git_path("--path-format=absolute", "--git-common-dir")
    hooks = git_path("--path-format=absolute", "--git-path", "hooks")
    if not hooks.is_relative_to(common):
        raise ValueError(f"core.hooksPath points outside this clone's Git directory: {hooks}")
    hooks.mkdir(parents=True, exist_ok=True)
    python = shlex.quote(hook_python(hooks).as_posix())
    write_file(hooks / "tdesktop-commit-msg.py",
        (HERE / "commit_msg.py").read_bytes(), 0o644)
    install_hook(hooks, "commit-msg",
        f'{python} "$here/tdesktop-commit-msg.py" "$1" || exit $?')
    legacy = f"#!/bin/sh\nexec python3 {root}/tools/rules/hook.py --staged\n"
    install_hook(hooks, "pre-commit", f"""root=$(git rev-parse --show-toplevel) || exit $?
if [ -f "$root/tools/rules/hook.py" ]; then
    {python} "$root/tools/rules/hook.py" --staged || exit $?
fi""", legacy.encode())
    print(f"Installed Telegram Desktop Git hooks in {hooks}")


if __name__ == "__main__":
    try:
        install()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"Could not install Git hooks: {error}")
