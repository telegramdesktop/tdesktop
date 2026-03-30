#!/usr/bin/env python3

import argparse
import collections
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_GLOBS = [
    "*.cpp",
    "*.h",
]
PATTERN = (
    r"DevicePixelRatio(Exact)?\(\)"
    r"|setDevicePixelRatio\("
    r"|QImage\("
    r"|QPixmap\("
    r"|drawImage\("
    r"|drawPixmap\("
    r"|QSize\("
    r"|QRect\("
    r"|qRound\("
    r"|qCeil\("
    r"|qFloor\("
    r"|int\s*\("
    r"|\bratio\b"
)


def run_rg(globs: list[str], path_filters: list[str]) -> list[str]:
    cmd = ["rg", "-n", "-H", "-S", PATTERN]
    for glob in globs:
        cmd.extend(["-g", glob])
    cmd.extend(path_filters or ["Telegram", "cmake"])
    result = subprocess.run(
        cmd,
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode not in (0, 1):
        print(result.stderr.strip(), file=sys.stderr)
        raise SystemExit(result.returncode)
    return [line for line in result.stdout.splitlines() if line]


def classify(text: str) -> tuple[str, list[str]]:
    reasons: list[str] = []
    score = 0

    has_dpr = bool(re.search(r"DevicePixelRatio(Exact)?\(\)", text))
    has_ratio_var = bool(re.search(r"\bratio\b", text))
    if not has_dpr and not has_ratio_var:
        return ("", [])

    ratio_decl_only = bool(re.search(
        r"^\s*(const\s+)?auto\s+ratio\s*=\s*.*(DevicePixelRatio(Exact)?\(\)|devicePixelRatio(F)?\(\))\s*;\s*$",
        text))
    ratio_derived = bool(re.search(
        r"\bratio\b\s*=\s*.*(DevicePixelRatio(Exact)?\(\)|devicePixelRatio(F)?\(\))",
        text))
    uses_ratio_math = bool(re.search(r"[*\/]\s*ratio\b|\bratio\s*[*\/]", text))
    uses_ratio_in_constructor = bool(re.search(
        r"(QSize|QRect|QPoint|QMargins|QImage|QPixmap)\s*\([^;\n]*\bratio\b",
        text))
    uses_dpr_in_constructor = bool(re.search(
        r"(QSize|QRect|QPoint|QMargins|QImage|QPixmap)\s*\([^;\n]*DevicePixelRatio(Exact)?\(\)",
        text))
    uses_per_image_dpr = bool(re.search(
        r"image\.devicePixelRatio\(\)|pixmap\.devicePixelRatio\(\)|maskImage\.devicePixelRatio\(\)|_pixmap\.devicePixelRatio\(\)",
        text))
    already_rounded_ratio = bool(re.search(
        r"(qRound|base::SafeRound)\s*\([^)]*\bratio\b",
        text))
    already_rounded_dpr = bool(re.search(
        r"(qRound|base::SafeRound)\s*\([^)]*DevicePixelRatio(Exact)?\(\)",
        text))
    constructor_guarded_by_rounding = (
        (uses_ratio_in_constructor or uses_dpr_in_constructor)
        and ("qRound(" in text or "base::SafeRound(" in text))

    if uses_dpr_in_constructor or uses_ratio_in_constructor:
        score += 4
        reasons.append("DPR used inside Qt integer geometry/image constructor")

    if re.search(r"\b(int|uint|auto)\s+\w+\s*=\s*.*DevicePixelRatio", text):
        score += 2
        reasons.append("DPR value assigned into likely integral sizing path")

    if re.search(r"\bint\s*\([^)]*DevicePixelRatio", text):
        score += 4
        reasons.append("explicit int cast around DPR expression")

    if re.search(r"\bint\s*\([^)]*\bratio\b", text):
        score += 4
        reasons.append("explicit int cast around local ratio expression")

    if re.search(r"(qCeil|qFloor)\s*\([^)]*DevicePixelRatio", text):
        score += 4
        reasons.append("explicit rounding of DPR expression")

    if re.search(r"(qCeil|qFloor)\s*\([^)]*\bratio\b", text):
        score += 4
        reasons.append("explicit rounding of local ratio expression")

    if re.search(r"/\s*style::DevicePixelRatio(Exact)?\(\)", text):
        score += 3
        reasons.append("physical-to-logical conversion using global DPR")

    if re.search(r"\*\s*style::DevicePixelRatio(Exact)?\(\)", text):
        score += 2
        reasons.append("logical-to-physical conversion using global DPR")

    if ratio_derived:
        score += 1
        reasons.append("local ratio derived from DPR source")

    if uses_ratio_math:
        score += 2
        reasons.append("local ratio used in scale conversion math")

    if re.search(r"setDevicePixelRatio\s*\(", text):
        score += 2
        reasons.append("image/pixmap DPR tagging site")

    if re.search(r"(drawImage|drawPixmap)\s*\(", text):
        score += 2
        reasons.append("paint site using DPR-sensitive raster content")

    if already_rounded_dpr:
        score -= 2
        reasons.append("already uses explicit rounding")

    if already_rounded_ratio:
        score -= 2
        reasons.append("already uses explicit rounding with local ratio")

    if uses_per_image_dpr:
        score -= 3
        reasons.append("uses per-image/pixmap DPR")

    if ratio_decl_only and not uses_ratio_math and not uses_ratio_in_constructor:
        score -= 3
        reasons.append("plain local DPR ratio declaration")

    if uses_ratio_in_constructor and already_rounded_ratio:
        score -= 4
        reasons.append("constructor use already guarded by explicit rounding")

    if uses_dpr_in_constructor and already_rounded_dpr:
        score -= 4
        reasons.append("constructor use already guarded by explicit rounding")

    if constructor_guarded_by_rounding:
        score -= 4
        reasons.append("constructor use already guarded by explicit rounding")

    if (uses_ratio_in_constructor or uses_dpr_in_constructor) and uses_per_image_dpr:
        score -= 2
        reasons.append("constructor use already guarded by per-image DPR")

    if score >= 6:
        return ("high", reasons)
    if score >= 3:
        return ("medium", reasons)
    return ("low", reasons)


def parse_line(line: str) -> tuple[str, int, str]:
    path, lineno, text = line.split(":", 2)
    return (path, int(lineno), text.strip())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", help="Optional paths to limit the audit.")
    parser.add_argument("--max-per-level", type=int, default=80)
    parser.add_argument("--level", choices=["high", "medium", "low", "all"], default="all")
    args = parser.parse_args()

    results = collections.defaultdict(list)
    for raw in run_rg(DEFAULT_GLOBS, args.paths):
        path, lineno, text = parse_line(raw)
        level, reasons = classify(text)
        if not level:
            continue
        if args.level != "all" and level != args.level:
            continue
        results[level].append((path, lineno, text, reasons))

    order = ["high", "medium", "low"]
    for level in order:
        entries = sorted(results[level], key=lambda item: (item[0], item[1]))
        if not entries:
            continue
        print(f"[{level.upper()}] {len(entries)}")
        for path, lineno, text, reasons in entries[:args.max_per_level]:
            unique = list(dict.fromkeys(reasons))
            reason_text = "; ".join(unique[:3])
            print(f"{path}:{lineno}: {text}")
            if reason_text:
                print(f"  -> {reason_text}")
        if len(entries) > args.max_per_level:
            print(f"  ... {len(entries) - args.max_per_level} more")
        print()

    total = sum(len(results[level]) for level in order)
    print(f"Total flagged sites: {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
