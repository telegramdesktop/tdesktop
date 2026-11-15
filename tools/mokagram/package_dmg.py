#!/usr/bin/env python3
"""Package a Release app. Requires dmgbuild==1.6.7 (macOS only)."""
import argparse
from pathlib import Path

import dmgbuild

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("app", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
app = args.app.resolve()
if not (app / "Contents/Info.plist").is_file():
    parser.error("app must be a built macOS application bundle")
output = args.output.resolve()
if output.exists():
    parser.error("output already exists; choose a new filename")
output.parent.mkdir(parents=True, exist_ok=True)
dmgbuild.build_dmg(str(output), "mokaGram", settings={
    "format": "UDZO",
    "files": [(str(app), "mokaGram.app")],
    "symlinks": {"Applications": "/Applications"},
    "background": str(Path(__file__).resolve().parent / "dmg/background.png"),
    "window_rect": ((160, 160), (720, 440)),
    "icon_locations": {"mokaGram.app": (200, 242), "Applications": (520, 242)},
    "icon_size": 96,
    "text_size": 14,
    "hide_extension": ["mokaGram.app"],
    "default_view": "icon-view",
    "include_icon_view_settings": True,
    "include_list_view_settings": False,
    "show_status_bar": False,
    "show_tab_view": False,
    "show_toolbar": False,
    "show_pathbar": False,
    "show_sidebar": False,
})
print(output)
