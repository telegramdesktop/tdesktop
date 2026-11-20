# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

import argparse
import os
import pathlib
import plistlib
import stat
import subprocess
import sys

PROJECT = pathlib.Path(__file__).resolve().parents[1]
sys.path.append(str(PROJECT / 'cmake'))
import widget_helper_identity

HELPER = pathlib.Path('Contents/Helpers/TelegramWidgetHelper')
SERVICE = pathlib.Path('Contents/XPCServices/TelegramWidgetHelper.xpc')
HELPER_ENTITLEMENTS = {
    HELPER: PROJECT / 'Telegram/TelegramWidgetHelperDirect.entitlements',
    SERVICE: PROJECT / 'Telegram/TelegramWidgetHelper.entitlements',
}


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
    present = widget_helpers(bundle)
    for helper in present:
        check_layout(bundle, helper)
    certified = present if identity != '-' else []
    for helper in certified:
        check_helper(bundle, helper, widget_helper_identity.check_staged)
    for path in nested_code(bundle):
        codesign(identity, path, entitlements, timestamp, runtime, keychain)
    for helper in certified:
        codesign(identity, bundle / helper, HELPER_ENTITLEMENTS[helper], timestamp, True, keychain,
            extra=['--preserve-metadata=identifier'])
    check_helpers(bundle)
    if nested_only:
        return
    codesign(identity, bundle, entitlements, timestamp, runtime, keychain)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)


def widget_helpers(bundle):
    present = [helper for helper in HELPER_ENTITLEMENTS if (bundle / helper).exists()]
    if not present:
        raise RuntimeError(str(bundle) + ' has no widget helper')
    return present


def check_entry(path, directory):
    mode = path.lstat().st_mode
    if stat.S_ISLNK(mode):
        raise RuntimeError(str(path) + ' is a link')
    if not (stat.S_ISDIR(mode) if directory else stat.S_ISREG(mode)):
        raise RuntimeError(str(path) + ' is not a ' + ('directory' if directory else 'regular file'))


def check_layout(bundle, helper):
    for parent in reversed(helper.parents):
        if parent.parts:
            check_entry(bundle / parent, True)
    if helper == HELPER:
        check_entry(bundle / helper, False)
        return
    contents = bundle / helper / 'Contents'
    info = contents / 'Info.plist'
    for path, directory in ((bundle / helper, True), (contents, True), (info, False)):
        check_entry(path, directory)
    executable = plistlib.loads(info.read_bytes()).get('CFBundleExecutable')
    if not isinstance(executable, str) or executable in ('', '.', '..') or '/' in executable:
        raise RuntimeError(str(info) + ' has no valid CFBundleExecutable')
    for path, directory in (
            (contents / 'MacOS', True),
            (contents / 'MacOS' / executable, False),
            (contents / '_CodeSignature', True),
            (contents / '_CodeSignature/CodeResources', False)):
        check_entry(path, directory)


def check_helper(bundle, helper, check):
    check_layout(bundle, helper)
    path = bundle / helper
    root = bundle.resolve()
    for item in [path, *path.rglob('*')]:
        if root not in item.resolve().parents:
            raise RuntimeError(str(item) + ' resolves outside ' + str(bundle))
    try:
        return check(path, HELPER_ENTITLEMENTS[helper])
    except RuntimeError as error:
        raise RuntimeError(str(path) + ': ' + str(error)) from None


def check_helpers(bundle):
    for helper in widget_helpers(bundle):
        found = check_helper(bundle, helper, widget_helper_identity.check)
        print('Widget helper checked: ' + str(helper) + ' (' + ', '.join(found) + ')', flush=True)


def main():
    parser = argparse.ArgumentParser(description='Sign a macOS app bundle and the code nested in it.')
    parser.add_argument('bundle', type=pathlib.Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--identity')
    source.add_argument('--xcode', action='store_true')
    source.add_argument('--check', action='store_true')
    source.add_argument('--check-layout', action='store_true')
    parser.add_argument('--entitlements', type=pathlib.Path)
    parser.add_argument('--timestamp', action='store_true')
    parser.add_argument('--runtime', action='store_true')
    parser.add_argument('--keychain', type=pathlib.Path)
    args = parser.parse_args()
    if args.check_layout:
        for helper in widget_helpers(args.bundle):
            check_layout(args.bundle, helper)
        return
    if args.check:
        check_helpers(args.bundle)
        return
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
