import os, sys, pprint, re, json, pathlib, hashlib, subprocess, glob

executePath = os.getcwd()
scriptPath = os.path.dirname(os.path.realpath(__file__))

def finish(code):
    global executePath
    os.chdir(executePath)
    sys.exit(code)

def error(text):
    print('[ERROR] ' + text)
    finish(1)

def nativeToolsError():
    error('Make sure to run from Native Tools Command Prompt.')

win = (sys.platform == 'win32')
mac = (sys.platform == 'darwin')

if win and not 'Platform' in os.environ:
    nativeToolsError()

win32 = win and (os.environ['Platform'] == 'x86')
win64 = win and (os.environ['Platform'] == 'x64')

if win and not 'COMSPEC' in os.environ:
    error('COMSPEC environment variable is not set.')

if win and not win32 and not win64:
    nativeToolsError()

os.chdir(scriptPath + '/../../../..')

pathSep = ';' if win else ':'
libsLoc = 'Libraries' if not win64 else (os.path.join('Libraries', 'win64'))
keysLoc = 'cache_keys'

rootDir = os.getcwd()
libsDir = os.path.realpath(os.path.join(rootDir, libsLoc))
thirdPartyDir = os.path.realpath(os.path.join(rootDir, 'ThirdParty'))
usedPrefix = os.path.realpath(os.path.join(libsDir, 'local'))

optionsList = [
    'skip-release',
    'build-qt5',
    'skip-qt5',
    'build-qt6',
    'skip-qt6',
    'build-stackwalk',
]
options = []
runCommand = []
customRunCommand = False
for arg in sys.argv[1:]:
    if customRunCommand:
        runCommand.append(arg)
    if arg in optionsList:
        options.append(arg)
    elif arg == 'run':
        customRunCommand = True
buildQt5 = not 'skip-qt5' in options if win else 'build-qt5' in options
buildQt6 = 'build-qt6' in options if win else not 'skip-qt6' in options

if not os.path.isdir(os.path.join(libsDir, keysLoc)):
    pathlib.Path(os.path.join(libsDir, keysLoc)).mkdir(parents=True, exist_ok=True)
if not os.path.isdir(os.path.join(thirdPartyDir, keysLoc)):
    pathlib.Path(os.path.join(thirdPartyDir, keysLoc)).mkdir(parents=True, exist_ok=True)

pathPrefixes = [
    'ThirdParty\\msys64\\mingw64\\bin',
    'ThirdParty\\jom',
    'ThirdParty\\cmake\\bin',
    'ThirdParty\\gyp',
] if win else [
    'ThirdParty/gyp',
    'ThirdParty/yasm',
    'ThirdParty/depot_tools',
]
pathPrefix = ''
for singlePrefix in pathPrefixes:
    pathPrefix = pathPrefix + os.path.join(rootDir, singlePrefix) + pathSep

environment = {
    'USED_PREFIX': usedPrefix,
    'ROOT_DIR': rootDir,
    'LIBS_DIR': libsDir,
    'THIRDPARTY_DIR': thirdPartyDir,
    'PATH_PREFIX': pathPrefix,
}
if (win32):
    environment.update({
        'SPECIAL_TARGET': 'win',
        'X8664': 'x86',
        'WIN32X64': 'Win32',
    })
elif (win64):
    environment.update({
        'SPECIAL_TARGET': 'win64',
        'X8664': 'x64',
        'WIN32X64': 'x64',
    })
elif (mac):
    environment.update({
        'SPECIAL_TARGET': 'mac',
        'MAKE_THREADS_CNT': '-j8',
        'MACOSX_DEPLOYMENT_TARGET': '10.13',
        'UNGUARDED': '-Werror=unguarded-availability-new',
        'MIN_VER': '-mmacosx-version-min=10.13',
    })

ignoreInCacheForThirdParty = [
    'USED_PREFIX',
    'LIBS_DIR',
    'SPECIAL_TARGET',
    'X8664',
    'WIN32X64',
]

environmentKeyString = ''
envForThirdPartyKeyString = ''
for key in environment:
    part = key + '=' + environment[key] + ';'
    environmentKeyString += part
    if not key in ignoreInCacheForThirdParty:
        envForThirdPartyKeyString += part
environmentKey = hashlib.sha1(environmentKeyString.encode('utf-8')).hexdigest()
envForThirdPartyKey = hashlib.sha1(envForThirdPartyKeyString.encode('utf-8')).hexdigest()

modifiedEnv = os.environ.copy()
for key in environment:
    modifiedEnv[key] = environment[key]

modifiedEnv['PATH'] = environment['PATH_PREFIX'] + modifiedEnv['PATH']

def computeFileHash(path):
    sha1 = hashlib.sha1()
    with open(path, 'rb') as f:
        while True:
            data = f.read(256 * 1024)
            if not data:
                break
            sha1.update(data)
    return sha1.hexdigest()

def computeCacheKey(stage):
    if (stage['location'] == 'ThirdParty'):
        envKey = envForThirdPartyKey
    else:
        envKey = environmentKey
    objects = [
        envKey,
        stage['location'],
        stage['name'],
        stage['version'],
        stage['commands']
    ]
    for pattern in stage['dependencies']:
        pathlist = glob.glob(os.path.join(libsDir, pattern))
        items = [pattern]
        if len(pathlist) == 0:
            pathlist = glob.glob(os.path.join(thirdPartyDir, pattern))
        if len(pathlist) == 0:
            error('Nothing found: ' + pattern)
        for path in pathlist:
            if not os.path.exists(path):
                error('Not found: ' + path)
            items.append(computeFileHash(path))
        objects.append(':'.join(items))
    return hashlib.sha1(';'.join(objects).encode('utf-8')).hexdigest()

def keyPath(stage):
    return os.path.join(stage['directory'], keysLoc, stage['name'])

def checkCacheKey(stage):
    if not 'key' in stage:
        error('Key not set in stage: ' + stage['name'])
    key = keyPath(stage)
    if not os.path.exists(os.path.join(stage['directory'], stage['name'])):
        return 'NotFound'
    if not os.path.exists(key):
        return 'Stale'
    with open(key, 'r') as file:
        return 'Good' if (file.read() == stage['key']) else 'Stale'

def clearCacheKey(stage):
    key = keyPath(stage)
    if os.path.exists(key):
        os.remove(key)

def writeCacheKey(stage):
    if not 'key' in stage:
        error('Key not set in stage: ' + stage['name'])
    key = keyPath(stage)
    with open(key, 'w') as file:
        file.write(stage['key'])

stages = []

def removeDir(folder):
    if win:
        return 'if exist ' + folder + ' rmdir /Q /S ' + folder + '\nif exist ' + folder + ' exit /b 1'
    return 'rm -rf ' + folder

def filterByPlatform(commands):
    commands = commands.split('\n')
    result = ''
    dependencies = []
    version = '0'
    skip = False
    for command in commands:
        m = re.match(r'(!?)([a-z0-9_]+):', command)
        if m and m.group(2) != 'depends' and m.group(2) != 'version':
            scopes = m.group(2).split('_')
            inscope = 'common' in scopes
            if win and 'win' in scopes:
                inscope = True
            if win32 and 'win32' in scopes:
                inscope = True
            if win64 and 'win64' in scopes:
                inscope = True
            if mac and 'mac' in scopes:
                inscope = True
            # if linux and 'linux' in scopes:
            #     inscope = True
            if 'release' in scopes:
                if 'skip-release' in options:
                    inscope = False
                elif len(scopes) == 1:
                    continue
            skip = inscope if m.group(1) == '!' else not inscope
        elif not skip and not re.match(r'\s*#', command):
            if m and m.group(2) == 'version':
                version = version + '.' + command[len(m.group(0)):].strip()
            elif m and m.group(2) == 'depends':
                pattern = command[len(m.group(0)):].strip()
                dependencies.append(pattern)
            else:
                command = command.strip()
                if len(command) > 0:
                    result = result + command + '\n'
    return [result, dependencies, version]

def stage(name, commands, location = 'Libraries'):
    if location == 'Libraries':
        directory = libsDir
    elif location == 'ThirdParty':
        directory = thirdPartyDir
    else:
        error('Unknown location: ' + location)
    [commands, dependencies, version] = filterByPlatform(commands)
    if len(commands) > 0:
        stages.append({
            'name': name,
            'location': location,
            'directory': directory,
            'commands': commands,
            'version': version,
            'dependencies': dependencies
        })

def winFailOnEach(command):
    commands = command.split('\n')
    result = ''
    startingCommand = True
    for command in commands:
        command = re.sub(r'\$([A-Za-z0-9_]+)', r'%\1%', command)
        if re.search(r'\$[^<]', command):
            error('Bad command: ' + command)
        appendCall = startingCommand and not re.match(r'(if|for) ', command)
        called = 'call ' + command if appendCall else command
        result = result + called
        if command.endswith('^'):
            startingCommand = False
        else:
            startingCommand = True
            result = result + '\r\nif %errorlevel% neq 0 exit /b %errorlevel%\r\n'
    return result

def printCommands(commands):
    print('---------------------------------COMMANDS-LIST----------------------------------')
    print(commands, end='')
    print('--------------------------------------------------------------------------------')

def run(commands):
    printCommands(commands)
    if win:
        if os.path.exists("command.bat"):
            os.remove("command.bat")
        with open("command.bat", 'w') as file:
            file.write('@echo OFF\r\n' + winFailOnEach(commands))
        result = subprocess.run("command.bat", shell=True, env=modifiedEnv).returncode == 0
        if result and os.path.exists("command.bat"):
            os.remove("command.bat")
        return result
    elif re.search(r'\%', commands):
        error('Bad command: ' + commands)
    else:
        return subprocess.run("set -e\n" + commands, shell=True, env=modifiedEnv).returncode == 0

# Thanks https://stackoverflow.com/a/510364
class _Getch:
    """Gets a single character from standard input.  Does not echo to the
screen."""
    def __init__(self):
        try:
            self.impl = _GetchWindows()
        except ImportError:
            self.impl = _GetchUnix()

    def __call__(self): return self.impl()

class _GetchUnix:
    def __init__(self):
        import tty, sys

    def __call__(self):
        import sys, tty, termios
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(sys.stdin.fileno())
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        return ch

class _GetchWindows:
    def __init__(self):
        import msvcrt

    def __call__(self):
        import msvcrt
        return msvcrt.getch().decode('ascii')

getch = _Getch()

def runStages():
    onlyStages = []
    rebuildStale = False
    for arg in sys.argv[1:]:
        if arg in options:
            continue
        elif arg == 'silent':
            rebuildStale = True
            continue
        found = False
        for stage in stages:
            if stage['name'] == arg:
                onlyStages.append(arg)
                found = True
                break
        if not found:
            error('Unknown argument: ' + arg)
    count = len(stages)
    index = 0
    for stage in stages:
        if len(onlyStages) > 0 and not stage['name'] in onlyStages:
            continue
        index = index + 1
        version = ('#' + str(stage['version'])) if (stage['version'] != '0') else ''
        prefix = '[' + str(index) + '/' + str(count) + '](' + stage['location'] + '/' + stage['name'] + version + ')'
        print(prefix + ': ', end = '', flush=True)
        stage['key'] = computeCacheKey(stage)
        commands = removeDir(stage['name']) + '\n' + stage['commands']
        checkResult = 'Forced' if len(onlyStages) > 0 else checkCacheKey(stage)
        if checkResult == 'Good':
            print('SKIPPING')
            continue
        elif checkResult == 'NotFound':
            print('NOT FOUND, ', end='')
        elif checkResult == 'Stale' or checkResult == 'Forced':
            if checkResult == 'Stale':
                print('CHANGED, ', end='')
            if rebuildStale:
                checkResult == 'Rebuild'
            else:
                print('(r)ebuild, rebuild (a)ll, (s)kip, (p)rint, (q)uit?: ', end='', flush=True)
                while True:
                    ch = 'r' if rebuildStale else getch()
                    if ch == 'q':
                        finish(0)
                    elif ch == 'p':
                        printCommands(commands)
                        checkResult = 'Printed'
                        break
                    elif ch == 's':
                        checkResult = 'Skip'
                        break
                    elif ch == 'r':
                        checkResult = 'Rebuild'
                        break
                    elif ch == 'a':
                        checkResult = 'Rebuild'
                        rebuildStale = True
                        break
        if checkResult == 'Printed':
            continue
        if checkResult == 'Skip':
            print('SKIPPING')
            continue
        clearCacheKey(stage)
        print('BUILDING:')
        os.chdir(stage['directory'])
        if not run(commands):
            print(prefix + ': FAILED')
            finish(1)
        writeCacheKey(stage)

if customRunCommand:
    os.chdir(executePath)
    command = ' '.join(runCommand) + '\n'
    if not run(command):
        print('FAILED :(')
        finish(1)
    finish(0)

stage('patches', """
    git clone https://github.com/desktop-app/patches.git
    cd patches
    git checkout 58c8cd0c0f
""")

stage('msys64', """
win:
    SET PATH_BACKUP_=%PATH%
    SET PATH=%ROOT_DIR%\\ThirdParty\\msys64\\usr\\bin;%PATH%

    SET CHERE_INVOKING=enabled_from_arguments
    SET MSYS2_PATH_TYPE=inherit

    powershell -Command "iwr -OutFile ./msys64.exe https://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20221028.sfx.exe"
    msys64.exe
    del msys64.exe

    bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm"
    pacman -Syu --noconfirm mingw-w64-x86_64-perl mingw-w64-x86_64-nasm mingw-w64-x86_64-yasm mingw-w64-x86_64-ninja

    SET PATH=%PATH_BACKUP_%
""", 'ThirdParty')

stage('python', """
version: """ + (subprocess.run(['python', '-V'], capture_output=True, text=True, env=modifiedEnv).stdout.strip().split()[-1] if win else '0') + """
win:
    python -m venv python
    python\\Scripts\\activate.bat
    pip install pywin32 six meson
    deactivate
""", 'ThirdParty')

stage('NuGet', """
win:
    mkdir NuGet
    powershell -Command "iwr -OutFile ./NuGet/nuget.exe https://dist.nuget.org/win-x86-commandline/latest/nuget.exe"
""", 'ThirdParty')

stage('jom', """
win:
    powershell -Command "iwr -OutFile ./jom.zip https://master.qt.io/official_releases/jom/jom_1_1_3.zip"
    powershell -Command "Expand-Archive ./jom.zip"
    del jom.zip
""", 'ThirdParty')

stage('depot_tools', """
mac:
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
    cd depot_tools
    ./update_depot_tools
""", 'ThirdParty')

if not mac or 'build-stackwalk' in options:
    stage('gyp', """
win:
    git clone https://chromium.googlesource.com/external/gyp
    cd gyp
    git checkout 9d09418933
mac:
    python3 -m pip install \\
        --ignore-installed \\
        --target=$THIRDPARTY_DIR/gyp \\
        git+https://chromium.googlesource.com/external/gyp@master
""", 'ThirdParty')

stage('yasm', """
mac:
    git clone https://github.com/yasm/yasm.git
    cd yasm
    git checkout 41762bea
    ./autogen.sh
    make $MAKE_THREADS_CNT
""", 'ThirdParty')

stage('lzma', """
win:
    git clone https://github.com/desktop-app/lzma.git
    cd lzma\\C\\Util\\LzmaLib
    msbuild LzmaLib.sln /property:Configuration=Debug /property:Platform="$X8664"
release:
    msbuild LzmaLib.sln /property:Configuration=Release /property:Platform="$X8664"
""")

stage('xz', """
!win:
    git clone -b v5.4.5 https://git.tukaani.org/xz.git
    cd xz
    sed -i '' '\\@check_symbol_exists(futimens "sys/types.h;sys/stat.h" HAVE_FUTIMENS)@d' CMakeLists.txt
    CFLAGS="$UNGUARDED" CPPFLAGS="$UNGUARDED" cmake -B build . \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" \\
        -D CMAKE_INSTALL_PREFIX:STRING=$USED_PREFIX
    cmake --build build $MAKE_THREADS_CNT
    cmake --install build
""")

stage('zlib', """
    git clone -b v1.3 https://github.com/madler/zlib.git
    cd zlib
win:
    cmake . ^
        -A %WIN32X64% ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
        -DCMAKE_C_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DCMAKE_C_FLAGS="/DZLIB_WINAPI"
    cmake --build . --config Debug
release:
    cmake --build . --config Release
mac:
    CFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure \\
        --static \\
        --prefix=$USED_PREFIX \\
        --archs="-arch x86_64 -arch arm64"
    make $MAKE_THREADS_CNT
    make install
""")

stage('mozjpeg', """
    git clone -b v4.1.5 https://github.com/mozilla/mozjpeg.git
    cd mozjpeg
win:
    cmake . ^
        -A %WIN32X64% ^
        -DWITH_JPEG8=ON ^
        -DPNG_SUPPORTED=OFF
    cmake --build . --config Debug
release:
    cmake --build . --config Release
mac:
    CFLAGS="-arch arm64" cmake -B build.arm64 . \\
        -D CMAKE_SYSTEM_NAME=Darwin \\
        -D CMAKE_SYSTEM_PROCESSOR=arm64 \\
        -D CMAKE_BUILD_TYPE=Release \\
        -D CMAKE_INSTALL_PREFIX=$USED_PREFIX \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D WITH_JPEG8=ON \\
        -D ENABLE_SHARED=OFF \\
        -D PNG_SUPPORTED=OFF
    cmake --build build.arm64 $MAKE_THREADS_CNT
    CFLAGS="-arch x86_64" cmake -B build . \\
        -D CMAKE_SYSTEM_NAME=Darwin \\
        -D CMAKE_SYSTEM_PROCESSOR=x86_64 \\
        -D CMAKE_BUILD_TYPE=Release \\
        -D CMAKE_INSTALL_PREFIX=$USED_PREFIX \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D WITH_JPEG8=ON \\
        -D ENABLE_SHARED=OFF \\
        -D PNG_SUPPORTED=OFF
    cmake --build build $MAKE_THREADS_CNT
    lipo -create build.arm64/libjpeg.a build/libjpeg.a -output build/libjpeg.a
    lipo -create build.arm64/libturbojpeg.a build/libturbojpeg.a -output build/libturbojpeg.a
    cmake --install build
""")

stage('openssl', """
    git clone -b OpenSSL_1_1_1-stable https://github.com/openssl/openssl openssl
    cd openssl
win32:
    perl Configure no-shared no-tests debug-VC-WIN32
win64:
    perl Configure no-shared no-tests debug-VC-WIN64A
win:
    nmake
    mkdir out.dbg
    move libcrypto.lib out.dbg
    move libssl.lib out.dbg
    move ossl_static.pdb out.dbg
release:
    move out.dbg\\ossl_static.pdb out.dbg\\ossl_static
    nmake clean
    move out.dbg\\ossl_static out.dbg\\ossl_static.pdb
win32:
    perl Configure no-shared no-tests VC-WIN32
win64:
    perl Configure no-shared no-tests VC-WIN64A
win:
    nmake
    mkdir out
    move libcrypto.lib out
    move libssl.lib out
    move ossl_static.pdb out
mac:
    ./Configure --prefix=$USED_PREFIX no-shared no-tests darwin64-arm64-cc $MIN_VER
    make build_libs $MAKE_THREADS_CNT
    mkdir out.arm64
    mv libssl.a out.arm64
    mv libcrypto.a out.arm64
    make clean
    ./Configure --prefix=$USED_PREFIX no-shared no-tests darwin64-x86_64-cc $MIN_VER
    make build_libs $MAKE_THREADS_CNT
    mkdir out.x86_64
    mv libssl.a out.x86_64
    mv libcrypto.a out.x86_64
    lipo -create out.arm64/libcrypto.a out.x86_64/libcrypto.a -output libcrypto.a
    lipo -create out.arm64/libssl.a out.x86_64/libssl.a -output libssl.a
""")

stage('opus', """
    git clone -b v1.3.1 https://github.com/xiph/opus.git
    cd opus
    git cherry-pick 927de8453c
win:
    cmake -B out . ^
        -A %WIN32X64% ^
        -DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
        -DCMAKE_C_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG"
    cmake --build out --config Debug
    cmake --build out --config Release
    cmake --install out --config Release
mac:
    CFLAGS="$UNGUARDED" CPPFLAGS="$UNGUARDED" cmake -B build . \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" \\
        -D CMAKE_INSTALL_PREFIX:STRING=$USED_PREFIX
    cmake --build build $MAKE_THREADS_CNT
    cmake --install build
""")

stage('rnnoise', """
    git clone https://github.com/desktop-app/rnnoise.git
    cd rnnoise
    git checkout fe37e57d09
    mkdir out
    cd out
win:
    cmake -A %WIN32X64% ..
    cmake --build . --config Debug
release:
    cmake --build . --config Release
!win:
    mkdir Debug
    cd Debug
    cmake -G Ninja ../.. \\
        -D CMAKE_BUILD_TYPE=Debug \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64"
    ninja
release:
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja ../.. \\
        -D CMAKE_BUILD_TYPE=Release \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64"
    ninja
""")

stage('libiconv', """
mac:
    VERSION=1.17
    rm -f libiconv.tar.gz
    wget -O libiconv.tar.gz ftp://ftp.gnu.org/gnu/libiconv/libiconv-$VERSION.tar.gz
    rm -rf libiconv-$VERSION
    tar -xvzf libiconv.tar.gz
    rm libiconv.tar.gz
    mv libiconv-$VERSION libiconv
    cd libiconv
    CFLAGS="$MIN_VER $UNGUARDED -arch arm64" CPPFLAGS="$MIN_VER $UNGUARDED -arch arm64" LDFLAGS="$MIN_VER" ./configure --enable-static --host=arm --prefix=$USED_PREFIX
    make $MAKE_THREADS_CNT
    mkdir out.arm64
    mv lib/.libs/libiconv.a out.arm64
    make clean
    CFLAGS="$MIN_VER $UNGUARDED -arch x86_64" CPPFLAGS="$MIN_VER $UNGUARDED -arch x86_64" LDFLAGS="$MIN_VER" ./configure --enable-static --host=x86_64 --prefix=$USED_PREFIX
    make $MAKE_THREADS_CNT
    mkdir out.x86_64
    mv lib/.libs/libiconv.a out.x86_64
    lipo -create out.arm64/libiconv.a out.x86_64/libiconv.a -output lib/.libs/libiconv.a
    make install
""")

stage('dav1d', """
win:
    git clone -b 1.2.1 --depth 1 https://code.videolan.org/videolan/dav1d.git
    cd dav1d

    if "%X8664%" equ "x64" (
        SET "TARGET=x86_64"
    ) else (
        SET "TARGET=x86"
    )
    set FILE=cross-file.txt
    echo [binaries] > %FILE%
    echo c = 'cl' >> %FILE%
    echo cpp = 'cl' >> %FILE%
    echo ar = 'lib' >> %FILE%
    echo windres = 'rc' >> %FILE%
    echo [host_machine] >> %FILE%
    echo system = 'windows' >> %FILE%
    echo cpu_family = '%TARGET%' >> %FILE%
    echo cpu = '%TARGET%' >> %FILE%
    echo endian = 'little'>> %FILE%

depends:python/Scripts/activate.bat
    %THIRDPARTY_DIR%\\python\\Scripts\\activate.bat
    meson setup --cross-file %FILE% --prefix %LIBS_DIR%/local --default-library=static --buildtype=debug -Denable_tools=false -Denable_tests=false -Db_vscrt=mtd builddir-debug
    meson compile -C builddir-debug
    meson install -C builddir-debug
release:
    meson setup --cross-file %FILE% --prefix %LIBS_DIR%/local --default-library=static --buildtype=release -Denable_tools=false -Denable_tests=false -Db_vscrt=mt builddir-release
    meson compile -C builddir-release
    meson install -C builddir-release
win:
    copy %LIBS_DIR%\\local\\lib\\libdav1d.a %LIBS_DIR%\\local\\lib\\dav1d.lib
    deactivate
""")

stage('libavif', """
win:
    git clone -b v0.11.1 --depth 1 https://github.com/AOMediaCodec/libavif.git
    cd libavif
    cmake . ^
        -A %WIN32X64% ^
        -DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
        -DCMAKE_C_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DAVIF_ENABLE_WERROR=OFF ^
        -DAVIF_CODEC_DAV1D=ON
    cmake --build . --config Debug
    cmake --install . --config Debug
release:
    cmake --build . --config Release
    cmake --install . --config Release
""")

stage('libde265', """
win:
    git clone --depth 1 -b v1.0.12 https://github.com/strukturag/libde265.git
    cd libde265
    cmake . ^
        -A %WIN32X64% ^
        -DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
        -DCMAKE_C_FLAGS="/DLIBDE265_STATIC_BUILD" ^
        -DCMAKE_CXX_FLAGS="/DLIBDE265_STATIC_BUILD" ^
        -DCMAKE_C_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_CXX_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DENABLE_SDL=OFF ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DENABLE_DECODER=OFF ^
        -DENABLE_ENCODER=OFF
    cmake --build . --config Debug
    cmake --install . --config Debug
release:
    cmake --build . --config Release
    cmake --install . --config Release
""")

stage('libheif', """
win:
    git clone --depth 1 -b v1.16.2 https://github.com/strukturag/libheif.git
    cd libheif
    %THIRDPARTY_DIR%\\msys64\\usr\\bin\\sed.exe -i 's/LIBHEIF_EXPORTS/LIBDE265_STATIC_BUILD/g' libheif/CMakeLists.txt
    %THIRDPARTY_DIR%\\msys64\\usr\\bin\\sed.exe -i 's/HAVE_VISIBILITY/LIBHEIF_STATIC_BUILD/g' libheif/CMakeLists.txt
    cmake . ^
        -A %WIN32X64% ^
        -DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
        -DCMAKE_C_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_CXX_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DENABLE_PLUGIN_LOADING=OFF ^
        -DWITH_LIBDE265=ON ^
        -DWITH_SvtEnc=OFF ^
        -DWITH_SvtEnc_PLUGIN=OFF ^
        -DWITH_RAV1E=OFF ^
        -DWITH_RAV1E_PLUGIN=OFF ^
        -DWITH_EXAMPLES=OFF
    cmake --build . --config Debug
    cmake --install . --config Debug
release:
    cmake --build . --config Release
    cmake --install . --config Release
""")

stage('libjxl', """
win:
    git clone -b v0.8.2 --depth 1 --recursive --shallow-submodules https://github.com/libjxl/libjxl.git
    cd libjxl
    cmake . ^
        -A %WIN32X64% ^
        -DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
        -DCMAKE_C_FLAGS="/DJXL_STATIC_DEFINE /DJXL_THREADS_STATIC_DEFINE" ^
        -DCMAKE_CXX_FLAGS="/DJXL_STATIC_DEFINE /DJXL_THREADS_STATIC_DEFINE" ^
        -DCMAKE_C_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_CXX_FLAGS_DEBUG="/MTd /Zi /Ob0 /Od /RTC1" ^
        -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DBUILD_TESTING=OFF ^
        -DJPEGXL_ENABLE_FUZZERS=OFF ^
        -DJPEGXL_ENABLE_DEVTOOLS=OFF ^
        -DJPEGXL_ENABLE_TOOLS=OFF ^
        -DJPEGXL_ENABLE_DOXYGEN=OFF ^
        -DJPEGXL_ENABLE_MANPAGES=OFF ^
        -DJPEGXL_ENABLE_EXAMPLES=OFF ^
        -DJPEGXL_ENABLE_JNI=OFF ^
        -DJPEGXL_ENABLE_JPEGLI_LIBJPEG=OFF ^
        -DJPEGXL_ENABLE_SJPEG=OFF ^
        -DJPEGXL_ENABLE_OPENEXR=OFF ^
        -DJPEGXL_ENABLE_SKCMS=ON ^
        -DJPEGXL_BUNDLE_SKCMS=ON ^
        -DJPEGXL_ENABLE_VIEWERS=OFF ^
        -DJPEGXL_ENABLE_TCMALLOC=OFF ^
        -DJPEGXL_ENABLE_PLUGINS=OFF ^
        -DJPEGXL_ENABLE_COVERAGE=OFF ^
        -DJPEGXL_ENABLE_PROFILER=OFF ^
        -DJPEGXL_WARNINGS_AS_ERRORS=OFF
    cmake --build . --config Debug
    cmake --install . --config Debug
release:
    cmake --build . --config Release
    cmake --install . --config Release
""")

stage('libvpx', """
    git clone https://github.com/webmproject/libvpx.git
depends:patches/libvpx/*.patch
    cd libvpx
    git checkout 51057f4ba8
win:
    for /r %%i in (..\\patches\\libvpx\\*) do git apply %%i

    SET PATH_BACKUP_=%PATH%
    SET PATH=%ROOT_DIR%\\ThirdParty\\msys64\\usr\\bin;%PATH%

    SET CHERE_INVOKING=enabled_from_arguments
    SET MSYS2_PATH_TYPE=inherit

    if "%X8664%" equ "x64" (
        SET "TARGET=x86_64-win64-vs17"
    ) else (
        SET "TARGET=x86-win32-vs17"
    )

depends:patches/build_libvpx_win.sh
    bash --login ../patches/build_libvpx_win.sh

    SET PATH=%PATH_BACKUP_%
mac:
    find ../patches/libvpx -type f -print0 | sort -z | xargs -0 git apply

depends:yasm/yasm
    ./configure --prefix=$USED_PREFIX \
    --target=arm64-darwin20-gcc \
    --disable-examples \
    --disable-unit-tests \
    --disable-tools \
    --disable-docs \
    --enable-vp8 \
    --enable-vp9 \
    --enable-webm-io \
    --size-limit=4096x4096

    make $MAKE_THREADS_CNT

    mkdir out.arm64
    mv libvpx.a out.arm64

    make clean

    ./configure --prefix=$USED_PREFIX \
    --target=x86_64-darwin20-gcc \
    --disable-examples \
    --disable-unit-tests \
    --disable-tools \
    --disable-docs \
    --enable-vp8 \
    --enable-vp9 \
    --enable-webm-io

    make $MAKE_THREADS_CNT

    mkdir out.x86_64
    mv libvpx.a out.x86_64

    lipo -create out.arm64/libvpx.a out.x86_64/libvpx.a -output libvpx.a

    make install
""")

stage('libwebp', """
    git clone -b v1.3.2 https://github.com/webmproject/libwebp.git
    cd libwebp
win:
    nmake /f Makefile.vc CFG=debug-static OBJDIR=out RTLIBCFG=static all
    nmake /f Makefile.vc CFG=release-static OBJDIR=out RTLIBCFG=static all
    copy out\\release-static\\$X8664\\lib\\libwebp.lib out\\release-static\\$X8664\\lib\\webp.lib
    copy out\\release-static\\$X8664\\lib\\libwebpdemux.lib out\\release-static\\$X8664\\lib\\webpdemux.lib
    copy out\\release-static\\$X8664\\lib\\libwebpmux.lib out\\release-static\\$X8664\\lib\\webpmux.lib
mac:
    buildOneArch() {
        arch=$1
        folder=$2

        CFLAGS=$UNGUARDED cmake -B $folder -G Ninja . \\
            -D CMAKE_BUILD_TYPE=Release \\
            -D CMAKE_INSTALL_PREFIX=$USED_PREFIX \\
            -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
            -D CMAKE_OSX_ARCHITECTURES=$arch \\
            -D WEBP_BUILD_ANIM_UTILS=OFF \\
            -D WEBP_BUILD_CWEBP=OFF \\
            -D WEBP_BUILD_DWEBP=OFF \\
            -D WEBP_BUILD_GIF2WEBP=OFF \\
            -D WEBP_BUILD_IMG2WEBP=OFF \\
            -D WEBP_BUILD_VWEBP=OFF \\
            -D WEBP_BUILD_WEBPMUX=OFF \\
            -D WEBP_BUILD_WEBPINFO=OFF \\
            -D WEBP_BUILD_EXTRAS=OFF
        cmake --build $folder $MAKE_THREADS_CNT
    }
    buildOneArch arm64 build.arm64
    buildOneArch x86_64 build

    lipo -create build.arm64/libsharpyuv.a build/libsharpyuv.a -output build/libsharpyuv.a
    lipo -create build.arm64/libwebp.a build/libwebp.a -output build/libwebp.a
    lipo -create build.arm64/libwebpdemux.a build/libwebpdemux.a -output build/libwebpdemux.a
    lipo -create build.arm64/libwebpmux.a build/libwebpmux.a -output build/libwebpmux.a
    cmake --install build
""")

stage('nv-codec-headers', """
win:
    git clone https://github.com/FFmpeg/nv-codec-headers.git
    cd nv-codec-headers
    git checkout n11.1.5.1
""")

stage('regex', """
    git clone -b boost-1.83.0 https://github.com/boostorg/regex.git
""")

stage('ffmpeg', """
    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout 7268323193
win:
    SET PATH_BACKUP_=%PATH%
    SET PATH=%ROOT_DIR%\\ThirdParty\\msys64\\usr\\bin;%PATH%

    SET CHERE_INVOKING=enabled_from_arguments
    SET MSYS2_PATH_TYPE=inherit

depends:patches/build_ffmpeg_win.sh
    bash --login ../patches/build_ffmpeg_win.sh

    SET PATH=%PATH_BACKUP_%
mac:
    export PKG_CONFIG_PATH=$USED_PREFIX/lib/pkgconfig
depends:yasm/yasm

    configureFFmpeg() {
        arch=$1

        ./configure --prefix=$USED_PREFIX \
        --enable-cross-compile \
        --target-os=darwin \
        --arch="$arch" \
        --extra-cflags="$MIN_VER -arch $arch $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1 -I$USED_PREFIX/include" \
        --extra-cxxflags="$MIN_VER -arch $arch $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1 -I$USED_PREFIX/include" \
        --extra-ldflags="$MIN_VER -arch $arch $USED_PREFIX/lib/libopus.a" \
        --disable-programs \
        --disable-doc \
        --disable-network \
        --disable-everything \
        --enable-protocol=file \
        --enable-libopus \
        --enable-libvpx \
        --enable-hwaccel=h264_videotoolbox \
        --enable-hwaccel=hevc_videotoolbox \
        --enable-hwaccel=mpeg1_videotoolbox \
        --enable-hwaccel=mpeg2_videotoolbox \
        --enable-hwaccel=mpeg4_videotoolbox \
        --enable-decoder=aac \
        --enable-decoder=aac_at \
        --enable-decoder=aac_fixed \
        --enable-decoder=aac_latm \
        --enable-decoder=aasc \
        --enable-decoder=ac3 \
        --enable-decoder=alac \
        --enable-decoder=alac_at \
        --enable-decoder=av1 \
        --enable-decoder=eac3 \
        --enable-decoder=flac \
        --enable-decoder=gif \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-decoder=libvpx_vp8 \
        --enable-decoder=libvpx_vp9 \
        --enable-decoder=mp1 \
        --enable-decoder=mp1float \
        --enable-decoder=mp2 \
        --enable-decoder=mp2float \
        --enable-decoder=mp3 \
        --enable-decoder=mp3adu \
        --enable-decoder=mp3adufloat \
        --enable-decoder=mp3float \
        --enable-decoder=mp3on4 \
        --enable-decoder=mp3on4float \
        --enable-decoder=mpeg4 \
        --enable-decoder=msmpeg4v2 \
        --enable-decoder=msmpeg4v3 \
        --enable-decoder=opus \
        --enable-decoder=pcm_alaw \
        --enable-decoder=pcm_alaw_at \
        --enable-decoder=pcm_f32be \
        --enable-decoder=pcm_f32le \
        --enable-decoder=pcm_f64be \
        --enable-decoder=pcm_f64le \
        --enable-decoder=pcm_lxf \
        --enable-decoder=pcm_mulaw \
        --enable-decoder=pcm_mulaw_at \
        --enable-decoder=pcm_s16be \
        --enable-decoder=pcm_s16be_planar \
        --enable-decoder=pcm_s16le \
        --enable-decoder=pcm_s16le_planar \
        --enable-decoder=pcm_s24be \
        --enable-decoder=pcm_s24daud \
        --enable-decoder=pcm_s24le \
        --enable-decoder=pcm_s24le_planar \
        --enable-decoder=pcm_s32be \
        --enable-decoder=pcm_s32le \
        --enable-decoder=pcm_s32le_planar \
        --enable-decoder=pcm_s64be \
        --enable-decoder=pcm_s64le \
        --enable-decoder=pcm_s8 \
        --enable-decoder=pcm_s8_planar \
        --enable-decoder=pcm_u16be \
        --enable-decoder=pcm_u16le \
        --enable-decoder=pcm_u24be \
        --enable-decoder=pcm_u24le \
        --enable-decoder=pcm_u32be \
        --enable-decoder=pcm_u32le \
        --enable-decoder=pcm_u8 \
        --enable-decoder=vorbis \
        --enable-decoder=vp8 \
        --enable-decoder=wavpack \
        --enable-decoder=wmalossless \
        --enable-decoder=wmapro \
        --enable-decoder=wmav1 \
        --enable-decoder=wmav2 \
        --enable-decoder=wmavoice \
        --enable-encoder=libopus \
        --enable-filter=atempo \
        --enable-parser=aac \
        --enable-parser=aac_latm \
        --enable-parser=flac \
        --enable-parser=h264 \
        --enable-parser=hevc \
        --enable-parser=mpeg4video \
        --enable-parser=mpegaudio \
        --enable-parser=opus \
        --enable-parser=vorbis \
        --enable-demuxer=aac \
        --enable-demuxer=flac \
        --enable-demuxer=gif \
        --enable-demuxer=h264 \
        --enable-demuxer=hevc \
        --enable-demuxer=matroska \
        --enable-demuxer=m4v \
        --enable-demuxer=mov \
        --enable-demuxer=mp3 \
        --enable-demuxer=ogg \
        --enable-demuxer=wav \
        --enable-muxer=ogg \
        --enable-muxer=opus
    }

    configureFFmpeg arm64
    make $MAKE_THREADS_CNT

    mkdir out.arm64
    mv libavfilter/libavfilter.a out.arm64
    mv libavformat/libavformat.a out.arm64
    mv libavcodec/libavcodec.a out.arm64
    mv libswresample/libswresample.a out.arm64
    mv libswscale/libswscale.a out.arm64
    mv libavutil/libavutil.a out.arm64

    make clean

    configureFFmpeg x86_64
    make $MAKE_THREADS_CNT

    mkdir out.x86_64
    mv libavfilter/libavfilter.a out.x86_64
    mv libavformat/libavformat.a out.x86_64
    mv libavcodec/libavcodec.a out.x86_64
    mv libswresample/libswresample.a out.x86_64
    mv libswscale/libswscale.a out.x86_64
    mv libavutil/libavutil.a out.x86_64

    lipo -create out.arm64/libavfilter.a out.x86_64/libavfilter.a -output libavfilter/libavfilter.a
    lipo -create out.arm64/libavformat.a out.x86_64/libavformat.a -output libavformat/libavformat.a
    lipo -create out.arm64/libavcodec.a out.x86_64/libavcodec.a -output libavcodec/libavcodec.a
    lipo -create out.arm64/libswresample.a out.x86_64/libswresample.a -output libswresample/libswresample.a
    lipo -create out.arm64/libswscale.a out.x86_64/libswscale.a -output libswscale/libswscale.a
    lipo -create out.arm64/libavutil.a out.x86_64/libavutil.a -output libavutil/libavutil.a

    make install
""")

stage('openal-soft', """
version: 2
win:
    git clone -b wasapi_exact_device_time https://github.com/telegramdesktop/openal-soft.git
    cd openal-soft
    cmake -B build . ^
        -A %WIN32X64% ^
        -D LIBTYPE:STRING=STATIC ^
        -D FORCE_STATIC_VCRT=ON
    cmake --build build --config Debug --parallel
release:
    cmake --build build --config RelWithDebInfo --parallel
mac:
    git clone https://github.com/kcat/openal-soft.git
    cd openal-soft
    git checkout 1.23.1
    CFLAGS=$UNGUARDED CPPFLAGS=$UNGUARDED cmake -B build . \\
        -D CMAKE_BUILD_TYPE=RelWithDebInfo \\
        -D CMAKE_INSTALL_PREFIX:PATH=$USED_PREFIX \\
        -D ALSOFT_EXAMPLES=OFF \\
        -D ALSOFT_UTILS=OFF \\
        -D LIBTYPE:STRING=STATIC \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64"
    cmake --build build $MAKE_THREADS_CNT
    cmake --install build
""")

if 'build-stackwalk' in options:
    stage('stackwalk', """
mac:
    git clone https://chromium.googlesource.com/breakpad/breakpad stackwalk
    cd stackwalk
    git checkout dfcb7b6799
depends:patches/breakpad.diff
    git apply ../patches/breakpad.diff
    git clone -b release-1.11.0 https://github.com/google/googletest src/testing
    git clone https://chromium.googlesource.com/linux-syscall-support src/third_party/lss
    cd src/third_party/lss
    git checkout e1e7b0ad8e
    cd ../../build
    PYTHONPATH=$THIRDPARTY_DIR/gyp python3 gyp_breakpad
    cd ../processor
    xcodebuild -project processor.xcodeproj -target minidump_stackwalk -configuration Release build
""")

stage('breakpad', """
    git clone https://chromium.googlesource.com/breakpad/breakpad
    cd breakpad
    git checkout dfcb7b6799
depends:patches/breakpad.diff
    git apply ../patches/breakpad.diff
    git clone -b release-1.11.0 https://github.com/google/googletest src/testing
win:
    SET "PYTHONUTF8=1"
    if "%X8664%" equ "x64" (
        SET "FolderPostfix=_x64"
    ) else (
        SET "FolderPostfix="
    )
depends:python/Scripts/activate.bat
    %THIRDPARTY_DIR%\\python\\Scripts\\activate.bat
    cd src\\client\\windows
    gyp --no-circular-check breakpad_client.gyp --format=ninja
    cd ..\\..
    ninja -C out/Debug%FolderPostfix% common crash_generation_client exception_handler
release:
    ninja -C out/Release%FolderPostfix% common crash_generation_client exception_handler
    cd tools\\windows\\dump_syms
    gyp dump_syms.gyp --format=msvs
    msbuild dump_syms.vcxproj /property:Configuration=Release /property:Platform="x64"
win:
    deactivate
mac:
    git clone https://chromium.googlesource.com/linux-syscall-support src/third_party/lss
    cd src/third_party/lss
    git checkout e1e7b0ad8e
    cd ../../..
    cd src/client/mac
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Debug build
release:
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Release build
    cd ../../tools/mac/dump_syms
    xcodebuild -project dump_syms.xcodeproj -target dump_syms -configuration Release build
""")

stage('crashpad', """
mac:
    git clone https://github.com/desktop-app/crashpad.git
    cd crashpad
    git checkout 3279fae3f0
    git submodule init
    git submodule update third_party/mini_chromium
    ZLIB_PATH=$USED_PREFIX/include
    ZLIB_LIB=$USED_PREFIX/lib/libz.a
    mkdir out
    cd out
    mkdir Debug.x86_64
    cd Debug.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Debug.arm64
    cd Debug.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Debug
    lipo -create Debug.arm64/crashpad_handler Debug.x86_64/crashpad_handler -output Debug/crashpad_handler
    lipo -create Debug.arm64/libcrashpad_client.a Debug.x86_64/libcrashpad_client.a -output Debug/libcrashpad_client.a
release:
    mkdir Release.x86_64
    cd Release.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Release.arm64
    cd Release.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCRASHPAD_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DCRASHPAD_ZLIB_INCLUDE_PATH=$ZLIB_PATH \
        -DCRASHPAD_ZLIB_LIB_PATH=$ZLIB_LIB ../..
    ninja
    cd ..
    mkdir Release
    lipo -create Release.arm64/crashpad_handler Release.x86_64/crashpad_handler -output Release/crashpad_handler
    lipo -create Release.arm64/libcrashpad_client.a Release.x86_64/libcrashpad_client.a -output Release/libcrashpad_client.a
""")

stage('tg_angle', """
win:
    git clone https://github.com/desktop-app/tg_angle.git
    cd tg_angle
    git checkout e3f59e8d0c
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja ^
        -DCMAKE_BUILD_TYPE=Debug ^
        -DTG_ANGLE_SPECIAL_TARGET=%SPECIAL_TARGET% ^
        -DTG_ANGLE_ZLIB_INCLUDE_PATH=%LIBS_DIR%/zlib ../..
    ninja
release:
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DTG_ANGLE_SPECIAL_TARGET=%SPECIAL_TARGET% ^
        -DTG_ANGLE_ZLIB_INCLUDE_PATH=%LIBS_DIR%/zlib ../..
    ninja
    cd ..\\..\\..
""")

if buildQt5:
    stage('qt_5_15_11', """
    git clone https://github.com/qt/qt5.git qt_5_15_11
    cd qt_5_15_11
    perl init-repository --module-subset=qtbase,qtimageformats,qtsvg
    git checkout v5.15.11-lts-lgpl
    git submodule update qtbase qtimageformats qtsvg
depends:patches/qtbase_5.15.11/*.patch
    cd qtbase
win:
    for /r %%i in (..\\..\\patches\\qtbase_5.15.11\\*) do git apply %%i -v
    cd ..

    SET CONFIGURATIONS=-debug
release:
    SET CONFIGURATIONS=-debug-and-release
win:
    """ + removeDir("\"%LIBS_DIR%\\Qt-5.15.11\"") + """
    SET ANGLE_DIR=%LIBS_DIR%\\tg_angle
    SET ANGLE_LIBS_DIR=%ANGLE_DIR%\\out
    SET MOZJPEG_DIR=%LIBS_DIR%\\mozjpeg
    SET OPENSSL_DIR=%LIBS_DIR%\\openssl
    SET OPENSSL_LIBS_DIR=%OPENSSL_DIR%\\out
    SET ZLIB_LIBS_DIR=%LIBS_DIR%\\zlib
    SET WEBP_DIR=%LIBS_DIR%\\libwebp
    configure -prefix "%LIBS_DIR%\\Qt-5.15.11" ^
        %CONFIGURATIONS% ^
        -force-debug-info ^
        -opensource ^
        -confirm-license ^
        -static ^
        -static-runtime ^
        -opengl es2 -no-angle ^
        -I "%ANGLE_DIR%\\include" ^
        -D "KHRONOS_STATIC=" ^
        -D "DESKTOP_APP_QT_STATIC_ANGLE=" ^
        QMAKE_LIBS_OPENGL_ES2_DEBUG="%ANGLE_LIBS_DIR%\\Debug\\tg_angle.lib %ZLIB_LIBS_DIR%\\Debug\\zlibstaticd.lib d3d9.lib dxgi.lib dxguid.lib" ^
        QMAKE_LIBS_OPENGL_ES2_RELEASE="%ANGLE_LIBS_DIR%\\Release\\tg_angle.lib %ZLIB_LIBS_DIR%\\Release\\zlibstatic.lib d3d9.lib dxgi.lib dxguid.lib" ^
        -egl ^
        QMAKE_LIBS_EGL_DEBUG="%ANGLE_LIBS_DIR%\\Debug\\tg_angle.lib %ZLIB_LIBS_DIR%\\Debug\\zlibstaticd.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
        QMAKE_LIBS_EGL_RELEASE="%ANGLE_LIBS_DIR%\\Release\\tg_angle.lib %ZLIB_LIBS_DIR%\\Release\\zlibstatic.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
        -openssl-linked ^
        -I "%OPENSSL_DIR%\\include" ^
        OPENSSL_LIBS_DEBUG="%OPENSSL_LIBS_DIR%.dbg\\libssl.lib %OPENSSL_LIBS_DIR%.dbg\\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
        OPENSSL_LIBS_RELEASE="%OPENSSL_LIBS_DIR%\\libssl.lib %OPENSSL_LIBS_DIR%\\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
        -I "%MOZJPEG_DIR%" ^
        LIBJPEG_LIBS_DEBUG="%MOZJPEG_DIR%\\Debug\\jpeg-static.lib" ^
        LIBJPEG_LIBS_RELEASE="%MOZJPEG_DIR%\\Release\\jpeg-static.lib" ^
        -system-webp ^
        -I "%WEBP_DIR%\\src" ^
        -L "%WEBP_DIR%\\out\\release-static\\$X8664\\lib" ^
        -mp ^
        -no-feature-netlistmgr ^
        -nomake examples ^
        -nomake tests ^
        -platform win32-msvc

    jom -j16
    jom -j16 install
mac:
    find ../../patches/qtbase_5.15.11 -type f -print0 | sort -z | xargs -0 git apply
    cd ..

    CONFIGURATIONS=-debug
release:
    CONFIGURATIONS=-debug-and-release
mac:
    ./configure -prefix "$USED_PREFIX/Qt-5.15.11" \
        $CONFIGURATIONS \
        -force-debug-info \
        -opensource \
        -confirm-license \
        -static \
        -opengl desktop \
        -no-openssl \
        -securetransport \
        -I "$USED_PREFIX/include" \
        LIBJPEG_LIBS="$USED_PREFIX/lib/libjpeg.a" \
        ZLIB_LIBS="$USED_PREFIX/lib/libz.a" \
        -nomake examples \
        -nomake tests \
        -platform macx-clang

    make $MAKE_THREADS_CNT
    make install
""")

if buildQt6:
    stage('qt_6_2_6', """
mac:
    git clone -b v6.2.6-lts-lgpl https://code.qt.io/qt/qt5.git qt_6_2_6
    cd qt_6_2_6
    perl init-repository --module-subset=qtbase,qtimageformats,qtsvg
depends:patches/qtbase_6.2.6/*.patch
    cd qtbase
    find ../../patches/qtbase_6.2.6 -type f -print0 | sort -z | xargs -0 git apply -v
    cd ..
    sed -i.bak 's/tqtc-//' {qtimageformats,qtsvg}/dependencies.yaml

    CONFIGURATIONS=-debug
release:
    CONFIGURATIONS=-debug-and-release
mac:
    ./configure -prefix "$USED_PREFIX/Qt-6.2.6" \
        $CONFIGURATIONS \
        -force-debug-info \
        -opensource \
        -confirm-license \
        -static \
        -opengl desktop \
        -no-openssl \
        -securetransport \
        -system-webp \
        -I "$USED_PREFIX/include" \
        -no-feature-futimens \
        -nomake examples \
        -nomake tests \
        -platform macx-clang -- \
        -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
        -DCMAKE_PREFIX_PATH="$USED_PREFIX"

    ninja
    ninja install
""")

stage('tg_owt', """
    git clone https://github.com/desktop-app/tg_owt.git
    cd tg_owt
    git checkout be153adaa3
    git submodule init
    git submodule update
win:
    SET MOZJPEG_PATH=$LIBS_DIR/mozjpeg
    SET OPUS_PATH=$USED_PREFIX/include/opus
    SET OPENSSL_PATH=$LIBS_DIR/openssl/include
    SET LIBVPX_PATH=$USED_PREFIX/include
    SET FFMPEG_PATH=$LIBS_DIR/ffmpeg
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$OPENSSL_PATH \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_LIBVPX_INCLUDE_PATH=$LIBVPX_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
release:
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$OPENSSL_PATH \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_LIBVPX_INCLUDE_PATH=$LIBVPX_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
mac:
    MOZJPEG_PATH=$USED_PREFIX/include
    OPUS_PATH=$USED_PREFIX/include/opus
    LIBVPX_PATH=$USED_PREFIX/include
    FFMPEG_PATH=$USED_PREFIX/include
    mkdir out
    cd out
    mkdir Debug.x86_64
    cd Debug.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_LIBVPX_INCLUDE_PATH=$LIBVPX_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Debug.arm64
    cd Debug.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_LIBVPX_INCLUDE_PATH=$LIBVPX_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Debug
    lipo -create Debug.arm64/libtg_owt.a Debug.x86_64/libtg_owt.a -output Debug/libtg_owt.a
release:
    mkdir Release.x86_64
    cd Release.x86_64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_LIBVPX_INCLUDE_PATH=$LIBVPX_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Release.arm64
    cd Release.arm64
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_LIBVPX_INCLUDE_PATH=$LIBVPX_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
    cd ..
    mkdir Release
    lipo -create Release.arm64/libtg_owt.a Release.x86_64/libtg_owt.a -output Release/libtg_owt.a
""")

stage('protobuf', """
win:
    git clone --recursive -b v21.9 https://github.com/protocolbuffers/protobuf
    cd protobuf
    git clone https://github.com/abseil/abseil-cpp third_party/abseil-cpp
    cd third_party/abseil-cpp
    git checkout 273292d1cf
    cd ../..
    mkdir build
    cd build
    cmake .. ^
        -A %WIN32X64% ^
        -Dprotobuf_BUILD_TESTS=OFF ^
        -Dprotobuf_BUILD_PROTOBUF_BINARIES=ON ^
        -Dprotobuf_BUILD_LIBPROTOC=ON ^
        -Dprotobuf_WITH_ZLIB_DEFAULT=OFF ^
        -Dprotobuf_DEBUG_POSTFIX=""
    cmake --build . --config Release --parallel
    cmake --build . --config Debug --parallel
""")
# mac:
#     git clone --recursive -b v21.9 https://github.com/protocolbuffers/protobuf
#     cd protobuf
#     git clone https://github.com/abseil/abseil-cpp third_party/abseil-cpp
#     cd third_party/abseil-cpp
#     git checkout 273292d1cf
#     cd ../..
#     mkdir build
#     cd build
#     CFLAGS="$UNGUARDED" CPPFLAGS="$UNGUARDED" cmake .. \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \
#         -Dprotobuf_BUILD_TESTS=OFF \
#         -Dprotobuf_BUILD_PROTOBUF_BINARIES=ON \
#         -Dprotobuf_BUILD_LIBPROTOC=ON \
#         -Dprotobuf_WITH_ZLIB_DEFAULT=OFF
#     cmake --build . $MAKE_THREADS_CNT

if win:
    currentCodePage = subprocess.run('chcp', capture_output=True, shell=True, text=True, env=modifiedEnv).stdout.strip().split()[-1]
    subprocess.run('chcp 65001 > nul', shell=True, env=modifiedEnv)
    runStages()
    subprocess.run('chcp ' + currentCodePage + ' > nul', shell=True, env=modifiedEnv)
else:
    runStages()
