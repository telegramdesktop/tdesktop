import argparse
import os
import pathlib
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


def run(command, **kwargs):
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def output(command):
    return subprocess.check_output([str(part) for part in command], text=True).strip()


def toolchain_environment(toolchain):
    if not toolchain.is_dir():
        raise RuntimeError('Preserved Command Line Tools 26.6 not found: ' + str(toolchain))
    receipt_path = toolchain / 'installation-receipt.plist'
    if not receipt_path.is_file():
        raise RuntimeError('Command Line Tools 26.6 installation receipt not found: ' + str(receipt_path))
    receipt = plistlib.loads(receipt_path.read_bytes())
    if not receipt.get('pkg-version', '').startswith('26.6.'):
        raise RuntimeError('The release toolchain must be Command Line Tools 26.6.')
    sdk = toolchain / 'SDKs/MacOSX26.5.sdk'
    settings_path = sdk / 'SDKSettings.plist'
    if not settings_path.is_file():
        raise RuntimeError('Preserved macOS SDK 26.5 not found: ' + str(sdk))
    settings = plistlib.loads(settings_path.read_bytes())
    if settings.get('Version') != '26.5':
        raise RuntimeError('The release SDK must be macOS SDK 26.5: ' + str(sdk))
    if settings['SupportedTargets']['macosx']['MinimumDeploymentTarget'] != '10.13':
        raise RuntimeError('The release SDK must support deployment target 10.13.')
    for name in ('clang', 'clang++', 'swiftc', 'ar', 'ranlib', 'ld', 'otool', 'lipo', 'dsymutil', 'swift-stdlib-tool'):
        program = toolchain / 'usr/bin' / name
        if not program.is_file() or not os.access(program, os.X_OK):
            raise RuntimeError('Required Command Line Tools 26.6 executable not found: ' + str(program))
    compiler = output([toolchain / 'usr/bin/clang', '--version']).splitlines()[0]
    if 'clang-2100.1.1.101' not in compiler:
        raise RuntimeError('The preserved 26.6 compiler does not match its receipt.')
    output(['xcrun', '--find', 'actool'])
    environment = os.environ.copy()
    environment.pop('TOOLCHAINS', None)
    environment['PATH'] = str(toolchain / 'usr/bin') + os.pathsep + environment['PATH']
    environment['SDKROOT'] = str(sdk)
    environment['MACOSX_DEPLOYMENT_TARGET'] = '10.13'
    print('Using Command Line Tools 26.6, SDK 26.5, deployment target 10.13.', flush=True)
    return environment, sdk


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


def verify_dependencies(folder, libraries, toolchain, arch, target):
    pattern = re.escape(str(libraries)) + r'/[^\s|]+\.a(?=[\s|]|$)'
    archives = sorted(set(re.findall(pattern, (folder / 'build.ninja').read_text())))
    if not archives:
        raise RuntimeError('No prepared dependency archives found in the build plan.')
    if target == 'macstore':
        framework = libraries / 'breakpad/src/client/mac/build/Release/Breakpad.framework/Versions/A'
        archives.extend(str(framework / name) for name in (
            'Breakpad', 'Resources/breakpadUtilities.dylib'))
    maximum = (10, 13, 0) if arch == 'x86_64' else (11, 0, 0)
    versions = 0
    for archive in archives:
        process = subprocess.Popen([str(toolchain / 'usr/bin/otool'), '-l', '-arch', arch, archive],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        minimum_field = None
        failures = []
        for line in process.stdout:
            parts = line.split()
            if parts[:2] == ['cmd', 'LC_BUILD_VERSION']:
                minimum_field = 'minos'
            elif parts[:2] == ['cmd', 'LC_VERSION_MIN_MACOSX']:
                minimum_field = 'version'
            elif parts[:1] == ['Load']:
                minimum_field = None
            elif minimum_field and len(parts) == 2 and parts[0] in (minimum_field, 'sdk'):
                if parts[0] == 'sdk' and parts[1] == 'n/a':
                    continue
                value = tuple((list(map(int, parts[1].split('.'))) + [0, 0])[:3])
                if parts[0] == 'sdk':
                    if value > (26, 5, 0):
                        failures.append('SDK ' + parts[1])
                else:
                    versions += 1
                    if value > maximum:
                        failures.append('deployment target ' + parts[1])
        errors = process.stderr.read()
        if process.wait():
            raise RuntimeError('Could not inspect ' + archive + ': ' + errors)
        if failures:
            raise RuntimeError(archive + ' contains incompatible ' + ', '.join(sorted(set(failures))))
    if not versions:
        raise RuntimeError('Dependency archives contain no deployment-version metadata.')
    print('Verified ' + str(len(archives)) + ' dependency binaries for ' + arch
        + ' (' + str(versions) + ' deployment-version records).', flush=True)


def merge_binary(lipo, inputs, destination, temporary):
    slices = []
    for arch, path in inputs.items():
        architectures = output([lipo, '-archs', path]).split()
        if arch not in architectures:
            raise RuntimeError(str(path) + ' is missing ' + arch + '.')
        if len(architectures) == 1:
            slices.append(path)
        else:
            thin = temporary / (destination.name + '.' + arch)
            run([lipo, path, '-thin', arch, '-output', thin])
            slices.append(thin)
    run([lipo, '-create', *slices, '-output', destination])
    if set(output([lipo, '-archs', destination]).split()) != {'arm64', 'x86_64'}:
        raise RuntimeError('Universal binary verification failed: ' + str(destination))


def resource_content(path):
    data = bytearray(path.read_bytes())
    if path.suffix == '.rcc' and data[:4] == b'qres':
        version = int.from_bytes(data[4:8], 'big')
        if version in (2, 3):
            tree = int.from_bytes(data[8:12], 'big')
            if tree < 20 or tree > len(data) or (len(data) - tree) % 22:
                raise RuntimeError('Invalid Qt resource tree: ' + str(path))
            # Generated-file timestamps differ between architecture builds.
            for offset in range(tree, len(data), 22):
                data[offset + 14:offset + 22] = b'\0' * 8
    return data


def assemble(builds, destination, toolchain, target, configuration):
    binary_name = 'Telegram Lite' if target == 'macstore' else 'Telegram'
    app_name = binary_name + '.app'
    apps = {arch: folder / app_name for arch, folder in builds.items()}
    swift_runtime = pathlib.Path('Contents/Frameworks')
    file_sets = {
        arch: {p.relative_to(app) for p in app.rglob('*')
            if (p.is_symlink() or not p.is_dir())
            and not (p.relative_to(app).parent == swift_runtime
                and p.name.startswith('libswift') and p.suffix == '.dylib')}
        for arch, app in apps.items()
    }
    if file_sets['arm64'] != file_sets['x86_64']:
        raise RuntimeError('Architecture builds contain different bundle resources.')
    destination.mkdir(parents=True, exist_ok=True)
    lipo = toolchain / 'usr/bin/lipo'
    with tempfile.TemporaryDirectory(prefix='mac-universal-', dir=destination) as name:
        temporary = pathlib.Path(name)
        bundle = temporary / app_name
        shutil.copytree(apps['arm64'], bundle, symlinks=True)
        for runtime in (bundle / swift_runtime).glob('libswift*.dylib'):
            runtime.unlink()
        for relative in sorted(file_sets['arm64']):
            files = {arch: app / relative for arch, app in apps.items()}
            if macho(files['arm64']):
                if not macho(files['x86_64']):
                    raise RuntimeError('Mismatched bundle file: ' + str(relative))
                merge_binary(lipo, files, bundle / relative, temporary)
            elif files['arm64'].is_symlink():
                if os.readlink(files['arm64']) != os.readlink(files['x86_64']):
                    raise RuntimeError('Mismatched bundle symlink: ' + str(relative))
            elif resource_content(files['arm64']) != resource_content(files['x86_64']):
                raise RuntimeError('Mismatched bundle resource: ' + str(relative))
        run([toolchain / 'usr/bin/swift-stdlib-tool', '--copy', '--platform', 'macosx',
            '--scan-executable', bundle / 'Contents/MacOS' / binary_name,
            '--destination', bundle / swift_runtime])
        icon_info = temporary / 'assetcatalog.plist'
        root = pathlib.Path(__file__).resolve().parents[2]
        run(['xcrun', 'actool', root / 'Telegram/Telegram/Images.xcassets',
            '--compile', bundle / 'Contents/Resources',
            '--output-format', 'human-readable-text', '--notices', '--warnings',
            '--output-partial-info-plist', icon_info, '--app-icon', 'Icon',
            '--enable-on-demand-resources', 'NO', '--development-region', 'en',
            '--target-device', 'mac', '--minimum-deployment-target', '10.13',
            '--platform', 'macosx'])
        info_path = bundle / 'Contents/Info.plist'
        info = plistlib.loads(info_path.read_bytes())
        info.update(plistlib.loads(icon_info.read_bytes()))
        info_path.write_bytes(plistlib.dumps(info, sort_keys=False))
        if info['LSMinimumSystemVersion'] != '10.13':
            raise RuntimeError('The universal app lost its 10.13 deployment target.')
        identifier = 'org.telegram.desktop' if target == 'macstore' else 'com.tdesktop.Telegram'
        if info['CFBundleIdentifier'] != identifier or info['CFBundleExecutable'] != binary_name:
            raise RuntimeError('The app bundle does not match the selected distribution target.')
        resources = ['Contents/MacOS/' + binary_name,
            'Contents/Resources/Icon.icns', 'Contents/Resources/Assets.car',
            'Contents/Resources/Telegram.rcc',
            'Contents/Resources/lib_ui.rcc', 'Contents/Resources/lib_spellcheck.rcc',
            'Contents/Resources/external_microtex_bundled.rcc']
        if target == 'macstore':
            resources.extend(('Contents/Frameworks/Breakpad.framework/Versions/A/Breakpad',
                'Contents/Frameworks/Breakpad.framework/Versions/A/Resources/breakpadUtilities.dylib'))
        else:
            resources.extend(('Contents/Frameworks/Updater', 'Contents/Helpers/crashpad_handler'))
        for relative in resources:
            if not (bundle / relative).is_file():
                raise RuntimeError('Missing release resource: ' + relative)
        if target == 'mac':
            merge_binary(lipo, {arch: folder / 'Packer' for arch, folder in builds.items()},
                temporary / 'Packer', temporary)
        if configuration == 'Debug':
            if target == 'macstore':
                run(['codesign', '--force', '--sign', '-', bundle
                    / 'Contents/Frameworks/Breakpad.framework/Versions/A/Resources/breakpadUtilities.dylib'])
            run(['codesign', '--force', '--deep', '--sign', '-', bundle])
            run(['codesign', '--verify', '--deep', '--strict', bundle])
        result = destination / app_name
        if result.exists():
            shutil.rmtree(result)
        shutil.move(bundle, result)
        if target == 'mac':
            shutil.copy2(temporary / 'Packer', destination / 'Packer')
            shutil.copy2(result / 'Contents/Frameworks/Updater', destination / 'Updater')
    print('Universal app ready: ' + str(destination / app_name), flush=True)


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description='Build the universal macOS app with the preserved 26.6 toolchain.')
    parser.add_argument('--configuration', choices=['Debug', 'Release'], default='Release')
    parser.add_argument('--toolchain', type=pathlib.Path,
        default=root.parent / 'Toolchains/CommandLineTools-26.6')
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 1, 12))
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = (root / 'Telegram/build/target').read_text().strip()
    if target not in ('mac', 'macstore'):
        raise RuntimeError('Telegram/build/target must contain mac or macstore.')
    toolchain = args.toolchain.resolve()
    environment, sdk = toolchain_environment(toolchain)
    if args.check:
        return
    base = root / 'out' / (target + '-26.6') / args.configuration
    destination = args.output or (root / 'out/Release' if args.configuration == 'Release' else base / 'universal')
    builds = {arch: base / arch for arch in ('arm64', 'x86_64')}
    for arch, folder in builds.items():
        command = [sys.executable, root / 'Telegram/configure.py', '-GNinja', '-B', folder,
            '-DCMAKE_BUILD_TYPE=' + args.configuration,
            '-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13',
            '-DCMAKE_OSX_SYSROOT=' + str(sdk),
            '-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64',
            '-DDESKTOP_APP_MAC_ARCH=' + arch,
            '-DDESKTOP_APP_ENABLE_LTO=ON',
            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        for language, compiler in (('C', 'clang'), ('CXX', 'clang++'),
                ('OBJC', 'clang'), ('OBJCXX', 'clang++'), ('Swift', 'swiftc')):
            command.append('-DCMAKE_' + language + '_COMPILER=' + str(toolchain / 'usr/bin' / compiler))
        for variable, program in (('AR', 'ar'), ('RANLIB', 'ranlib'), ('LINKER', 'ld')):
            command.append('-DCMAKE_' + variable + '=' + str(toolchain / 'usr/bin' / program))
        print('Configuring ' + arch + ' ' + args.configuration + '.', flush=True)
        run(command, cwd=root, env=environment)
        verify_dependencies(folder, root.parent / 'Libraries', toolchain, arch, target)
        if not args.configure_only:
            run(['cmake', '--build', folder, '--target', 'Telegram', '--parallel', args.jobs],
                cwd=root, env=environment)
    if not args.configure_only:
        assemble(builds, destination.resolve(), toolchain, target, args.configuration)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print('macOS build failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
