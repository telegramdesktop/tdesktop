# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

import argparse
import os
import pathlib
import plistlib
import subprocess
import sys

HELPER = pathlib.Path('Contents/Helpers/TelegramWidgetHelper')
SERVICE = pathlib.Path('Contents/XPCServices/TelegramWidgetHelper.xpc')


def macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as file:
        return file.read(4) in (
            b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe',
            b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe',
            b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
            b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca',
        )


def skipped(relative, excluded):
    return relative in excluded or SERVICE in relative.parents or 'Resources' in relative.parts


def nested_code(bundle):
    info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    excluded = {pathlib.Path('Contents/MacOS') / info['CFBundleExecutable'], HELPER}
    paths = [path for path in bundle.rglob('*')
        if not skipped(path.relative_to(bundle), excluded) and macho(path)]
    return sorted(paths, key=lambda path: (-len(path.parts), path))


def codesign(identity, path, entitlements=None, timestamp=None, runtime=False, keychain=None, extra=()):
    command = ['codesign', '--force', '--sign', identity]
    if timestamp is not None:
        command.append('--timestamp' if timestamp else '--timestamp=none')
    if runtime:
        command.extend(['--options', 'runtime'])
    if keychain:
        command.extend(['--keychain', keychain])
    command.extend(extra)
    if entitlements:
        command.extend(['--entitlements', entitlements])
    command.append(path)
    subprocess.run([str(part) for part in command], check=True)


def sign(bundle, identity, entitlements=None, timestamp=None, runtime=False, keychain=None, nested_only=False):
    for path in nested_code(bundle):
        codesign(identity, path, entitlements, timestamp, runtime, keychain)
    if (bundle / HELPER).exists() and identity != '-':
        codesign(identity, bundle / HELPER, timestamp=timestamp, runtime=True, keychain=keychain,
            extra=['--preserve-metadata=identifier'])
    if (bundle / SERVICE).exists() and identity != '-':
        codesign(identity, bundle / SERVICE, timestamp=timestamp, runtime=True, keychain=keychain,
            extra=['--preserve-metadata=identifier,entitlements'])
    if nested_only:
        return
    codesign(identity, bundle, entitlements, timestamp, runtime, keychain)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)


def main():
    parser = argparse.ArgumentParser(description='Sign a macOS app bundle and the code nested in it.')
    parser.add_argument('bundle', type=pathlib.Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--identity')
    source.add_argument('--xcode', action='store_true')
    parser.add_argument('--entitlements', type=pathlib.Path)
    parser.add_argument('--timestamp', action='store_true')
    parser.add_argument('--runtime', action='store_true')
    parser.add_argument('--keychain', type=pathlib.Path)
    args = parser.parse_args()
    if not args.xcode:
        sign(args.bundle, args.identity, args.entitlements, True if args.timestamp else None,
            args.runtime, args.keychain)
        return
    if os.environ.get('CODE_SIGNING_ALLOWED') != 'YES':
        print('Code signing is not allowed, nested code is left as built.', flush=True)
        return
    identity = os.environ.get('EXPANDED_CODE_SIGN_IDENTITY')
    if not identity:
        raise RuntimeError('Xcode allows code signing but provides no EXPANDED_CODE_SIGN_IDENTITY.')
    sign(args.bundle, identity, args.entitlements, False, args.runtime, args.keychain, nested_only=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print('macOS signing failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
