#!/usr/bin/env python3
"""Package a Release app. Requires dmgbuild==1.6.7 (macOS only)."""
import argparse
import subprocess
import tempfile
from pathlib import Path

import dmgbuild

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("app", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--variant", choices=("dark", "light", "minimal"), default="dark")
args = parser.parse_args()
app = args.app.resolve()
if not (app / "Contents/Info.plist").is_file():
    parser.error("app must be a built macOS application bundle")
output = args.output.resolve()
if output.exists():
    parser.error("output already exists; choose a new filename")
output.parent.mkdir(parents=True, exist_ok=True)
background_name = {
    "dark": "background", "light": "background-light", "minimal": "background-minimal",
}[args.variant]
background_dir = Path(__file__).resolve().parent / "dmg"
volume = {
    "dark": "mokaGram Installer", "light": "mokaGram Light Installer",
    "minimal": "mokaGram Minimal Installer",
}[args.variant]
# A large solid canvas keeps the background when Finder is expanded. Compress
# the multi-resolution TIFF before copying it into the image to keep it small.
with tempfile.TemporaryDirectory(prefix="mokagram-dmg-") as temporary:
    combined = Path(temporary) / "combined.tiff"
    background = Path(temporary) / "background.tiff"
    subprocess.run(["/usr/bin/tiffutil", "-cathidpicheck",
                    str(background_dir / f"{background_name}.png"),
                    str(background_dir / f"{background_name}@2x.png"),
                    "-out", str(combined)], check=True)
    subprocess.run(["/usr/bin/tiffutil", "-lzw", str(combined),
                    "-out", str(background)], check=True)
    dmgbuild.build_dmg(str(output), volume, lookForHiDPI=False, settings={
        "format": "UDZO",
        "files": [(str(app), "mokaGram.app")],
        "symlinks": {"Applications": "/Applications"},
        "background": str(background),
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
