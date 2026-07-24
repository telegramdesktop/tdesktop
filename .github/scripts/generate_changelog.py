#!/usr/bin/env python3
"""Convert changelog.txt to a static HTML page for GitHub Pages."""

import re
import shutil
import sys
import html
from pathlib import Path

MONTHS = [
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
]

VERSION_RE = re.compile(
    r"^(\d+\.\d+(?:\.\d+)?)\s*"        # version number
    r"(?:(alpha|beta|dev|stable)\s*)?"   # optional tag
    r"\((\d{2})\.(\d{2})\.(\d{2,4})\)$" # date (DD.MM.YY or DD.MM.YYYY)
)


def parse_date(day: str, month: str, year: str) -> tuple[str, str, str]:
    """Return (sort_key, raw_display, full_display) from DD, MM, YY strings."""
    y = int(year)
    if y < 100:
        y += 2000
    m = int(month)
    d = int(day)
    sort_key = f"{y:04d}-{m:02d}-{d:02d}"
    raw_display = f"{day}.{month}.{year}"
    full_display = f"{d} {MONTHS[m]} {y}"
    return sort_key, raw_display, full_display


def parse_changelog(text: str) -> list[dict]:
    entries = []
    current = None

    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        m = VERSION_RE.match(line)
        if m:
            if current:
                entries.append(current)
            version, tag, day, month, year = m.groups()
            sort_key, raw_date, full_date = parse_date(day, month, year)
            current = {
                "version": version,
                "tag": tag or "",
                "date": raw_date,
                "full_date": full_date,
                "sort_key": sort_key,
                "lines": [],
            }
        elif current is not None:
            # Skip blank lines at the start
            if not line and not current["lines"]:
                continue
            # Skip stray artifact lines
            if line.strip() in ("),", "),"):
                continue
            current["lines"].append(line)

    if current:
        entries.append(current)

    # Trim trailing blank lines from each entry
    for entry in entries:
        while entry["lines"] and not entry["lines"][-1]:
            entry["lines"].pop()

    return entries


def render_entry(entry: dict) -> str:
    version = html.escape(entry["version"])
    tag = entry["tag"]
    date = html.escape(entry["date"])
    anchor = f"v{version}"

    tag_html = ""
    if tag and tag not in ("stable",):
        tag_html = f' {html.escape(tag)}'

    parts = [
        f'<article class="entry" id="{anchor}">',
        f'  <h2><a class="anchor" href="#{anchor}"></a>'
        f'{version}{tag_html}'
        f' <time>{date}</time></h2>',
    ]

    in_list = False
    for line in entry["lines"]:
        stripped = line.lstrip()
        if stripped.startswith("- ") or stripped.startswith("\u2014 "):
            # Bullet point (- or em dash)
            if not in_list:
                parts.append("  <ul>")
                in_list = True
            bullet_text = stripped[2:]
            parts.append(f"    <li>{html.escape(bullet_text)}</li>")
        else:
            if in_list:
                parts.append("  </ul>")
                in_list = False
            if stripped:
                parts.append(f"  <p>{html.escape(stripped)}</p>")

    if in_list:
        parts.append("  </ul>")

    parts.append("</article>")
    return "\n".join(parts)


def build_html(entries: list[dict]) -> str:
    count = len(entries)
    first_date = entries[-1]["full_date"] if entries else ""
    latest_version = entries[0]["version"] if entries else ""

    entries_html = "\n\n".join(render_entry(e) for e in entries)

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Version history</title>
<link rel="icon" type="image/png" sizes="32x32" href="icon32.png">
<link rel="icon" type="image/png" sizes="16x16" href="icon16.png">
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{
  font: 12px / 18px "Lucida Grande", "Lucida Sans Unicode", Arial,
    Helvetica, Verdana, sans-serif;
  background: #fff;
  color: #000;
}}
header {{
  background: #1d98dc;
  color: #fff;
  padding: 2rem 1.5rem;
  text-align: center;
}}
header h1 {{ font-size: 18px; font-weight: 700; }}
header p {{ opacity: .85; margin-top: 4px; font-size: 12px; }}
.container {{
  max-width: 600px;
  margin: 0 auto;
  padding: 20px 15px;
}}
.search-box {{
  position: sticky;
  top: 0;
  z-index: 10;
  background: #fff;
  padding: 8px 0 12px;
}}
.search-box input {{
  width: 100%;
  padding: 6px 10px;
  font: 12px / 18px "Lucida Grande", "Lucida Sans Unicode", Arial,
    Helvetica, Verdana, sans-serif;
  border: 1px solid #ccc;
  border-radius: 4px;
  background: #fff;
  color: #000;
  outline: none;
}}
.search-box input:focus {{ border-color: #1d98dc; }}
.entry {{
  padding: 14px 0 4px;
  scroll-margin-top: 48px;
}}
.entry h2 {{
  font-size: 16px;
  font-weight: 700;
  line-height: 22px;
  margin-bottom: 6px;
  position: relative;
}}
.entry h2 .anchor {{
  position: absolute;
  left: -24px;
  top: 0;
  width: 24px;
  height: 22px;
  display: block;
  opacity: 0;
  transition: opacity .15s;
  background: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='18' height='18' viewBox='0 0 16 16'%3E%3Cpath fill='%23168acd' d='M7.775 3.275a.75.75 0 0 0 1.06 1.06l1.25-1.25a2 2 0 1 1 2.83 2.83l-2.5 2.5a2 2 0 0 1-2.83 0 .75.75 0 0 0-1.06 1.06 3.5 3.5 0 0 0 4.95 0l2.5-2.5a3.5 3.5 0 0 0-4.95-4.95l-1.25 1.25zm-4.69 9.64a2 2 0 0 1 0-2.83l2.5-2.5a2 2 0 0 1 2.83 0 .75.75 0 0 0 1.06-1.06 3.5 3.5 0 0 0-4.95 0l-2.5 2.5a3.5 3.5 0 0 0 4.95 4.95l1.25-1.25a.75.75 0 0 0-1.06-1.06l-1.25 1.25a2 2 0 0 1-2.83 0z'/%3E%3C/svg%3E") 0 center / 18px no-repeat;
  cursor: pointer;
}}
.entry h2:hover .anchor {{ opacity: .6; }}
.entry h2 .anchor:hover {{ opacity: 1; }}
.entry h2 time {{
  font-size: 12px;
  font-weight: 400;
  color: #999;
  margin-left: 6px;
}}
.entry ul {{
  margin: 0 0 4px 8px;
  padding: 0;
  list-style: none;
}}
.entry li {{
  padding: 2px 0 2px 16px;
  position: relative;
  color: #333;
}}
.entry li::before {{
  content: "";
  position: absolute;
  left: 0;
  top: 9px;
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #009be1;
}}
.entry p {{
  margin: 4px 0;
  color: #555;
  font-style: italic;
}}
.hidden {{ display: none; }}
footer {{
  text-align: center;
  padding: 24px 15px;
  font-size: 11px;
  color: #999;
}}
footer a {{ color: #168acd; text-decoration: none; }}
footer a:hover {{ text-decoration: underline; }}
</style>
</head>
<body>

<header>
  <h1>Version history</h1>
  <p>{count} releases since {first_date} &middot; latest: {latest_version}</p>
</header>

<div class="container">
  <div class="search-box">
    <input type="text" id="search" placeholder="Search versions and changes\u2026"
           autocomplete="off" spellcheck="false">
  </div>

  <div id="entries">
{entries_html}
  </div>
</div>

<footer>
  Auto-generated from
  <a href="https://github.com/telegramdesktop/tdesktop/blob/dev/changelog.txt">changelog.txt</a>.
  Source code is published under
  <a href="https://github.com/telegramdesktop/tdesktop">GPL v3</a>.
</footer>

<script>
(function() {{
  var input = document.getElementById('search');
  var entries = document.querySelectorAll('.entry');
  var timer;
  input.addEventListener('input', function() {{
    clearTimeout(timer);
    timer = setTimeout(function() {{
      var q = input.value.toLowerCase().trim();
      entries.forEach(function(el) {{
        if (!q) {{
          el.classList.remove('hidden');
        }} else {{
          el.classList.toggle('hidden', el.textContent.toLowerCase().indexOf(q) === -1);
        }}
      }});
    }}, 150);
  }});

  // Anchor links: copy URL on click
  document.addEventListener('click', function(e) {{
    var anchor = e.target.closest('.anchor');
    if (!anchor) return;
    e.preventDefault();
    var url = location.origin + location.pathname + anchor.getAttribute('href');
    history.replaceState(null, '', anchor.getAttribute('href'));
    if (navigator.clipboard) {{
      navigator.clipboard.writeText(url);
    }}
  }});
}})();
</script>

</body>
</html>"""


def main():
    repo = Path(__file__).resolve().parent.parent.parent
    src = repo / "changelog.txt"
    if len(sys.argv) > 1:
        src = Path(sys.argv[1])

    out = repo / "docs" / "changelog" / "index.html"
    if len(sys.argv) > 2:
        out = Path(sys.argv[2])

    text = src.read_text(encoding="utf-8")
    entries = parse_changelog(text)
    html_content = build_html(entries)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html_content, encoding="utf-8")

    # Copy favicon files from resources
    icons_src = repo / "Telegram" / "Resources" / "art"
    for name in ("icon16.png", "icon32.png"):
        icon = icons_src / name
        if icon.exists():
            shutil.copy2(icon, out.parent / name)

    print(f"Generated {out} ({len(entries)} entries, {out.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
