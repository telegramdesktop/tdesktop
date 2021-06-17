#!/usr/bin/env python3

import re
import datetime
from xml.etree import ElementTree as ET
import argparse

def parse_changelog(changelog_path):
    version_re = re.compile(r'([\d.-]+)\s+(\w+)?\s*\((\d{2}.\d{2}\.\d{2})\)')
    entry_re = re.compile(r'-\s(.*)')

    with open(changelog_path, "r") as f:
        changelog_lines = f.read().splitlines()

    releases = []
    for l in changelog_lines:
        version_match = version_re.match(l)
        entry_match = entry_re.match(l)
        if version_match is not None:
            version, prerelease, date = version_match.groups()
            release = (version,
                       prerelease,
                       datetime.datetime.strptime(date, '%d.%m.%y').date(),
                       [])
            releases.append(release)
        elif entry_match is not None:
            release[3].append(entry_match.group(1))

    return releases

def get_release_xml(version, prerelease, date, changes):
    release = ET.Element("release")
    if prerelease is None:
        ver_str = version
    else:
        ver_str = f"{version}~{prerelease}"
    release.set("version", ver_str)
    release.set("date", date.isoformat())
    description = ET.SubElement(release, "description")
    changelist = ET.SubElement(description, "ul")
    for c in changes:
        change = ET.SubElement(changelist, "li")
        change.text = c
    return release

def get_changelog_xml(changelog, max_items=None):
    releases = ET.Element("releases")
    if max_items is not None:
        changelog = changelog[:max_items]
    for rel in changelog:
        release = get_release_xml(*rel)
        releases.append(release)
    return releases

def update_appdata(appdata_path, changelog, max_items=None):
    appdata = ET.parse(appdata_path)
    appdata.getroot().append(
        get_changelog_xml(changelog, max_items)
    )
    appdata.write(appdata_path, encoding="utf-8", xml_declaration=True)

def main():
    ap = argparse.ArgumentParser("Parse Telegram changelog")
    ap.add_argument("-c", "--changelog-path", default="changelog.txt")
    ap.add_argument("-a", "--appdata-path", default="lib/xdg/telegramdesktop.appdata.xml")
    ap.add_argument("-n", "--num-releases", type=int, default=None)
    args = ap.parse_args()
    update_appdata(args.appdata_path,
                   parse_changelog(args.changelog_path),
                   max_items=args.num_releases)

if __name__ == "__main__":
    main()
