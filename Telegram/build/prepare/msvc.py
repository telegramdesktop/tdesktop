import pathlib
import re
import shutil
import subprocess
import tempfile

def arm64_version(environment):
    compiler = shutil.which('cl.exe', path=environment.get('PATH', ''))
    if not compiler:
        raise RuntimeError('Could not find the ARM64 MSVC compiler.')
    with tempfile.TemporaryDirectory() as directory:
        source = pathlib.Path(directory) / 'compiler.c'
        source.write_text(
            '#if !defined(_MSC_FULL_VER) || !defined(_M_ARM64)\n'
            '#error Expected an ARM64 MSVC compiler.\n'
            '#endif\n'
            'TDESKTOP_MSVC_VERSION _MSC_FULL_VER\n', encoding='utf-8')
        try:
            result = subprocess.run(
                [compiler, '/nologo', '/EP', '/TC', str(source)],
                env=environment, capture_output=True, text=True,
                errors='replace', timeout=30)
        except (OSError, subprocess.TimeoutExpired) as exception:
            raise RuntimeError('Could not run the ARM64 MSVC compiler: '
                + str(exception)) from exception
    match = re.search(r'^TDESKTOP_MSVC_VERSION\s+(\d+)\s*$',
        result.stdout, re.MULTILINE)
    if result.returncode or not match:
        raise RuntimeError('Could not identify the ARM64 MSVC compiler. '
            'Run from the ARM64 Native Tools Command Prompt.\n'
            + result.stdout + result.stderr)
    return int(match.group(1))

def ffmpeg_environment(environment):
    version = arm64_version(environment)
    # https://github.com/telegramdesktop/tdesktop/issues/31570
    if not 195200000 <= version < 195237025:
        return environment, version
    failure = ('This MSVC 19.52 version miscompiles ARM64 FFmpeg. '
        'Install the 14.51 ARM64 build tools in this Visual Studio instance, '
        'or select MSVC 19.52.37025 or newer.')
    installation = environment.get('VSINSTALLDIR', '')
    host = environment.get('VSCMD_ARG_HOST_ARCH', '')
    setup = pathlib.Path(installation) / 'VC/Auxiliary/Build/vcvarsall.bat'
    if not installation or not setup.is_file() or host not in ('arm64', 'x64', 'x86'):
        raise RuntimeError(failure)
    architecture = 'arm64' if host == 'arm64' else host + '_arm64'
    with tempfile.TemporaryDirectory() as directory:
        script = pathlib.Path(directory) / 'ffmpeg-msvc.cmd'
        script.write_text('@echo off\n'
            + 'call "%VSINSTALLDIR%\\VC\\Auxiliary\\Build\\vcvarsall.bat" '
            + architecture + ' -vcvars_ver=14.51 >nul\n'
            + 'if errorlevel 1 exit /b 1\nset\n', encoding='utf-8')
        try:
            result = subprocess.run(
                [environment['COMSPEC'], '/d', '/u', '/c', str(script)],
                env=environment, capture_output=True, encoding='utf-16le',
                errors='replace', timeout=60)
        except (OSError, subprocess.TimeoutExpired) as exception:
            raise RuntimeError(failure) from exception
    if result.returncode:
        raise RuntimeError(failure)
    selected = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition('=')
        if key and separator:
            selected[key.upper()] = value
    prefix = environment.get('PATH_PREFIX', '')
    if not selected.get('PATH', '').startswith(prefix):
        selected['PATH'] = prefix + selected.get('PATH', '')
    version = arm64_version(selected)
    if not 195100000 <= version < 195200000:
        raise RuntimeError(failure)
    print('FFmpeg: using MSVC 14.51; other stages keep the selected toolset.')
    return selected, version
