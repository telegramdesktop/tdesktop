'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''

import argparse
import pathlib
import plistlib
import re
import subprocess
import sys
import xml.parsers.expat


def codesign(arguments):
    result = subprocess.run(
        ['codesign'] + arguments,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True)
    if result.returncode != 0:
        raise RuntimeError(
            'codesign ' + ' '.join(arguments) + ' failed: '
            + result.stderr.strip())
    return result


def display(helper, arch=None):
    arguments = ['--display', '--verbose=4']
    if arch:
        arguments += ['--arch', arch]
    return codesign(arguments + [str(helper)]).stderr


def entitlements(helper, arch):
    text = codesign([
        '--display',
        '--entitlements',
        '-',
        '--xml',
        '--arch',
        arch,
        str(helper),
    ]).stdout.strip()
    if not text:
        return {}
    try:
        parsed = plistlib.loads(text.encode())
    except (ValueError, xml.parsers.expat.ExpatError) as error:
        raise RuntimeError(
            'the ' + arch + ' slice has unreadable entitlements: '
            + str(error))
    if not isinstance(parsed, dict):
        raise RuntimeError('the ' + arch + ' slice has unreadable entitlements')
    return parsed


def find(pattern, text, error):
    found = re.search(pattern, text, re.MULTILINE)
    if not found:
        raise RuntimeError(error)
    return found.group(1)


def cdhashes(helper, identifier):
    architectures = find(
        r'^Format=Mach-O (?:thin|universal) \(([^)]*)\)$',
        display(helper),
        'the helper is not a Mach-O executable').split()
    if not architectures:
        raise RuntimeError('the helper has no architectures')
    digests = []
    for arch in architectures:
        name = 'the ' + arch + ' slice'
        info = display(helper, arch)
        if not re.search(r'^Signature=adhoc$', info, re.MULTILINE):
            raise RuntimeError(name + ' is not signed ad-hoc')
        flags = find(
            r'^CodeDirectory .* flags=0x[0-9a-f]+\(([^)]*)\)',
            info,
            name + ' has no code directory flags')
        if 'runtime' not in flags.split(','):
            raise RuntimeError(name + ' has no hardened runtime flag')
        if identifier is not None:
            find(
                '^Identifier=(' + re.escape(identifier) + ')$',
                info,
                name + ' has no identifier ' + identifier)
        keys = entitlements(helper, arch)
        if keys:
            raise RuntimeError(
                name + ' has entitlements: ' + ', '.join(sorted(keys)))
        digest = find(
            r'^CDHash=([0-9a-f]{40})$',
            info,
            name + ' has no CDHash')
        digests.append((arch, digest))
    return digests


def header(digests, identifier):
    lines = [
        '// This file was generated from the widget helper target,',
        '// see Telegram/cmake/widget_helper_identity.py.',
        '#pragma once',
        '',
        'namespace Platform::WidgetHelper::details {',
        '',
        'struct BuiltDigest {',
        '\tconst char *architecture = nullptr;',
        '\tconst char *digest = nullptr;',
        '};',
        '',
    ]
    if identifier is not None:
        lines += [
            'inline constexpr auto kIdentifier = "' + identifier + '";',
            '',
        ]
    lines.append('inline constexpr BuiltDigest kBuiltDigests[] = {')
    for arch, digest in digests:
        lines.append('\t{ "' + arch + '", "' + digest + '" },')
    lines += [
        '};',
        '',
        '} // namespace Platform::WidgetHelper::details',
        '',
    ]
    return '\n'.join(lines)


def write(path, content):
    if path.is_file() and path.read_text() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def main():
    parser = argparse.ArgumentParser(
        description='Record the identity of the built widget helper.')
    parser.add_argument('--kind', choices=['cdhash'], required=True)
    parser.add_argument('--helper', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--identifier')
    args = parser.parse_args()
    digests = cdhashes(args.helper, args.identifier)
    write(args.output, header(digests, args.identifier))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        print('Widget helper identity failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
