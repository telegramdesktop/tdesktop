import subprocess
import sys
from pathlib import Path


PREFIX = "[git-hook] "
HOOK_PATHS = (":(top)tools/rules/", ":(top)tools/clangtidy/compile_db.py")


def validate(message):
    result = subprocess.run(["git", "diff", "--cached", "--quiet",
        "--no-ext-diff", "--no-textconv", "--no-renames", "--", *HOOK_PATHS])
    if result.returncode != 1:
        return result.returncode
    with message.open(encoding="utf-8") as stream:
        if stream.readline().startswith(PREFIX):
            return 0
    print("commit-msg: subject must start with [git-hook] followed by a "
        "space when the commit changes Git hook files.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(validate(Path(sys.argv[1])))
