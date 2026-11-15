#!/usr/bin/env python3
"""Package a Release app. Requires dmgbuild==1.6.7 (macOS only)."""
import argparse
from pathlib import Path

import dmgbuild

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("app", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--variant", choices=("dark", "light"), default="dark")
args = parser.parse_args()
app = args.app.resolve()
if not (app / "Contents/Info.plist").is_file():
    parser.error("app must be a built macOS application bundle")
output = args.output.resolve()
if output.exists():
    parser.error("output already exists; choose a new filename")
output.parent.mkdir(parents=True, exist_ok=True)
background = "background-light.png" if args.variant == "light" else "background.png"
volume = "mokaGram Light Installer" if args.variant == "light" else "mokaGram Installer"
dmgbuild.build_dmg(str(output), volume, settings={
    "format": "UDZO",
    "files": [(str(app), "mokaGram.app")],
    "symlinks": {"Applications": "/Applications"},
    "background": str(Path(__file__).resolve().parent / "dmg" / background),
    "window_rect": ((160, 160), (800, 500)),
    "icon_locations": {"mokaGram.app": (220, 265), "Applications": (580, 265)},
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
