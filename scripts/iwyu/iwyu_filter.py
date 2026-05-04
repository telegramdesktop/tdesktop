#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
"""Filter and rewrite IWYU output: drop submodule/platform paths, fix
generated-header quoting, normalise Qt long-form. Platform regexes
mirror cmake/nice_target_sources.cmake."""

from __future__ import annotations

import argparse
import configparser
import re
import sys
from pathlib import Path
from typing import Iterable

_PLATFORM_PATTERNS = {
    "win": [
        re.compile(r"(^|/)win/"),
        re.compile(r"(^|/)winrc/"),
        re.compile(r"(^|/)windows/"),
        re.compile(r"[_/]win\."),
    ],
    "mac": [
        re.compile(r"(^|/)mac/"),
        re.compile(r"(^|/)darwin/"),
        re.compile(r"(^|/)osx/"),
        re.compile(r"[_/]mac\."),
        re.compile(r"[_/]darwin\."),
        re.compile(r"[_/]osx\."),
    ],
    "linux": [
        re.compile(r"(^|/)linux/"),
        re.compile(r"[_/]linux\."),
    ],
    "posix": [
        re.compile(r"(^|/)posix/"),
        re.compile(r"[_/]posix\."),
    ],
}


def _load_submodule_paths(repo_root: Path) -> list[str]:
    gitmodules = repo_root / ".gitmodules"
    if not gitmodules.exists():
        return []

    parser = configparser.ConfigParser()
    parser.read(gitmodules, encoding="utf-8")
    paths: list[str] = []
    for section in parser.sections():
        if not section.startswith("submodule "):
            continue
        path = parser.get(section, "path", fallback=None)
        if path:
            paths.append(path.replace("\\", "/").rstrip("/"))
    return paths


def _normalize(path_str: str, repo_root: Path) -> str:
    p = Path(path_str)
    try:
        rel = p.resolve().relative_to(repo_root.resolve())
        return rel.as_posix()
    except (ValueError, OSError):
        return path_str.replace("\\", "/")


class IwyuFilter:
    def __init__(
        self,
        repo_root: Path,
        skip_platforms: Iterable[str] = (),
        only_under: str = "Telegram/SourceFiles",
        extra_ignore_globs: Iterable[str] = (),
    ) -> None:
        self.repo_root = repo_root
        self.only_under = only_under.replace("\\", "/").rstrip("/")
        self.submodules = _load_submodule_paths(repo_root)
        self.platform_patterns: list[re.Pattern[str]] = []
        for name in skip_platforms:
            self.platform_patterns.extend(_PLATFORM_PATTERNS.get(name, ()))
        self.extra_ignores = [
            re.compile(self._glob_to_regex(g)) for g in extra_ignore_globs
        ]

    @staticmethod
    def _glob_to_regex(glob: str) -> str:
        out: list[str] = []
        i = 0
        while i < len(glob):
            c = glob[i]
            if c == "*" and i + 1 < len(glob) and glob[i + 1] == "*":
                out.append(".*")
                i += 2
            elif c == "*":
                out.append(r"[^/]*")
                i += 1
            elif c == "?":
                out.append(r"[^/]")
                i += 1
            else:
                out.append(re.escape(c))
                i += 1
        return "^" + "".join(out) + "$"

    def is_submodule(self, rel_path: str) -> bool:
        for sub in self.submodules:
            if rel_path == sub or rel_path.startswith(sub + "/"):
                return True
        return False

    def is_platform_excluded(self, rel_path: str) -> bool:
        return any(p.search(rel_path) for p in self.platform_patterns)

    def is_extra_ignored(self, rel_path: str) -> bool:
        return any(p.match(rel_path) for p in self.extra_ignores)

    def is_in_scope(self, rel_path: str) -> bool:
        if self.only_under and not (
            rel_path == self.only_under
            or rel_path.startswith(self.only_under + "/")
        ):
            return False
        return True

    def accepts(self, path_str: str) -> tuple[bool, str]:
        rel = _normalize(path_str, self.repo_root)
        if not self.is_in_scope(rel):
            return False, f"out-of-scope (not under {self.only_under})"
        if self.is_submodule(rel):
            return False, "submodule path"
        if self.is_platform_excluded(rel):
            return False, "platform-specific file"
        if self.is_extra_ignored(rel):
            return False, "extra ignore"
        return True, ""


# Generated subdirs the project always quotes (IWYU emits angle brackets
# for build-dir includes). Add new gen subdirs here.
_QUOTE_GEN_PREFIXES = ("styles/",)
_GEN_INCLUDE_RE = re.compile(
    r"#include <(?P<prefix>" + "|".join(re.escape(p) for p in _QUOTE_GEN_PREFIXES)
    + r")(?P<rest>[^>]+)>"
)

# Qt classes the project quotes as <QtModule/QClass> instead of <QClass>.
# Regenerate via:
#   grep -rhoE '#include <Qt[A-Z][a-zA-Z]+/Q[A-Z][a-zA-Z]+>' \
#       Telegram/SourceFiles/ | sort -u
_QT_LONG_FORM = {
    "QAbstractEventDispatcher":  "QtCore/QAbstractEventDispatcher",
    "QAbstractNativeEventFilter": "QtCore/QAbstractNativeEventFilter",
    "QAction":                   "QtGui/QAction",
    "QApplication":              "QtWidgets/QApplication",
    "QAuthenticator":            "QtNetwork/QAuthenticator",
    "QBrush":                    "QtGui/QBrush",
    "QBuffer":                   "QtCore/QBuffer",
    "QByteArray":                "QtCore/QByteArray",
    "QChar":                     "QtCore/QChar",
    "QCheckBox":                 "QtWidgets/QCheckBox",
    "QClipboard":                "QtGui/QClipboard",
    "QColor":                    "QtGui/QColor",
    "QCoreApplication":          "QtCore/QCoreApplication",
    "QCursor":                   "QtGui/QCursor",
    "QDataStream":               "QtCore/QDataStream",
    "QDate":                     "QtCore/QDate",
    "QDateTime":                 "QtCore/QDateTime",
    "QDebug":                    "QtCore/QDebug",
    "QDesktopServices":          "QtGui/QDesktopServices",
    "QDir":                      "QtCore/QDir",
    "QDirIterator":              "QtCore/QDirIterator",
    "QDrag":                     "QtGui/QDrag",
    "QEvent":                    "QtCore/QEvent",
    "QFile":                     "QtCore/QFile",
    "QFileDialog":               "QtWidgets/QFileDialog",
    "QFileInfo":                 "QtCore/QFileInfo",
    "QFileSystemWatcher":        "QtCore/QFileSystemWatcher",
    "QFont":                     "QtGui/QFont",
    "QFontDatabase":             "QtGui/QFontDatabase",
    "QFontInfo":                 "QtGui/QFontInfo",
    "QFontMetrics":              "QtGui/QFontMetrics",
    "QGuiApplication":           "QtGui/QGuiApplication",
    "QHash":                     "QtCore/QHash",
    "QHttpMultiPart":            "QtNetwork/QHttpMultiPart",
    "QIcon":                     "QtGui/QIcon",
    "QImage":                    "QtGui/QImage",
    "QImageReader":              "QtGui/QImageReader",
    "QImageWriter":              "QtGui/QImageWriter",
    "QInputMethod":              "QtGui/QInputMethod",
    "QJsonArray":                "QtCore/QJsonArray",
    "QJsonDocument":             "QtCore/QJsonDocument",
    "QJsonObject":               "QtCore/QJsonObject",
    "QJsonParseError":           "QtCore/QJsonParseError",
    "QJsonValue":                "QtCore/QJsonValue",
    "QLabel":                    "QtWidgets/QLabel",
    "QLibraryInfo":              "QtCore/QLibraryInfo",
    "QLineEdit":                 "QtWidgets/QLineEdit",
    "QList":                     "QtCore/QList",
    "QLocale":                   "QtCore/QLocale",
    "QLocalServer":              "QtNetwork/QLocalServer",
    "QLocalSocket":              "QtNetwork/QLocalSocket",
    "QLockFile":                 "QtCore/QLockFile",
    "QLoggingCategory":          "QtCore/QLoggingCategory",
    "QMap":                      "QtCore/QMap",
    "QMargins":                  "QtCore/QMargins",
    "QMenu":                     "QtWidgets/QMenu",
    "QMenuBar":                  "QtWidgets/QMenuBar",
    "QMimeData":                 "QtCore/QMimeData",
    "QMimeDatabase":             "QtCore/QMimeDatabase",
    "QMimeType":                 "QtCore/QMimeType",
    "QMouseEvent":               "QtGui/QMouseEvent",
    "QMutex":                    "QtCore/QMutex",
    "QNetworkAccessManager":     "QtNetwork/QNetworkAccessManager",
    "QNetworkProxy":             "QtNetwork/QNetworkProxy",
    "QNetworkReply":             "QtNetwork/QNetworkReply",
    "QNetworkRequest":           "QtNetwork/QNetworkRequest",
    "QObject":                   "QtCore/QObject",
    "QOpenGLFunctions":          "QtGui/QOpenGLFunctions",
    "QOperatingSystemVersion":   "QtCore/QOperatingSystemVersion",
    "QPainter":                  "QtGui/QPainter",
    "QPainterPath":              "QtGui/QPainterPath",
    "QPainterPathStroker":       "QtGui/QPainterPathStroker",
    "QPair":                     "QtCore/QPair",
    "QPalette":                  "QtGui/QPalette",
    "QPen":                      "QtGui/QPen",
    "QPixmap":                   "QtGui/QPixmap",
    "QPoint":                    "QtCore/QPoint",
    "QPointer":                  "QtCore/QPointer",
    "QProcess":                  "QtCore/QProcess",
    "QPushButton":               "QtWidgets/QPushButton",
    "QReadWriteLock":            "QtCore/QReadWriteLock",
    "QRect":                     "QtCore/QRect",
    "QRegion":                   "QtGui/QRegion",
    "QRegularExpression":        "QtCore/QRegularExpression",
    "QRgb":                      "QtGui/QRgb",
    "QSaveFile":                 "QtCore/QSaveFile",
    "QScreen":                   "QtGui/QScreen",
    "QScrollBar":                "QtWidgets/QScrollBar",
    "QSemaphore":                "QtCore/QSemaphore",
    "QSessionManager":           "QtGui/QSessionManager",
    "QSet":                      "QtCore/QSet",
    "QSettings":                 "QtCore/QSettings",
    "QSize":                     "QtCore/QSize",
    "QStack":                    "QtCore/QStack",
    "QStandardPaths":            "QtCore/QStandardPaths",
    "QString":                   "QtCore/QString",
    "QStringList":               "QtCore/QStringList",
    "QStyleFactory":             "QtWidgets/QStyleFactory",
    "QStyleHints":               "QtGui/QStyleHints",
    "QSvgRenderer":              "QtSvg/QSvgRenderer",
    "QSysInfo":                  "QtCore/QSysInfo",
    "QSystemTrayIcon":           "QtWidgets/QSystemTrayIcon",
    "QTcpSocket":                "QtNetwork/QTcpSocket",
    "QTextBlock":                "QtGui/QTextBlock",
    "QTextDocument":             "QtGui/QTextDocument",
    "QTextDocumentFragment":     "QtGui/QTextDocumentFragment",
    "QTextEdit":                 "QtWidgets/QTextEdit",
    "QTextStream":               "QtCore/QTextStream",
    "QThread":                   "QtCore/QThread",
    "QTimer":                    "QtCore/QTimer",
    "QTimeZone":                 "QtCore/QTimeZone",
    "QTranslator":               "QtCore/QTranslator",
    "QUrl":                      "QtCore/QUrl",
    "QVector":                   "QtCore/QVector",
    "QVersionNumber":            "QtCore/QVersionNumber",
    "QWheelEvent":               "QtGui/QWheelEvent",
    "QWidget":                   "QtWidgets/QWidget",
    "QWindow":                   "QtGui/QWindow",
}
_QT_SHORT_RE = re.compile(
    r"#include <(?P<name>" + "|".join(re.escape(k) for k in _QT_LONG_FORM)
    + r")>"
)


def rewrite_generated_paths(iwyu_text: str) -> str:
    return _GEN_INCLUDE_RE.sub(
        lambda m: f'#include "{m.group("prefix")}{m.group("rest")}"',
        iwyu_text,
    )


_QT_REMOVE_LONG_RE = re.compile(
    r"^- #include <(?P<path>[^>]+)>"
)


def rewrite_qt_long_form(iwyu_text: str) -> str:
    """Rewrite add `<QClass>` to `<QtModule/QClass>` and drop the paired
    `- #include <QtModule/QClass>` so a path-rename does not turn into a
    deletion under --no-adds. Genuine removals in unpaired blocks survive."""
    long_targets = set(_QT_LONG_FORM.values())
    blocks: list[str] = []
    buf: list[str] = []
    for raw in iwyu_text.splitlines(keepends=True):
        buf.append(raw)
        if raw.rstrip("\n") == "---":
            blocks.append("".join(buf))
            buf = []
    if buf:
        blocks.append("".join(buf))

    out: list[str] = []
    for block in blocks:
        rewritten_block, rewritten_paths = _rewrite_block_adds(block)
        if not rewritten_paths:
            out.append(rewritten_block)
            continue
        out.append(_drop_paired_removes(
            rewritten_block, rewritten_paths & long_targets,
        ))
    return "".join(out)


def _rewrite_block_adds(block: str) -> tuple[str, set[str]]:
    rewritten_paths: set[str] = set()
    in_add = False
    out_lines: list[str] = []
    for line in block.splitlines(keepends=True):
        stripped = line.rstrip("\n")
        if stripped.endswith("should add these lines:"):
            in_add = True
        elif (stripped.endswith("should remove these lines:")
                or stripped.startswith("The full include-list for ")):
            in_add = False
        if in_add:
            m = _QT_SHORT_RE.match(stripped)
            if m:
                long = _QT_LONG_FORM[m.group("name")]
                rewritten_paths.add(long)
                out_lines.append(line.replace(f"<{m.group('name')}>",
                                              f"<{long}>", 1))
                continue
        out_lines.append(line)
    return "".join(out_lines), rewritten_paths


def _drop_paired_removes(block: str, drop_paths: set[str]) -> str:
    if not drop_paths:
        return block
    in_remove = False
    out_lines: list[str] = []
    for line in block.splitlines(keepends=True):
        stripped = line.rstrip("\n")
        if stripped.endswith("should remove these lines:"):
            in_remove = True
            out_lines.append(line)
            continue
        if (stripped.endswith("should add these lines:")
                or stripped.startswith("The full include-list for ")):
            in_remove = False
            out_lines.append(line)
            continue
        if in_remove:
            m = _QT_REMOVE_LONG_RE.match(stripped)
            if m and m.group("path") in drop_paths:
                continue
        out_lines.append(line)
    return "".join(out_lines)


_BLOCK_HEADER = re.compile(
    r"^(?P<path>.+?) (should add these lines|should remove these lines|"
    r"\- correct includes for .+|has correct #includes/fwd-decls\)|"
    r"correct #includes/fwd-decls)"
)
_FULL_LIST_HEADER = re.compile(r"^The full include-list for (?P<path>.+):$")
_SEPARATOR = re.compile(r"^---\s*$")


def filter_iwyu_output(
    iwyu_text: str,
    iwyu_filter: IwyuFilter,
    drop_adds: bool = False,
) -> tuple[str, dict[str, int]]:
    """Drop blocks for out-of-scope/submodule/platform paths. Returns
    the filtered text and a `{reason: count}` summary. Per-platform
    misreads of `#ifdef`-dispatched includes are pruned upstream by
    iwyu_intersect.py — every native platform sees its own block as
    needed, so the intersection drops the false-positive remove."""
    blocks: list[list[str]] = []
    current: list[str] = []
    for line in iwyu_text.splitlines():
        current.append(line)
        if _SEPARATOR.match(line):
            blocks.append(current)
            current = []
    if current:
        blocks.append(current)

    accepted: list[str] = []
    counts: dict[str, int] = {}
    for block in blocks:
        path = _block_subject_path(block)
        if path is None:
            accepted.extend(block)
            continue
        ok, why = iwyu_filter.accepts(path)
        if not ok:
            counts[why] = counts.get(why, 0) + 1
            continue
        kept = _drop_add_section(block) if drop_adds else list(block)
        accepted.extend(kept)
    return "\n".join(accepted) + ("\n" if accepted else ""), counts


def _drop_add_section(block: list[str]) -> list[str]:
    out: list[str] = []
    in_add = False
    for line in block:
        stripped = line.rstrip()
        if stripped.endswith("should add these lines:"):
            out.append(line)
            out.append("")
            in_add = True
            continue
        if (stripped.endswith("should remove these lines:")
                or stripped.startswith("The full include-list for ")
                or _SEPARATOR.match(line)):
            in_add = False
        if not in_add:
            out.append(line)
    return out


def _block_subject_path(block: list[str]) -> str | None:
    for line in block:
        m = _FULL_LIST_HEADER.match(line)
        if m:
            return m.group("path").strip()
        m = _BLOCK_HEADER.match(line)
        if m:
            return m.group("path").strip()
    return None


def _cli() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--repo-root", default=".", type=Path,
                   help="Repository root (default: cwd).")
    p.add_argument("--input", default="-", type=str,
                   help="IWYU output to filter, or '-' for stdin.")
    p.add_argument("--output", default="-", type=str,
                   help="Where to write filtered output, or '-' for stdout.")
    p.add_argument("--skip-platform", action="append", default=[],
                   choices=sorted(_PLATFORM_PATTERNS.keys()),
                   help="Drop blocks whose file matches the platform "
                        "patterns. May be repeated.")
    p.add_argument("--only-under", default="Telegram/SourceFiles",
                   help="Only keep files under this repo-relative path. "
                        "Pass an empty string to disable.")
    p.add_argument("--ignore", action="append", default=[],
                   help="Extra glob (relative to repo root) to drop.")
    p.add_argument("--no-adds", action="store_true",
                   help="Cleanup-only: strip every 'should add' section.")
    args = p.parse_args()

    iwyu_filter = IwyuFilter(
        repo_root=args.repo_root,
        skip_platforms=args.skip_platform,
        only_under=args.only_under,
        extra_ignore_globs=args.ignore,
    )

    text = sys.stdin.read() if args.input == "-" else Path(args.input).read_text(encoding="utf-8")
    filtered, counts = filter_iwyu_output(text, iwyu_filter,
                                           drop_adds=args.no_adds)
    if args.output == "-":
        sys.stdout.write(filtered)
    else:
        Path(args.output).write_text(filtered, encoding="utf-8")

    if counts:
        sys.stderr.write("Filtered out:\n")
        for reason, n in sorted(counts.items()):
            sys.stderr.write(f"  {n:5d}  {reason}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli())
