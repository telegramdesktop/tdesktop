#!/usr/bin/env python3
"""Extract and syntax-check inline JavaScript in the local admin pages."""
from html.parser import HTMLParser
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
PAGES = ("index.html", "account.html")


class ScriptExtractor(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.scripts = []
        self.current = None

    def handle_starttag(self, tag, attrs):
        if tag.lower() == "script":
            attributes = dict(attrs)
            self.current = [] if "src" not in attributes else None

    def handle_endtag(self, tag):
        if tag.lower() == "script" and self.current is not None:
            self.scripts.append("".join(self.current))
            self.current = None

    def handle_data(self, data):
        if self.current is not None:
            self.current.append(data)


def main():
    with tempfile.TemporaryDirectory(prefix="local-admin-js-") as temp:
        for page in PAGES:
            parser = ScriptExtractor()
            parser.feed((ROOT / page).read_text(encoding="utf-8"))
            parser.close()
            if not parser.scripts:
                raise SystemExit(f"{page}: no inline JavaScript found")
            for index, source in enumerate(parser.scripts, 1):
                target = Path(temp) / f"{page.replace('.html', '')}-{index}.js"
                target.write_text(source, encoding="utf-8")
                subprocess.run(["node", "--check", str(target)], check=True)
            print(f"{page}: {len(parser.scripts)} inline script(s) passed")


if __name__ == "__main__":
    main()
