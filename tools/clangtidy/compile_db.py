#!/usr/bin/env python3
"""Write the database clang-tidy reads: compile_db.py [build dir, "out"]."""
import collections
import json
import os
import shlex
import shutil
import subprocess
import sys

GCC_ONLY = frozenset(["-fhardened"])
MSVC_NOISE = frozenset(["MP", "WX", "FS", "showIncludes"])
MSVC_FILES = ("Yu", "Yc", "Fp", "Fo", "Fd")


def tools_directory():
    """Where clang-tidy sits, since its clang++ has to stand in for ours."""
    tidy = (os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
        or shutil.which("clang-tidy", path="/opt/homebrew/opt/llvm/bin"))
    if not tidy:
        sys.exit("error: clang-tidy not found; set CLANG_TIDY")
    return os.path.dirname(tidy)


def compiler_for(original, directory):
    """clang-cl stands in for MSVC, clang++ for every other compiler."""
    msvc = os.path.basename(original).lower() in ("cl", "cl.exe")
    name = "clang-cl" if msvc else "clang++"
    name += ".exe" if os.name == "nt" else ""
    beside = os.path.join(directory, name)
    return beside if os.path.exists(beside) else name


def split_command(command):
    """Windows quotes by CommandLineToArgvW rules, which shlex cannot read."""
    if os.name != "nt":
        return shlex.split(command)
    if not command.strip():
        return []  # an empty line answers with the path of this very process
    import ctypes
    shell, kernel = ctypes.windll.shell32, ctypes.windll.kernel32
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    parts = shell.CommandLineToArgvW(command, ctypes.byref(count))
    try:
        return [parts[index] for index in range(count.value)]
    finally:
        kernel.LocalFree(parts)


def dropped(argument):
    """An option clang rejects or cannot use; /FI stays, it force-includes."""
    if argument in GCC_ONLY:
        return True
    if argument[:1] not in ("/", "-"):
        return False
    flag = argument[1:]
    return flag.startswith(MSVC_FILES) or flag in MSVC_NOISE


def platform_arguments(driver):
    """What clang-tidy's clang needs and the build's own compiler did not."""
    if sys.platform == "darwin":
        sdk = subprocess.run(["xcrun", "--show-sdk-path"],
            capture_output=True, text=True).stdout.strip()
        if not sdk:
            return []
        # Its own libc++ is newer than ours and rejects some of our files.
        return ["-isysroot", sdk, "-nostdinc++",
            "-isystem" + os.path.join(sdk, "usr/include/c++/v1")]
    if os.name == "nt":
        # MSVC backs an enum of no fixed type by int, so mtpc_ cases narrow.
        return ["-Wno-c++11-narrowing"]
    if not os.path.basename(driver).startswith(("gcc", "g++")):
        return []
    found = shutil.which(driver) or driver
    prefix = os.path.dirname(os.path.dirname(found))
    if not os.path.isdir(prefix):
        print(f"warning: {driver} not on PATH, so the standard library it "
            f"carries stays hidden from clang", file=sys.stderr)
        return []
    return ["--gcc-toolchain=" + prefix]


def rewrite(command):
    """The command without what clang-tidy's clang cannot take."""
    arguments, kept, index = split_command(command), [], 0
    while index < len(arguments):
        argument = arguments[index]
        # Only clang's own form: GCC's -include of the header has to stay.
        if (argument == "-Xclang"
            and arguments[index + 1:index + 2] == ["-include-pch"]):
            index += 4
            continue
        if index and dropped(argument):
            index += 1
            continue
        kept.append(argument)
        index += 1
    if kept and os.path.basename(kept[0]) in ("ccache", "sccache"):
        kept.pop(0)
    return kept


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "out"
    if not os.path.exists(os.path.join(build, "build.ninja")):
        sys.exit(f"error: no ninja build in {build}")
    raw = subprocess.run(["ninja", "-C", build, "-t", "compdb"], check=True,
        capture_output=True, text=True).stdout
    commands = []
    for entry in json.loads(raw):
        if not entry["file"].endswith((".cpp", ".mm", ".cxx")):
            continue
        if not entry["command"].strip() or not os.path.exists(entry["file"]):
            continue
        kept = rewrite(entry["command"])
        if kept:
            commands.append((entry, kept))
    directory = tools_directory()
    drivers = collections.Counter(kept[0] for _, kept in commands)
    extra = platform_arguments(
        drivers.most_common(1)[0][0] if drivers else "")
    entries = [{
        "directory": entry["directory"],
        "file": entry["file"],
        "arguments": [compiler_for(kept[0], directory)] + extra + kept[1:],
    } for entry, kept in commands]
    target = os.path.join(build, "clang-tidy")
    os.makedirs(target, exist_ok=True)
    with open(os.path.join(target, "compile_commands.json"), "w") as stream:
        json.dump(entries, stream, indent=1)
    print(f"{len(entries)} entries -> {target}/compile_commands.json")


if __name__ == "__main__":
    main()
