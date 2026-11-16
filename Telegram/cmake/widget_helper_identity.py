'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''

import argparse
import hashlib
import pathlib
import plistlib
import re
import struct
import subprocess
import sys
import xml.parsers.expat

PE_ARCHITECTURES = {
    0x014c: ('x86', 0x10b),
    0x8664: ('x64', 0x20b),
    0xaa64: ('arm64', 0x20b),
}
LOAD_LIBRARY_SEARCH_SYSTEM32 = 0x00000800
SHA256_SIZE = 32
ELF_MACHINES = {62: 'x86_64', 183: 'aarch64'}


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


def expected_entitlements(path):
    if path is None:
        return {}
    try:
        parsed = plistlib.loads(path.read_bytes())
    except (ValueError, xml.parsers.expat.ExpatError) as error:
        raise RuntimeError(
            path.name + ' has unreadable entitlements: ' + str(error))
    if not isinstance(parsed, dict):
        raise RuntimeError(path.name + ' has unreadable entitlements')
    return parsed


def find(pattern, text, error):
    found = re.search(pattern, text, re.MULTILINE)
    if not found:
        raise RuntimeError(error)
    return found.group(1)


def cdhashes(helper, identifier, entitlements_file):
    expected = expected_entitlements(entitlements_file)
    architectures = find(
        r'^Format=(?:bundle with )?Mach-O (?:thin|universal) \(([^)]*)\)$',
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
        if keys != expected:
            listed = ', '.join(sorted(keys))
            if not expected:
                raise RuntimeError(name + ' has entitlements: ' + listed)
            raise RuntimeError(
                name + "'s entitlements differ from "
                + entitlements_file.name + ': ' + listed)
        digest = find(
            r'^CDHash=([0-9a-f]{40})$',
            info,
            name + ' has no CDHash')
        digests.append((arch, digest))
    return digests


def unpack(image, offset, layout, what):
    if offset + struct.calcsize(layout) > len(image):
        raise RuntimeError('the helper is truncated in its ' + what)
    return struct.unpack_from(layout, image, offset)


def pe_architecture(image):
    if unpack(image, 0, '<2s', 'DOS header')[0] != b'MZ':
        raise RuntimeError('the helper is not a PE executable')
    pe, = unpack(image, 0x3c, '<I', 'DOS header')
    if unpack(image, pe, '<4s', 'PE signature')[0] != b'PE\0\0':
        raise RuntimeError('the helper is not a PE executable')
    machine, sections, optional_size = unpack(
        image,
        pe + 4,
        '<HH12xH',
        'file header')
    optional = pe + 24
    magic, = unpack(image, optional, '<H', 'optional header')
    arch, expected = PE_ARCHITECTURES.get(machine, (None, None))
    if arch is None or magic != expected:
        raise RuntimeError(
            'the helper has unsupported machine 0x%04x'
            ' with optional header magic 0x%x'
            % (machine, magic))
    directories = optional + (96 if magic == 0x10b else 112)
    count, = unpack(image, directories - 4, '<I', 'optional header')
    if count <= 4:
        raise RuntimeError('the helper has no certificate table entry')
    if (directories + 8 * count > optional + optional_size
            or optional + optional_size > len(image)):
        raise RuntimeError('the helper has an incomplete optional header')
    end, = unpack(image, optional + 60, '<I', 'optional header')
    for index in range(sections):
        size, pointer = unpack(
            image,
            optional + optional_size + 40 * index + 16,
            '<II',
            'section table')
        if size:
            end = max(end, pointer + size)
    table, table_size = unpack(
        image,
        directories + 32,
        '<II',
        'certificate table entry')
    length = len(image)
    if not table and not table_size:
        unsigned = length
    elif not table or not table_size:
        raise RuntimeError('the helper has a partial certificate table entry')
    elif table + table_size > length:
        raise RuntimeError('the helper certificate table is truncated')
    elif table + table_size < length:
        raise RuntimeError('the helper has data after its certificate table')
    elif table_size % 8:
        raise RuntimeError(
            'the helper certificate table is not eight-byte aligned')
    else:
        unsigned = table
    if unsigned % 8:
        raise RuntimeError(
            'the helper is ' + str(unsigned)
            + ' bytes long without its certificate table,'
            + ' not a multiple of eight')
    if end > length:
        raise RuntimeError('the helper sections are truncated')
    if end > unsigned:
        raise RuntimeError('the helper certificate table overlaps its image')
    return arch


def resolve(library, name, result, *arguments):
    try:
        function = getattr(library, name)
    except AttributeError:
        raise RuntimeError(name + ' is not available') from None
    function.restype = result
    function.argtypes = arguments
    return function


def authenticode(helper):
    import ctypes
    import ctypes.wintypes as wintypes
    import msvcrt

    wintrust = ctypes.WinDLL(
        'wintrust.dll',
        use_last_error=True,
        winmode=LOAD_LIBRARY_SEARCH_SYSTEM32)
    acquire = resolve(
        wintrust,
        'CryptCATAdminAcquireContext2',
        wintypes.BOOL,
        wintypes.PHANDLE,
        wintypes.LPVOID,
        wintypes.LPCWSTR,
        wintypes.LPVOID,
        wintypes.DWORD)
    calculate = resolve(
        wintrust,
        'CryptCATAdminCalcHashFromFileHandle2',
        wintypes.BOOL,
        wintypes.HANDLE,
        wintypes.HANDLE,
        wintypes.PDWORD,
        wintypes.PBYTE,
        wintypes.DWORD)
    release = resolve(
        wintrust,
        'CryptCATAdminReleaseContext',
        wintypes.BOOL,
        wintypes.HANDLE,
        wintypes.DWORD)
    with open(helper, 'rb', buffering=0) as file:
        arch = pe_architecture(file.read())
        file.seek(0)
        admin = wintypes.HANDLE()
        if not acquire(ctypes.byref(admin), None, 'SHA256', None, 0):
            raise RuntimeError(
                'CryptCATAdminAcquireContext2 failed: '
                + ctypes.FormatError(ctypes.get_last_error()))
        try:
            size = wintypes.DWORD(SHA256_SIZE)
            digest = (wintypes.BYTE * SHA256_SIZE)()
            if not calculate(
                    admin,
                    msvcrt.get_osfhandle(file.fileno()),
                    ctypes.byref(size),
                    digest,
                    0):
                raise RuntimeError(
                    'CryptCATAdminCalcHashFromFileHandle2 failed: '
                    + ctypes.FormatError(ctypes.get_last_error()))
        finally:
            release(admin, 0)
    if size.value != SHA256_SIZE:
        raise RuntimeError(
            'CryptCATAdminCalcHashFromFileHandle2 gave '
            + str(size.value) + ' bytes instead of '
            + str(SHA256_SIZE))
    return [(arch, bytes(digest).hex())]


def sha256_digests(helper):
    data = helper.read_bytes()
    if len(data) < 20 or not data.startswith(b'\x7fELF'):
        raise RuntimeError('the helper is not an ELF executable')
    order = 'little' if data[5] == 1 else 'big'
    machine = int.from_bytes(data[18:20], order)
    return [(
        ELF_MACHINES.get(machine, 'elf-' + str(machine)),
        hashlib.sha256(data).hexdigest())]


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
    parser.add_argument(
        '--kind',
        choices=['cdhash', 'authenticode', 'sha256'],
        required=True)
    parser.add_argument('--helper', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--identifier')
    parser.add_argument('--entitlements', type=pathlib.Path)
    args = parser.parse_args()
    if args.kind == 'authenticode':
        if args.identifier is not None:
            parser.error('--identifier does not apply to --kind authenticode')
        digests = authenticode(args.helper)
    elif args.kind == 'sha256':
        digests = sha256_digests(args.helper)
    else:
        digests = cdhashes(args.helper, args.identifier, args.entitlements)
    write(args.output, header(digests, args.identifier))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        print('Widget helper identity failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
