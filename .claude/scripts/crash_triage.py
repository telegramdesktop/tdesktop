#!/usr/bin/env python3
"""Mechanical pass over Breakpad stackwalk logs in ~/Telegram/Crashes/all.

Splits the corpus into (a) reports worth a human/agent look and (b) reports
that can be dropped without reading them, then clusters (a) by crashed-thread
signature so each distinct crash is investigated exactly once.

Subcommands
    ledger  show what past runs already decided
    record  write one group's verdict into the ledger
    rekey   move ledger entries to the keys today's signature() gives them
    claim   move the pending triples out of all/ into a timestamped run folder
    scan    classify, cluster, write index/groups into the run folder
    show    print one group's representative report and member ids
    sweep   move (default) or delete the dropped triples; needs a prior scan

`claim` is what keeps successive runs disjoint: it empties `all/` into
`backup/<stamp>-crashes/`, so the next run only ever sees reports that arrived
after this one. Incomplete triples are left behind for the next run, and an
interrupted move is resumed through its `.processing-<name>` marker.

Everything is stdlib-only and never touches the git working tree.
"""

import argparse
import copy
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from collections import Counter, OrderedDict

# --------------------------------------------------------------------------
# version resolution
# --------------------------------------------------------------------------

TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")


def git(repo, *args):
    try:
        out = subprocess.run(
            ["git", "-C", repo] + list(args),
            capture_output=True, text=True, check=True)
        return out.stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def numeric_version(major, minor, patch):
    return major * 1000000 + minor * 1000 + patch


def resolve_target(repo, wanted):
    """-> (label, set_of_accepted_numeric_versions).

    `wanted` may be None (use the newest published tag), "7.1.2", or "7001002".
    """
    label = None
    if wanted:
        wanted = wanted.strip().lstrip("v")
        if re.match(r"^\d+\.\d+\.\d+$", wanted):
            parts = [int(p) for p in wanted.split(".")]
            base = numeric_version(*parts)
            label = "v" + wanted
        elif wanted.isdigit():
            base = int(wanted)
            # An alpha number is the release number with three extra digits.
            if base > 100000000:
                base //= 1000
            label = str(base)
        else:
            sys.exit("unrecognised --version %r" % wanted)
        return label, {base, base * 1000}

    tags = []
    for line in git(repo, "tag", "--sort=-v:refname").splitlines():
        m = TAG_RE.match(line.strip())
        if m:
            tags.append((line.strip(), tuple(int(g) for g in m.groups())))
    if not tags:
        sys.exit("no v<x>.<y>.<z> tags in %s; pass --version" % repo)
    tag, parts = tags[0]
    # Prefer the tagged build/version file over recomputing the encoding.
    base = None
    blob = git(repo, "show", "%s:Telegram/build/version" % tag)
    for line in blob.splitlines():
        f = line.split()
        if len(f) == 2 and f[0] == "AppVersion" and f[1].isdigit():
            base = int(f[1])
    if base is None:
        base = numeric_version(*parts)
    return tag, {base, base * 1000}


# --------------------------------------------------------------------------
# log parsing
# --------------------------------------------------------------------------

FRAME_RE = re.compile(r"^ {0,3}(\d+) {1,2}(\S.*)$")
THREAD_RE = re.compile(r"^Thread (\d+)( \(crashed\))?\s*$")
FOUND_RE = re.compile(r"^\s+Found by: (.+)$")
FILELINE_RE = re.compile(r"\s\[([^\[\]]+?)\s:\s(\d+)(?:\s\+\s0x[0-9a-fA-F]+)?\]\s*$")
OFFSET_RE = re.compile(r"\s\+\s0x[0-9a-fA-F]+\s*$")

HEADER_KEYS = (
    "ApiId", "Assertion", "Binary", "Launched", "OpenGL", "OpenGL ANGLE",
    "OpenGL Renderer", "Platform", "UserTag", "Username", "Version",
    "OS-Version", "Memory-usage", "Pagefile-usage")


def _clip(text, width):
    if len(text) <= width:
        return text
    return text[:width - 3] + "..."


class Frame(object):
    __slots__ = ("index", "raw", "module", "symbol", "file", "line", "found_by")

    def __init__(self, index, raw):
        self.index = index
        self.raw = raw
        self.module = None
        self.symbol = None
        self.file = None
        self.line = None
        self.found_by = None
        if "!" in raw:
            self.module, rest = raw.split("!", 1)
            m = FILELINE_RE.search(rest)
            if m:
                self.file = os.path.basename(m.group(1).strip())
                self.line = int(m.group(2))
                rest = rest[:m.start()]
            self.symbol = OFFSET_RE.sub("", rest).strip()
        elif " + 0x" in raw:
            self.module = raw.split(" + 0x", 1)[0].strip()
        # else: a bare address, nothing resolved

    @property
    def reliable(self):
        return self.found_by != "stack scanning"

    def func(self):
        """Function name without argument list, for loose clustering."""
        if self.symbol:
            name = re.sub(r"\(.*$", "", self.symbol).strip()
            return name or self.symbol
        if self.module:
            return self.module + "+?"
        return "?"

    def precise(self):
        if self.file:
            return "%s:%d" % (self.file, self.line)
        return self.func()

    def pretty(self, width=150):
        if self.symbol and self.file:
            text = "%s [%s:%d]" % (_clip(self.symbol, width), self.file, self.line)
        elif self.symbol:
            text = _clip(self.symbol, width)
        elif self.module:
            text = self.module + " + <offset>"
        else:
            text = self.raw
        return text


def parse_log(path):
    rep = {
        "path": path,
        "id": re.sub(r"^log_|\.txt$", "", os.path.basename(path)),
        "header": {},
        "reason": "",
        "address": "",
        "uptime": "",
        "os": "",
        "cpu": "",
        "frames": [],
        "no_frames": False,
        "modules": [],
    }
    try:
        with open(path, "r", errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError:
        return None

    section = "header"
    in_crashed = False
    current = None
    for line in lines:
        if line.startswith("Loaded modules:"):
            section = "modules"
            continue
        if section == "modules":
            f = line.split()
            if len(f) >= 4:
                rep["modules"].append(f[3])
            continue

        m = THREAD_RE.match(line)
        if m:
            section = "threads"
            in_crashed = bool(m.group(2))
            current = None
            continue

        if section == "header":
            if line.startswith("Crash reason:"):
                rep["reason"] = line.split(":", 1)[1].strip()
                continue
            if line.startswith("Crash address:"):
                rep["address"] = line.split(":", 1)[1].strip()
                continue
            if line.startswith("Process uptime:"):
                rep["uptime"] = line.split(":", 1)[1].strip()
                continue
            if line.startswith("Operating system:"):
                rep["os"] = line.split(":", 1)[1].strip()
                continue
            if line.startswith("CPU:"):
                rep["cpu"] = line.split(":", 1)[1].strip()
                continue
            if ":" in line:
                key, val = line.split(":", 1)
                if key in HEADER_KEYS:
                    rep["header"][key] = val.strip()
            continue

        if not in_crashed:
            continue
        if "<no frames>" in line:
            rep["no_frames"] = True
            continue
        fm = FRAME_RE.match(line)
        if fm and " = 0x" not in line[:24]:
            current = Frame(int(fm.group(1)), fm.group(2).strip())
            rep["frames"].append(current)
            continue
        fb = FOUND_RE.match(line)
        if fb and current is not None:
            current.found_by = fb.group(1).strip()

    rep["version_raw"] = rep["header"].get("Version", "")
    vm = re.match(r"^(\d+)", rep["version_raw"])
    rep["version"] = int(vm.group(1)) if vm else 0
    rep["beta"] = "beta" in rep["version_raw"]
    return rep


# --------------------------------------------------------------------------
# signature + hints
# --------------------------------------------------------------------------

# Frames that only say "we crashed on purpose" or "we were allocating"; they
# are identical across unrelated bugs, so they must not drive clustering.
NOISE_FUNC = re.compile(
    r"(base::assertion::fail|CrashReports::|SignalHandlers|LogSkipDebug"
    r"|::Unexpected|_purecall|abort|raise|__cxa_|std::terminate"
    r"|_invalid_parameter|RtlRaiseException|KiUserExceptionDispatcher"
    r"|callnewh|_callnewh"
    r"|RaiseException|qt_assert|qFatal|QMessageLogger::fatal"
    r"|operator new|malloc|_callnewh|::allocate\b)", re.I)

# The same crash is spelled differently by MSVC and by GCC/Clang: the lambda
# wrappers, the scope decorations and the std:: forwarding shim all differ. The
# canonical key below throws that away so a Windows dump and a Linux dump of
# one bug land in one group.
QUALIFIED = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_~][A-Za-z0-9_]*)+")
MSVC_LAMBDA = re.compile(r"<lambda_\d+_?>")
GCC_LAMBDA = re.compile(r"\{lambda[^}]*\}")
MSVC_SCOPE = re.compile(r"'::`\d+'::|[`\']")
# Namespaces that only ever forward: the interesting name is the callee they
# carry in their template arguments, so mine through them.
NOT_APP = ("std::", "__gnu", "__cxx", "_Func", "_Function", "_Invoker",
           "__pstl", "gnu_cxx", "rpl::", "crl::", "gsl::")
# A plain C function: the name right before its argument list, or the whole
# symbol when the dump printed no argument list at all.
C_FUNCTION = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(|^([A-Za-z_][A-Za-z0-9_]*)$")
# The C runtime and Qt's failure reporting linked into the app: a throw, an
# allocation, a jump, a copy, a fatal message. They say how the app failed,
# not where, so they stay glue.
APP_GLUE = re.compile(
    r"^(_|errno$|operator$|new$|delete$|CxxThrowException$|(set|long)jmp$"
    r"|internal_|mem(cpy|move|set|cmp)$|str(len|cmp|ncmp|cpy|ncpy|chr|str)$"
    r"|(m|c|re)alloc$|free$|qBadAlloc$|qt_message|qt_assert|qt_check_pointer"
    r"|qFatal$|qTerminate$)")


def app_modules(binary):
    """Module names the app's own code (FFmpeg, dav1d etc. included) runs in."""
    names = {"Telegram", "Telegram.exe"}
    if binary:
        names.add(binary)
        names.add(re.sub(r"\.app$", "", binary))
    return names


def canonical(frame, app=()):
    """A toolchain-independent name for one frame, or None for pure glue.

    std:: forwarding frames carry the real callee inside their template
    arguments, so they are mined rather than dropped; a frame that is still
    nothing but standard-library plumbing after mining returns None.

    C functions linked into the app binary (FFmpeg, dav1d, ...) are kept by
    name. Dropping them as glue left codec crashes keyed on nothing but
    `kernel32.dll+?`, which merged unrelated crashes in the ledger. C names
    from system libraries stay glue: on macOS every abort is `__pthread_kill`.
    """
    text = frame.symbol or frame.raw
    text = MSVC_LAMBDA.sub("<lambda>", text)
    text = GCC_LAMBDA.sub("<lambda>", text)
    text = MSVC_SCOPE.sub("", text)
    qualified = False
    for m in QUALIFIED.finditer(text):
        qualified = True
        name = m.group(0)
        if not name.startswith(NOT_APP):
            return name
    if frame.symbol and not qualified and frame.module in app:
        m = C_FUNCTION.search(frame.symbol.replace("static ", ""))
        name = m and (m.group(1) or m.group(2))
        if name and not APP_GLUE.search(name):
            return name
    if frame.symbol:
        return None
    if frame.module:
        return frame.module + "+?"
    return "?"


def collapse(keys):
    """Drop consecutive repeats left behind by forwarding frames."""
    out = []
    for k in keys:
        if not out or out[-1] != k:
            out.append(k)
    return out


# An assertion that names its own source line is a better signature than any
# stack. These three fire from the reporter itself, so they are not.
GENERIC_ASSERTION = re.compile(
    r"crash_reports\.cpp:|Pure virtual function called|^Unexpected: Unknown")

# A checked expression inside a header - `ptr_ != nullptr` in gsl's pointers,
# a container's bounds check - fails for every caller of that helper, so it
# names the helper rather than the bug. An Unexpected("...") message names its
# own bug wherever it lives.
HEADER_EXPRESSION = re.compile(r'^".*"\s+(\S+):\d+\s*$')
SOURCE_FILE = re.compile(r"\.(cpp|cc|cxx|c|mm|m)$")


def generic_assertion(assertion):
    if GENERIC_ASSERTION.search(assertion):
        return True
    m = HEADER_EXPRESSION.search(assertion)
    return bool(m) and not SOURCE_FILE.search(m.group(1))

GPU_MODULES = re.compile(
    r"(atio|amdvlk|amdxc|nvoglv|nvwgf2|nvd3d|nvcuda|igd|igvk|ig9|ig11"
    r"|opengl32|d3d1[01]|dxgi|libGLX|libEGL|libGLESv2|swiftshader"
    r"|AGXMetal|AMDRadeon|AppleIntel|Metal|vulkan|mesa|iris_dri|radeonsi)",
    re.I)

INTERCEPTOR_MODULES = re.compile(
    r"(ti_x64\.dll|ti_x32\.dll|TelegramInterceptor|TelegramCommon\.dll)", re.I)

OFFICIAL_BINARY = re.compile(r"^Telegram( \d+)?(\.exe|\.app)?$")


def signature(rep):
    frames = [f for f in rep["frames"] if f.reliable]
    if not frames:
        frames = list(rep["frames"])
    # Drop the crash-machinery prologue, but never drop everything.
    trimmed = list(frames)
    while trimmed and NOISE_FUNC.search(trimmed[0].func()):
        trimmed.pop(0)
    if not trimmed:
        trimmed = frames

    assertion = rep["header"].get("Assertion", "")
    app = app_modules(rep["header"].get("Binary", ""))
    keys = collapse([k for k in (canonical(f, app) for f in trimmed) if k])
    if not keys:
        keys = collapse([f.func() for f in trimmed])
    loose = " | ".join(keys[:3])
    exact = " | ".join(f.precise() for f in trimmed[:6])

    if assertion and not generic_assertion(assertion):
        # A named assertion is the same bug on every platform and in every
        # caller, so the stack must not split it.
        key = assertion
    elif assertion:
        key = assertion + " || " + loose
    else:
        key = loose
    return key, loose, exact, trimmed


def hints(rep, trimmed):
    tags = []
    mods = " ".join(rep["modules"])
    assertion = rep["header"].get("Assertion", "")
    binary = rep["header"].get("Binary", "")
    platform = rep["header"].get("Platform", "")

    if INTERCEPTOR_MODULES.search(mods) or any(
            INTERCEPTOR_MODULES.search(f.raw) for f in rep["frames"]):
        tags.append("interceptor")
    if binary and not OFFICIAL_BINARY.match(binary):
        tags.append("clone-binary")
    top = " ".join(f.raw for f in trimmed[:4])
    if GPU_MODULES.search(top):
        tags.append("gpu-driver")
    if "Could not allocate" in assertion or "bad_alloc" in " ".join(
            f.raw for f in rep["frames"][:8]):
        tags.append("oom")
    if "Deadlock found" in assertion:
        tags.append("deadlock-detector")
    if "Qt FATAL" in assertion:
        tags.append("qt-fatal")
    if "Pure virtual" in assertion:
        tags.append("pure-virtual")
    if platform == "Windows32Bit":
        tags.append("win32")
    if assertion:
        tags.append("assertion")
    return tags


# --------------------------------------------------------------------------
# claim: rotate all/ into a timestamped run folder
# --------------------------------------------------------------------------

RUN_SUFFIX = "-crashes"


def unique_run_name(backups, base):
    candidate, n = base, 1
    while (os.path.exists(os.path.join(backups, candidate))
           or os.path.exists(os.path.join(backups, ".processing-" + candidate))):
        n += 1
        candidate = "%s-%d" % (base, n)
    return candidate


def complete_triples(crashes):
    """Ids whose log/err/dmp are all present, so we never move a half-written
    report out from under the fetcher."""
    try:
        names = set(os.listdir(crashes))
    except OSError:
        return []
    ids = set()
    for name in names:
        if name.startswith("log_") and name.endswith(".txt"):
            ids.add(name[4:-4])
    ready = []
    for cid in sorted(ids):
        trio = ("log_%s.txt" % cid, "err_%s.txt" % cid, "dmp_%s.dmp" % cid)
        if all(t in names for t in trio):
            ready.append((cid, trio))
    return ready


def latest_run(backups):
    if not os.path.isdir(backups):
        return None
    runs = sorted(n for n in os.listdir(backups)
                  if n.endswith(RUN_SUFFIX) or RUN_SUFFIX + "-" in n)
    return os.path.join(backups, runs[-1]) if runs else None


def claim(crashes, backups, new=False):
    """Rotate the ready triples out of `crashes` into a fresh run folder.

    Returns a dict with `run_dir`, `claimed`, `reports`, `reused`, `left`.
    `run_dir` is None only when there is nothing to work on at all.
    """
    crashes = os.path.expanduser(crashes)
    backups = os.path.expanduser(backups)
    os.makedirs(backups, exist_ok=True)

    # Resume an interrupted claim before starting a new one.
    pending = sorted(n for n in os.listdir(backups)
                     if n.startswith(".processing-"))
    if pending:
        name = pending[0][len(".processing-"):]
        work = os.path.join(backups, pending[0])
        resumed = True
    else:
        stamp = datetime.datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
        name = unique_run_name(backups, stamp + RUN_SUFFIX)
        work = os.path.join(backups, ".processing-" + name)
        os.makedirs(work, exist_ok=True)
        resumed = False

    moved = 0
    for cid, trio in complete_triples(crashes):
        for fname in trio:
            src = os.path.join(crashes, fname)
            if os.path.exists(src):
                shutil.move(src, os.path.join(work, fname))
        moved += 1

    held = len([n for n in os.listdir(work) if n.startswith("log_")])
    if held == 0 and not new:
        # Nothing arrived since the last run: continue triaging that one
        # instead of leaving an empty folder behind.
        os.rmdir(work)
        previous = latest_run(backups)
        if previous is None:
            return {"run_dir": None, "claimed": 0, "reports": 0,
                    "reused": False, "left": 0}
        return {"run_dir": previous, "claimed": 0, "reused": True, "left": 0,
                "reports": len([n for n in os.listdir(previous)
                                if n.startswith("log_")])}

    final = os.path.join(backups, name)
    os.rename(work, final)
    left = len([n for n in os.listdir(crashes)
                if n.startswith("log_") and n.endswith(".txt")])
    return {"run_dir": final, "claimed": moved, "reports": held,
            "reused": resumed, "left": left}


def report_claim(info, crashes):
    if info["run_dir"] is None:
        print("nothing to claim: %s is empty and there is no previous run"
              % os.path.expanduser(crashes))
        return 1
    if info["reused"] and not info["claimed"]:
        print("nothing new in %s; reusing the previous run" % crashes)
    print("claimed=%d" % info["claimed"])
    print("reports=%d" % info["reports"])
    print("reused=%s" % ("true" if info["reused"] else "false"))
    if info["left"]:
        print("left_behind=%d  (incomplete triples, kept for the next run)"
              % info["left"])
    print("run_dir=%s" % info["run_dir"])
    return 0


def cmd_claim(args):
    info = claim(args.crashes, args.backups, args.new)
    return report_claim(info, args.crashes)


# --------------------------------------------------------------------------
# ledger: what earlier runs already decided
# --------------------------------------------------------------------------

DEFAULT_LEDGER = "~/Telegram/Crashes/triage-ledger.json"

# Bump whenever signature() starts keying reports differently: the next scan
# then moves every ledger entry to the key its own reports get now.
SIGNATURE_VERSION = 2


def stable_key(raw_key):
    """A signature hash that survives a version bump.

    Line numbers move between releases, so they are stripped; what is left is
    the assertion text and/or the canonical top-of-stack names, both of which
    already read the same on every toolchain.
    """
    text = re.sub(r"\.(h|cpp|cc|c|mm|m|hpp):\d+", r".\1", raw_key or "")
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:12]


def load_ledger(path):
    path = os.path.expanduser(path)
    if not os.path.exists(path):
        return {}
    try:
        with open(path) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def git_ok(repo, *args):
    try:
        return subprocess.run(
            ["git", "-C", repo] + list(args),
            capture_output=True).returncode == 0
    except FileNotFoundError:
        return False


_RUN_VERSIONS = {}


def run_version(backups, run):
    """The release a past run triaged, from that run's own groups.json."""
    if run not in _RUN_VERSIONS:
        version = None
        path = os.path.join(os.path.expanduser(backups), run, "triage",
                            "groups.json")
        try:
            with open(path) as fh:
                accepted = json.load(fh).get("accepted_versions") or []
            version = min(accepted) if accepted else None
        except (OSError, ValueError):
            pass
        _RUN_VERSIONS[run] = version
    return _RUN_VERSIONS[run]


def decided_version(entry, backups):
    if entry.get("version"):
        return entry["version"]
    found = [run_version(backups, run) for run in entry.get("runs", [])]
    found = [v for v in found if v]
    return max(found) if found else None


def recurring(entry, version, repo, tag, backups):
    """Whether a fix in the ledger is contradicted by this release's reports.

    A crash that still arrives from a release newer than the one its fix was
    decided for was not fixed, whatever the ledger says. The only exception is
    a fix commit that provably has not shipped in the tag yet. An old entry
    whose release is unknown, or whose commit no longer exists (rebased), is
    reopened: a needless investigation is cheaper than a silently hidden crash.
    """
    if entry.get("verdict") != "fixed" and not entry.get("commit"):
        return False
    decided = decided_version(entry, backups)
    if decided is not None and decided >= version:
        return False
    commit = entry.get("commit")
    if (commit
            and git_ok(repo, "cat-file", "-e", commit + "^{commit}")
            and not git_ok(repo, "merge-base", "--is-ancestor", commit, tag)):
        return False
    return True


def entry_reports(entry, key, backups):
    """The reports an entry was decided on: its representatives."""
    if entry.get("reports"):
        return entry["reports"]
    found = []
    for run in entry.get("runs", []):
        path = os.path.join(os.path.expanduser(backups), run, "triage",
                            "groups.json")
        try:
            with open(path) as fh:
                groups = json.load(fh)["groups"]
        except (OSError, ValueError, KeyError):
            continue
        found += [os.path.join(os.path.expanduser(backups), run,
                               "log_%s.txt" % g["representative"])
                  for g in groups if g.get("key") == key]
    return found


def rekey(data, backups):
    """Moves every entry to the key that today's signature() gives the
    reports it was decided on. An entry for a coarse old key that now splits
    keeps its verdict only for the representative that was investigated.
    """
    out = {}
    moved = 0
    for key, entry in data.items():
        if key.startswith("_"):
            continue
        reports = entry_reports(entry, key, backups)
        if reports:
            entry["reports"] = reports
        if not entry.get("version"):
            version = decided_version(entry, backups)
            if version:
                entry["version"] = version
        targets = set()
        for path in reports:
            rep = parse_log(path) if os.path.exists(path) else None
            if rep and rep["frames"]:
                targets.add(stable_key(signature(rep)[0]))
        targets = sorted(targets) or [key]
        if targets != [key]:
            moved += 1
        for target in targets:
            mine, theirs = copy.deepcopy(entry), out.get(target)
            if theirs is not None:
                order = lambda e: (e.get("version") or 0, e.get("last_seen", ""))
                if order(theirs) > order(mine):
                    mine, theirs = theirs, mine
                mine.setdefault("history", []).append({
                    k: theirs[k] for k in ("verdict", "commit", "note", "label")
                    if theirs.get(k)})
            out[target] = mine
    out["_signature"] = SIGNATURE_VERSION
    return out, moved


def cmd_rekey(args):
    data = load_ledger(args.ledger)
    data, moved = rekey(data, args.backups)
    save_ledger(args.ledger, data)
    print("ledger re-keyed for signature v%d: %d of %d entries moved" % (
        SIGNATURE_VERSION, moved, len(data) - 1))
    return 0


def save_ledger(path, data):
    path = os.path.expanduser(path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
    os.replace(tmp, path)


def cmd_ledger(args):
    data = load_ledger(args.ledger)
    if not data:
        print("ledger is empty (%s)" % os.path.expanduser(args.ledger))
        return 0
    rows = sorted(((k, v) for k, v in data.items() if not k.startswith("_")),
                  key=lambda kv: kv[1].get("last_seen", ""), reverse=True)
    print("%-12s  %-16s  %-6s  %s" % ("key", "verdict", "seen", "signature"))
    for key, entry in rows:
        print("%-12s  %-16s  %-6d  %s" % (
            key, entry.get("verdict", "?"), entry.get("seen", 0),
            _clip(entry.get("signature", ""), 90)))
    return 0


def cmd_record(args):
    payload = load_payload(os.path.expanduser(args.out))
    group = None
    for g in payload["groups"]:
        if g["gid"].upper() == args.gid.upper():
            group = g
            break
    if group is None:
        sys.exit("no group %s in %s" % (args.gid, args.out))
    data = load_ledger(args.ledger)
    key = group["key"]
    stamp = datetime.datetime.now().astimezone().strftime("%Y-%m-%d")
    entry = data.get(key, {"first_seen": stamp, "seen": 0, "runs": []})
    if entry.get("verdict") and entry.get("label") != payload["label"]:
        # Decided again for a newer release: keep what was decided before.
        entry.setdefault("history", []).append({
            k: entry[k] for k in ("verdict", "commit", "note", "label")
            if entry.get(k)})
        entry.pop("commit", None)
    entry["signature"] = (group["assertion"] + " || " if group["assertion"]
                          else "") + group["signature"]
    entry["verdict"] = args.verdict
    entry["label"] = payload["label"]
    entry["version"] = min(payload["accepted_versions"])
    entry["last_seen"] = stamp
    entry["seen"] = entry.get("seen", 0) + group["count"]
    if args.note:
        entry["note"] = args.note
    if args.commit:
        entry["commit"] = args.commit
    run = os.path.basename(os.path.dirname(os.path.expanduser(args.out).rstrip("/")))
    if run and run not in entry["runs"]:
        entry["runs"].append(run)
    # What a later rekey re-signs, so a changed signature keeps this verdict.
    report = os.path.join(payload["crashes"],
                          "log_%s.txt" % group["representative"])
    entry["reports"] = [r for r in entry.get("reports", []) if r != report]
    entry["reports"].append(report)
    data[key] = entry
    save_ledger(args.ledger, data)
    print("recorded %s %s -> %s" % (args.gid, key, args.verdict))
    return 0


# --------------------------------------------------------------------------
# repo cross-reference
# --------------------------------------------------------------------------

class RepoIndex(object):
    """Maps a dump's bare file name back to a tracked path, and says whether
    that path moved between the tagged build and the branch fixes land on."""

    def __init__(self, repo, tag, against):
        self.ok = False
        self.by_name = {}
        self.changed = set()
        self.tag = tag
        self.against = against
        listing = git(repo, "ls-files", "--recurse-submodules")
        if not listing:
            listing = git(repo, "ls-files")
        if not listing:
            return
        for path in listing.splitlines():
            self.by_name.setdefault(os.path.basename(path), []).append(path)
        diff = git(repo, "diff", "--name-only", "%s..%s" % (tag, against))
        self.changed = {line.strip() for line in diff.splitlines() if line.strip()}
        self.ok = True

    def lookup(self, filename):
        """-> (path_or_None, touched_since_tag)."""
        if not self.ok or not filename:
            return None, False
        paths = self.by_name.get(filename)
        if not paths:
            return None, False
        # Prefer app sources over third-party copies of the same file name.
        paths = sorted(paths, key=lambda p: (
            0 if p.startswith("Telegram/SourceFiles/") else
            1 if p.startswith("Telegram/lib_") else 2, len(p)))
        path = paths[0] if len(paths) == 1 else "|".join(paths[:3])
        touched = any(p in self.changed for p in paths)
        return path, touched


# --------------------------------------------------------------------------
# scan
# --------------------------------------------------------------------------

def classify(rep, accepted):
    if rep["version"] not in accepted:
        return "old-version"
    if rep["no_frames"] or not rep["frames"]:
        return "no-frames"
    if not any(f.symbol for f in rep["frames"]):
        return "unsymbolized"
    return "usable"


def source_map(index, frames, limit=6):
    seen = set()
    out = []
    for f in frames:
        if not f.file or f.file in seen:
            continue
        path, touched = index.lookup(f.file)
        if not path:
            continue
        seen.add(f.file)
        out.append({"file": f.file, "line": f.line, "path": path,
                    "touched": touched})
        if len(out) >= limit:
            break
    return out


def cmd_scan(args):
    crashes = os.path.expanduser(args.crashes)
    if getattr(args, "claim", False):
        info = claim(args.crashes, args.backups)
        rc = report_claim(info, args.crashes)
        if rc:
            return rc
        crashes = info["run_dir"]
        print("")

    repo = os.path.abspath(args.repo)
    label, accepted = resolve_target(repo, args.version)
    out = os.path.expanduser(args.out) if args.out else os.path.join(
        crashes, "triage")
    os.makedirs(out, exist_ok=True)
    # Investigations write their long-form notes and patch hints here.
    os.makedirs(os.path.join(out, "results"), exist_ok=True)

    index = RepoIndex(repo, label, args.against)

    logs = sorted(
        os.path.join(crashes, n) for n in os.listdir(crashes)
        if n.startswith("log_") and n.endswith(".txt"))

    buckets = {"old-version": [], "no-frames": [], "unsymbolized": [],
               "usable": []}
    groups = OrderedDict()
    for path in logs:
        rep = parse_log(path)
        if rep is None:
            continue
        kind = classify(rep, accepted)
        buckets[kind].append(rep)
        if kind != "usable":
            continue
        key, loose, exact, trimmed = signature(rep)
        g = groups.setdefault(key, {
            "raw_key": key, "loose": loose, "members": [], "exacts": Counter(),
            "reasons": Counter(), "platforms": Counter(), "tags": Counter(),
            "users": set(), "assertion": rep["header"].get("Assertion", ""),
            "rep": rep, "trimmed": trimmed})
        g["members"].append(rep)
        g["exacts"][exact] += 1
        g["reasons"][rep["reason"]] += 1
        g["platforms"][rep["header"].get("Platform", "?")] += 1
        g["users"].add(rep["header"].get("UserTag", "?"))
        for t in hints(rep, trimmed):
            g["tags"][t] += 1
        # Keep the richest report as the representative.
        if len(trimmed) > len(g["trimmed"]):
            g["rep"], g["trimmed"] = rep, trimmed

    ledger = load_ledger(args.ledger)
    if ledger and ledger.get("_signature") != SIGNATURE_VERSION:
        ledger, moved = rekey(ledger, args.backups)
        save_ledger(args.ledger, ledger)
        print("ledger re-keyed for signature v%d: %d entries moved" % (
            SIGNATURE_VERSION, moved))
    version = min(accepted)
    ordered = sorted(groups.values(), key=lambda g: -len(g["members"]))
    for g in ordered:
        g["key"] = stable_key(g["raw_key"])
        g["known"] = ledger.get(g["key"])
        g["previous"] = None
        if g["known"] and recurring(
                g["known"], version, repo, label, args.backups):
            g["previous"], g["known"] = g["known"], None
            g["tags"]["recurring"] = len(g["members"])
    # Groups still needing a decision come first and keep the low numbers.
    ordered.sort(key=lambda g: (g["known"] is not None, -len(g["members"])))
    for i, g in enumerate(ordered, 1):
        g["gid"] = "G%02d" % i
    fresh = [g for g in ordered if g["known"] is None]
    again = [g for g in fresh if g["previous"]]
    known = [g for g in ordered if g["known"] is not None]
    known_reports = sum(len(g["members"]) for g in known)

    # ---- index.tsv -------------------------------------------------------
    with open(os.path.join(out, "index.tsv"), "w") as fh:
        fh.write("\t".join((
            "id", "group", "kind", "version", "platform", "binary", "reason",
            "uptime_s", "usertag", "assertion", "top_frame")) + "\n")
        gid_of = {}
        for g in ordered:
            for rep in g["members"]:
                gid_of[rep["id"]] = g["gid"]
        for kind in ("usable", "no-frames", "unsymbolized", "old-version"):
            for rep in buckets[kind]:
                trimmed = rep["frames"]
                fh.write("\t".join((
                    rep["id"], gid_of.get(rep["id"], "-"), kind,
                    rep["version_raw"] or "?",
                    rep["header"].get("Platform", "?"),
                    rep["header"].get("Binary", "?"),
                    rep["reason"] or "?",
                    re.sub(r"[^0-9]", "", rep["uptime"]) or "?",
                    rep["header"].get("UserTag", "?"),
                    rep["header"].get("Assertion", ""),
                    trimmed[0].pretty() if trimmed else "<none>")) + "\n")

    # ---- groups.md -------------------------------------------------------
    lines = []
    lines.append("# Crash groups for %s" % label)
    lines.append("")
    lines.append("Corpus: `%s`  (%d reports)" % (crashes, len(logs)))
    lines.append("")
    lines.append("Dump line numbers correspond to `%s`; \"touched since the "
                 "tag\" is measured against `%s`." % (label, args.against))
    lines.append("")
    lines.append("| bucket | reports |")
    lines.append("| --- | --- |")
    for kind in ("usable", "no-frames", "unsymbolized", "old-version"):
        lines.append("| %s | %d |" % (kind, len(buckets[kind])))
    lines.append("| **distinct groups** | **%d** |" % len(ordered))
    lines.append("| of those, new | %d |" % (len(fresh) - len(again)))
    lines.append("| of those, recurring after a fix | %d |" % len(again))
    lines.append("| of those, already in the ledger | %d (%d reports) |" % (
        len(known), known_reports))
    lines.append("")
    if again:
        lines.append("Recurring groups were recorded as fixed for an older "
                     "release, yet still crash in %s: that fix did not cover "
                     "them, so they are investigated again." % label)
        lines.append("")
    if known:
        lines.append("Groups %s..%s were decided in an earlier run; they are "
                     "listed at the end and need no fresh investigation."
                     % (known[0]["gid"], known[-1]["gid"]))
        lines.append("")
    for g in ordered:
        rep = g["rep"]
        machines = len(g["users"])
        lines.append("## %s  (%d report%s, %d machine%s)" % (
            g["gid"], len(g["members"]), "" if len(g["members"]) == 1 else "s",
            machines, "" if machines == 1 else "s"))
        lines.append("")
        if g["assertion"]:
            lines.append("- assertion: `%s`" % g["assertion"])
        lines.append("- reason: %s" % ", ".join(
            "%s x%d" % (k or "?", v) for k, v in g["reasons"].most_common()))
        lines.append("- platform: %s" % ", ".join(
            "%s x%d" % (k, v) for k, v in g["platforms"].most_common()))
        if g["tags"]:
            lines.append("- hints: %s" % ", ".join(
                "%s x%d" % (k, v) for k, v in g["tags"].most_common()))
        if g["known"]:
            lines.append("- **already decided**: %s (%s)" % (
                g["known"].get("verdict", "?"), g["known"].get("note", "")))
        if g["previous"]:
            lines.append("- **recurring**: %s" % previous_text(g["previous"]))
        lines.append("- key: `%s`" % g["key"])
        lines.append("- representative: `log_%s.txt`  (uptime %s)" % (
            rep["id"], rep["uptime"] or "?"))
        lines.append("- members: %s" % " ".join(
            r["id"] for r in g["members"]))
        lines.append("")
        lines.append("```")
        for f in g["trimmed"][:10]:
            lines.append("%2d  %s" % (f.index, f.pretty()))
        lines.append("```")
        sources = source_map(index, g["trimmed"])
        if sources:
            lines.append("")
            lines.append("Sources (line numbers are as of `%s`):" % label)
            lines.append("")
            for entry in sources:
                lines.append("- `%s:%d` -> `%s`%s" % (
                    entry["file"], entry["line"], entry["path"],
                    "  **touched since the tag**" if entry["touched"] else ""))
        lines.append("")
    with open(os.path.join(out, "groups.md"), "w") as fh:
        fh.write("\n".join(lines) + "\n")

    # ---- groups.json (for tooling) ---------------------------------------
    payload = {
        "label": label,
        "accepted_versions": sorted(accepted),
        "crashes": crashes,
        "out": out,
        "counts": {k: len(v) for k, v in buckets.items()},
        "drop_ids": {k: [r["id"] for r in buckets[k]]
                     for k in ("old-version", "no-frames", "unsymbolized")},
        "fresh": [g["gid"] for g in fresh],
        "groups": [{
            "gid": g["gid"],
            "key": g["key"],
            "known": g["known"],
            "previous": g["previous"],
            "count": len(g["members"]),
            "machines": len(g["users"]),
            "assertion": g["assertion"],
            "signature": g["loose"],
            "tags": sorted(g["tags"]),
            "representative": g["rep"]["id"],
            "members": [r["id"] for r in g["members"]],
            "frames": [f.pretty() for f in g["trimmed"][:10]],
            "sources": source_map(index, g["trimmed"]),
        } for g in ordered],
    }
    with open(os.path.join(out, "groups.json"), "w") as fh:
        json.dump(payload, fh, indent=2)

    print("target %s (versions %s)" % (
        label, ", ".join(str(v) for v in sorted(accepted))))
    for kind in ("usable", "no-frames", "unsymbolized", "old-version"):
        print("  %-13s %4d" % (kind, len(buckets[kind])))
    print("  %-13s %4d  (%d new, %d recurring after a fix, %d already in "
          "the ledger with %d reports)" % (
              "groups", len(ordered), len(fresh) - len(again), len(again),
              len(known), known_reports))
    print("wrote %s/{index.tsv,groups.md,groups.json}" % out)
    return 0


# --------------------------------------------------------------------------
# show / sweep
# --------------------------------------------------------------------------

def previous_text(entry):
    return "recorded %s%s%s -- %s" % (
        entry.get("verdict", "?"),
        " for " + entry["label"] if entry.get("label") else "",
        " by " + entry["commit"] if entry.get("commit") else "",
        entry.get("note", ""))


def load_payload(out):
    with open(os.path.join(out, "groups.json")) as fh:
        return json.load(fh)


REGISTER_LINE = re.compile(r"^\s{4,}\S+\s+=\s+0x[0-9a-fA-F]+")


def cmd_show(args):
    out = os.path.expanduser(args.out)
    payload = load_payload(out)
    wanted = {g.upper() for g in args.group}
    shown = 0
    for g in payload["groups"]:
        if wanted and g["gid"] not in wanted:
            continue
        shown += 1
        path = os.path.join(payload["crashes"],
                            "log_%s.txt" % g["representative"])
        if not os.path.exists(path):
            path = os.path.join(out, "_dropped", "old-version",
                                "log_%s.txt" % g["representative"])
        print("=" * 78)
        print("%s  %d report%s, %d machine%s  tags=%s" % (
            g["gid"], g["count"], "" if g["count"] == 1 else "s",
            g["machines"], "" if g["machines"] == 1 else "s",
            ",".join(g["tags"]) or "-"))
        if g.get("known"):
            print("ALREADY DECIDED in an earlier run: %s -- %s" % (
                g["known"].get("verdict", "?"), g["known"].get("note", "")))
        if g.get("previous"):
            print("RECURRING: %s" % previous_text(g["previous"]))
            print("It still crashes in %s, so that diagnosis did not cover "
                  "these reports; investigate them from scratch."
                  % payload["label"])
        print("members: %s" % " ".join(g["members"]))
        print("all member logs: %s/log_<id>.txt" % payload["crashes"])
        print("=" * 78)
        with open(path, errors="replace") as fh:
            body = fh.read().splitlines()

        emitted = 0
        in_thread = None
        for line in body:
            if line.startswith("Loaded modules:"):
                if not args.modules:
                    print("")
                    print("[module list omitted; pass --modules to see it]")
                    break
                in_thread = None
            m = THREAD_RE.match(line)
            if m:
                in_thread = bool(m.group(2))
                if not in_thread and not args.all_threads:
                    continue
            if in_thread is False and not args.all_threads:
                continue
            if REGISTER_LINE.match(line) and not args.registers:
                continue
            if line.strip().startswith("Found by:") and not args.registers:
                continue
            print(line)
            emitted += 1
            if emitted >= args.lines:
                print("[truncated at --lines %d]" % args.lines)
                break
        print("")
    if wanted and not shown:
        sys.exit("no such group(s): %s" % ", ".join(sorted(wanted)))
    return 0


def cmd_sweep(args):
    out = os.path.expanduser(args.out)
    payload = load_payload(out)
    crashes = payload["crashes"]
    kinds = args.kinds.split(",")
    total = 0
    for kind in kinds:
        ids = payload["drop_ids"].get(kind, [])
        if not ids:
            continue
        dest = os.path.join(out, "_dropped", kind)
        if not args.delete:
            os.makedirs(dest, exist_ok=True)
        for cid in ids:
            for prefix, ext in (("log", "txt"), ("err", "txt"), ("dmp", "dmp")):
                src = os.path.join(crashes, "%s_%s.%s" % (prefix, cid, ext))
                if not os.path.exists(src):
                    continue
                if args.dry_run:
                    print("would %s %s" % (
                        "delete" if args.delete else "move", src))
                elif args.delete:
                    os.remove(src)
                else:
                    shutil.move(src, os.path.join(dest, os.path.basename(src)))
            total += 1
    verb = "would drop" if args.dry_run else (
        "deleted" if args.delete else "moved")
    print("%s %d report triples (%s)" % (verb, total, ",".join(kinds)))
    if not args.delete and not args.dry_run:
        print("destination: %s/_dropped/" % out)
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd")

    s = sub.add_parser("scan", help="classify and cluster the run folder")
    s.add_argument("--crashes", default="~/Telegram/Crashes/all")
    s.add_argument("--repo", default=".")
    s.add_argument("--version", default=None,
                   help="7.1.2 / 7001002; default = newest v*.*.* tag")
    s.add_argument("--out", default=None)
    s.add_argument("--claim", action="store_true",
                   help="first rotate all/ into a fresh backup run folder and "
                        "scan that, so the next run only sees new reports")
    s.add_argument("--backups", default="~/Telegram/Crashes/backup")
    s.add_argument("--against", default="HEAD",
                   help="branch fixes land on; used to flag files that already "
                        "moved since the tag (default HEAD)")
    s.add_argument("--ledger", default=DEFAULT_LEDGER)
    s.set_defaults(func=cmd_scan)

    s = sub.add_parser("ledger", help="show what past runs decided")
    s.add_argument("--ledger", default=DEFAULT_LEDGER)
    s.set_defaults(func=cmd_ledger)

    s = sub.add_parser("record", help="write one group's verdict to the ledger")
    s.add_argument("gid")
    s.add_argument("--out", required=True)
    s.add_argument("--verdict", required=True,
                   choices=["fixed", "not-actionable", "needs-discussion"])
    s.add_argument("--note", default="")
    s.add_argument("--commit", default="")
    s.add_argument("--ledger", default=DEFAULT_LEDGER)
    s.set_defaults(func=cmd_record)

    s = sub.add_parser("rekey", help="move ledger entries to today's keys")
    s.add_argument("--backups", default="~/Telegram/Crashes/backup")
    s.add_argument("--ledger", default=DEFAULT_LEDGER)
    s.set_defaults(func=cmd_rekey)

    s = sub.add_parser("claim", help="rotate all/ into a fresh run folder")
    s.add_argument("--crashes", default="~/Telegram/Crashes/all")
    s.add_argument("--backups", default="~/Telegram/Crashes/backup")
    s.add_argument("--new", action="store_true",
                   help="always start a new run folder, even if nothing is new")
    s.set_defaults(func=cmd_claim)

    s = sub.add_parser("show", help="print a group's representative report")
    s.add_argument("group", nargs="*")
    s.add_argument("--out", required=True)
    s.add_argument("--lines", type=int, default=400)
    s.add_argument("--registers", action="store_true",
                   help="keep register dumps and 'Found by' lines")
    s.add_argument("--all-threads", action="store_true",
                   help="keep the non-crashed threads too")
    s.add_argument("--modules", action="store_true",
                   help="keep the loaded-module list")
    s.set_defaults(func=cmd_show)

    s = sub.add_parser("sweep", help="move or delete the dropped triples")
    s.add_argument("--out", required=True)
    s.add_argument("--kinds", default="old-version,no-frames,unsymbolized")
    s.add_argument("--delete", action="store_true",
                   help="delete instead of moving into <out>/_dropped/")
    s.add_argument("--dry-run", action="store_true")
    s.set_defaults(func=cmd_sweep)

    args = p.parse_args()
    if not getattr(args, "func", None):
        p.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
