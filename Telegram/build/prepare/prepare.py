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

win = (sys.platform == 'win32')
mac = (sys.platform == 'darwin')
win32 = win and (os.environ['Platform'] == 'x86')
win64 = win and (os.environ['Platform'] == 'x64')

if win and not 'COMSPEC' in os.environ:
    error('COMSPEC environment variable is not set.')

if win and not win32 and not win64:
    error('Make sure to run from Native Tools Command Prompt.')

os.chdir(scriptPath + '/../../../..')

dirSep = '\\' if win else '/'
pathSep = ';' if win else ':'
libsLoc = 'Libraries' if not win64 else 'Libraries/win64'
keysLoc = 'cache_keys'

rootDir = os.getcwd()
libsDir = rootDir + dirSep + libsLoc
thirdPartyDir = rootDir + dirSep + 'ThirdParty'
usedPrefix = libsDir + dirSep + 'local'

processedArgs = []
skipReleaseBuilds = False
for arg in sys.argv[1:]:
    if arg == 'skip-release':
        processedArgs.append(arg)
        skipReleaseBuilds = True

if not os.path.isdir(libsDir + '/' + keysLoc):
    pathlib.Path(libsDir + '/' + keysLoc).mkdir(parents=True, exist_ok=True)
if not os.path.isdir(thirdPartyDir + '/' + keysLoc):
    pathlib.Path(thirdPartyDir + '/' + keysLoc).mkdir(parents=True, exist_ok=True)

pathPrefixes = [
    'ThirdParty\\Strawberry\\perl\\bin',
    'ThirdParty\\Python27',
    'ThirdParty\\NASM',
    'ThirdParty\\jom',
    'ThirdParty\\cmake\\bin',
    'ThirdParty\\yasm',
    'ThirdParty\\gyp',
    'ThirdParty\\Ninja',
] if win else [
    'ThirdParty/gyp',
    'ThirdParty/yasm',
    'ThirdParty/depot_tools',
]
pathPrefix = ''
for singlePrefix in pathPrefixes:
    pathPrefix = pathPrefix + rootDir + dirSep + singlePrefix + pathSep

environment = {
    'MAKE_THREADS_CNT': '-j8',
    'MACOSX_DEPLOYMENT_TARGET': '10.12',
    'UNGUARDED': '-Werror=unguarded-availability-new',
    'MIN_VER': '-mmacosx-version-min=10.12',
    'USED_PREFIX': usedPrefix,
    'ROOT_DIR': rootDir,
    'LIBS_DIR': libsDir,
    'SPECIAL_TARGET': 'win' if win32 else 'win64' if win64 else 'mac',
    'X8664': 'x86' if win32 else 'x64',
    'WIN32X64': 'Win32' if win32 else 'x64',
    'PATH_PREFIX': pathPrefix,
}
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
        pathlist = glob.glob(libsDir + '/' + pattern)
        items = [pattern]
        if len(pathlist) == 0:
            pathlist = glob.glob(thirdPartyDir + '/' + pattern)
        if len(pathlist) == 0:
            error('Nothing found: ' + pattern)
        for path in pathlist:
            if not os.path.exists(path):
                error('Not found: ' + path)
            items.append(computeFileHash(path))
        objects.append(':'.join(items))
    return hashlib.sha1(';'.join(objects).encode('utf-8')).hexdigest()

def keyPath(stage):
    return stage['directory'] + '/' + keysLoc + '/' + stage['name']

def checkCacheKey(stage):
    if not 'key' in stage:
        error('Key not set in stage: ' + stage['name'])
    key = keyPath(stage)
    if not os.path.exists(stage['directory'] + '/' + stage['name']):
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
                if skipReleaseBuilds:
                    inscope = False
                elif len(scopes) == 1:
                    continue
            skip = inscope if m.group(1) == '!' else not inscope
        elif not skip:
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
        if re.search(r'\$', command):
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

def run(command):
    print('---------------------------------COMMANDS-LIST----------------------------------')
    print(command, end='')
    print('--------------------------------------------------------------------------------')
    if win:
        if os.path.exists("command.bat"):
            os.remove("command.bat")
        with open("command.bat", 'w') as file:
            file.write('@echo OFF\r\n' + winFailOnEach(command))
        result = subprocess.run("command.bat", shell=True, env=modifiedEnv).returncode == 0
        if result and os.path.exists("command.bat"):
            os.remove("command.bat")
        return result
    elif re.search(r'\%', command):
        error('Bad command: ' + command)
    else:
        return subprocess.run("set -e\n" + command, shell=True, env=modifiedEnv).returncode == 0

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
        if arg in processedArgs:
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
                print('(r)ebuild, rebuild (a)ll, (s)kip, (q)uit?: ', end='', flush=True)
                while True:
                    ch = 'r' if rebuildStale else getch()
                    if ch == 'q':
                        finish(0)
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
        if checkResult == 'Skip':
            print('SKIPPING')
            continue
        clearCacheKey(stage)
        print('BUILDING:')
        os.chdir(stage['directory'])
        commands = removeDir(stage['name']) + '\n' + stage['commands']
        if not run(commands):
            print(prefix + ': FAILED')
            finish(1)
        writeCacheKey(stage)

stage('patches', """
    git clone https://github.com/desktop-app/patches.git
    cd patches
    git checkout 1a1d9e6d2c
""")

stage('depot_tools', """
mac:
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
""", 'ThirdParty')

stage('gyp', """
win:
    git clone https://chromium.googlesource.com/external/gyp
    cd gyp
    git checkout d6c5dd51dc
depends:patches/gyp.diff
    git apply $LIBS_DIR/patches/gyp.diff
mac:
    python3 -m pip install git+https://github.com/nodejs/gyp-next@v0.10.0
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
    git clone -b v5.2.5 https://git.tukaani.org/xz.git
    cd xz
    CFLAGS="$UNGUARDED" CPPFLAGS="$UNGUARDED" cmake -B build . \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D CMAKE_INSTALL_PREFIX:STRING=$USED_PREFIX
    cmake --build build $MAKE_THREADS_CNT
    cmake --install build
""")

stage('zlib', """
    git clone https://github.com/desktop-app/zlib.git
    cd zlib
win:
    cd contrib\\vstudio\\vc14
    msbuild zlibstat.vcxproj /property:Configuration=Debug /property:Platform="%X8664%"
release:
    msbuild zlibstat.vcxproj /property:Configuration=ReleaseWithoutAsm /property:Platform="%X8664%"
mac:
    CFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure \\
        --prefix=$USED_PREFIX
    make $MAKE_THREADS_CNT
    make install
""")

stage('mozjpeg', """
    git clone -b v4.0.1-rc2 https://github.com/mozilla/mozjpeg.git
    cd mozjpeg
win:
    cmake . ^
        -G "Visual Studio 16 2019" ^
        -A %WIN32X64% ^
        -DWITH_JPEG8=ON ^
        -DPNG_SUPPORTED=OFF
    cmake --build . --config Debug
release:
    cmake --build . --config Release
mac:
    cmake -B build . \\
        -D CMAKE_BUILD_TYPE=Release \\
        -D CMAKE_INSTALL_PREFIX=$USED_PREFIX \\
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET \\
        -D WITH_JPEG8=ON \\
        -D PNG_SUPPORTED=OFF
    cmake --build build $MAKE_THREADS_CNT
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
    ./Configure --prefix=$USED_PREFIX no-shared no-tests darwin64-x86_64-cc $MIN_VER
    make build_libs $MAKE_THREADS_CNT
""")

stage('opus', """
    git clone -b td-v1.3.1 https://github.com/telegramdesktop/opus.git
    cd opus
win:
    cd win32\\VS2015
    msbuild opus.sln /property:Configuration=Debug /property:Platform="%WIN32X64%"
release:
    msbuild opus.sln /property:Configuration=Release /property:Platform="%WIN32X64%"
mac:
    ./autogen.sh
    CFLAGS="$MIN_VER $UNGUARDED" CPPFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure --prefix=$USED_PREFIX
    make $MAKE_THREADS_CNT
    make install
""")

stage('rnnoise', """
    git clone https://github.com/desktop-app/rnnoise.git
    cd rnnoise
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
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ../..
    ninja
release:
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ../..
    ninja
""")

stage('libiconv', """
mac:
    VERSION=1.16
    rm -f libiconv.tar.gz
    wget -O libiconv.tar.gz https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$VERSION.tar.gz
    rm -rf libiconv-$VERSION
    tar -xvzf libiconv.tar.gz
    rm libiconv.tar.gz
    mv libiconv-$VERSION libiconv
    cd libiconv
    CFLAGS="$MIN_VER $UNGUARDED" CPPFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure --enable-static --prefix=$USED_PREFIX
    make $MAKE_THREADS_CNT
    make install
""")

stage('ffmpeg', """
    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/4.4
win:
    SET PATH_BACKUP_=%PATH%
    SET PATH=%ROOT_DIR%\\ThirdParty\\msys64\\usr\\bin;%PATH%

    set CHERE_INVOKING=enabled_from_arguments
    set MSYS2_PATH_TYPE=inherit

depends:patches/build_ffmpeg_win.sh
    bash --login ../patches/build_ffmpeg_win.sh

    SET PATH=%PATH_BACKUP_%
mac:
    CFLAGS=`freetype-config --cflags`
    LDFLAGS=`freetype-config --libs`
    PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/X11/lib/pkgconfig

depends:yasm/yasm
    ./configure --prefix=$USED_PREFIX \
    --extra-cflags="$MIN_VER $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1" \
    --extra-cxxflags="$MIN_VER $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1" \
    --extra-ldflags="$MIN_VER" \
    --enable-protocol=file \
    --enable-libopus \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-everything \
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
    --enable-decoder=alac \
    --enable-decoder=alac_at \
    --enable-decoder=flac \
    --enable-decoder=gif \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
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
    --enable-decoder=wavpack \
    --enable-decoder=wmalossless \
    --enable-decoder=wmapro \
    --enable-decoder=wmav1 \
    --enable-decoder=wmav2 \
    --enable-decoder=wmavoice \
    --enable-encoder=libopus \
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
    --enable-demuxer=m4v \
    --enable-demuxer=mov \
    --enable-demuxer=mp3 \
    --enable-demuxer=ogg \
    --enable-demuxer=wav \
    --enable-muxer=ogg \
    --enable-muxer=opus

    make $MAKE_THREADS_CNT
    make install
""")

stage('openal-soft', """
    git clone -b wasapi_exact_device_time https://github.com/telegramdesktop/openal-soft.git
    cd openal-soft
    cd build
win:
    cmake .. ^
        -G "Visual Studio 16 2019" ^
        -A %WIN32X64% ^
        -D LIBTYPE:STRING=STATIC ^
        -D FORCE_STATIC_VCRT=ON
    msbuild OpenAL.vcxproj /property:Configuration=Debug /property:Platform="%WIN32X64%"
release:
    msbuild OpenAL.vcxproj /property:Configuration=RelWithDebInfo /property:Platform="%WIN32X64%"
mac:
    CFLAGS=$UNGUARDED CPPFLAGS=$UNGUARDED cmake \
        -D CMAKE_INSTALL_PREFIX:PATH=$USED_PREFIX \
        -D ALSOFT_EXAMPLES=OFF \
        -D LIBTYPE:STRING=STATIC \
        -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=$MACOSX_DEPLOYMENT_TARGET ..
    make $MAKE_THREADS_CNT
    make install
""")

stage('breakpad', """
    git clone https://chromium.googlesource.com/breakpad/breakpad
    cd breakpad
    git checkout bc8fb886
depends:patches/breakpad.diff
    git apply ../patches/breakpad.diff
    git clone https://github.com/google/googletest src/testing
win:
    if "%X8664%" equ "x64" (
        set "FolderPostfix=_x64"
    ) else (
        set "FolderPostfix="
    )
    cd src\\client\\windows
    gyp --no-circular-check breakpad_client.gyp --format=ninja
    cd ..\\..
    ninja -C out/Debug%FolderPostfix% common crash_generation_client exception_handler
release:
    ninja -C out/Release%FolderPostfix% common crash_generation_client exception_handler
    cd tools\\windows\\dump_syms
    gyp dump_syms.gyp --format=ninja
    cd ..\\..\\..
    ninja -C out/Release%FolderPostfix% dump_syms
mac:
    git clone https://chromium.googlesource.com/linux-syscall-support src/third_party/lss
    cd src/third_party/lss
    git checkout a91633d1
    cd ../../..
    cd src/client/mac
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Debug build
release:
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Release build
    cd ../../tools/mac/dump_syms
    xcodebuild -project dump_syms.xcodeproj -target dump_syms -configuration Release build
    cd ../../../build
    ./gyp_breakpad
    cd ../processor
    xcodebuild -project processor.xcodeproj -target minidump_stackwalk -configuration Release build
""")

stage('crashpad', """
mac:
    git clone https://chromium.googlesource.com/crashpad/crashpad.git
    cd crashpad
    git checkout feb3aa3923
depends:patches/crashpad.diff
    git apply ../patches/crashpad.diff
    cd third_party/mini_chromium
    git clone https://chromium.googlesource.com/chromium/mini_chromium
    cd mini_chromium
    git checkout 7c5b0c1ab4
depends:patches/mini_chromium.diff
    git apply ../../../../patches/mini_chromium.diff
    cd ../../gtest
    git clone https://chromium.googlesource.com/external/github.com/google/googletest gtest
    cd gtest
    git checkout d62d6c6556
    cd ../../..

    python3 build/gyp_crashpad.py -Dmac_deployment_target=10.10
    ninja -C out/Debug base crashpad_util crashpad_client crashpad_handler
release:
    ninja -C out/Release base crashpad_util crashpad_client crashpad_handler
""")

stage('tg_angle', """
win:
    git clone https://github.com/desktop-app/tg_angle.git
    cd tg_angle
    git checkout ec51cc6
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

stage('qt_5_15_2', """
    git clone git://code.qt.io/qt/qt5.git qt_5_15_2
    cd qt_5_15_2
    perl init-repository --module-subset=qtbase,qtimageformats,qtsvg
    git checkout v5.15.2
    git submodule update qtbase qtimageformats qtsvg
depends:patches/qtbase_5_15_2/*.patch
    cd qtbase
win:
    for /r %%i in (..\\..\\patches\\qtbase_5_15_2\\*) do git apply %%i
    cd ..

    SET CONFIGURATIONS=-debug-and-release
release:
    SET CONFIGURATIONS=-debug
win:
    SET ANGLE_DIR=%LIBS_DIR%\\tg_angle
    SET ANGLE_LIBS_DIR=%ANGLE_DIR%\\out
    SET MOZJPEG_DIR=%LIBS_DIR%\\mozjpeg
    SET OPENSSL_DIR=%LIBS_DIR%\\openssl
    SET OPENSSL_LIBS_DIR=%OPENSSL_DIR%\\out
    SET ZLIB_LIBS_DIR=%LIBS_DIR%\\zlib\\contrib\\vstudio\\vc14\\%X8664%
    configure -prefix "%LIBS_DIR%\\Qt-5.15.2" ^
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
        QMAKE_LIBS_OPENGL_ES2_DEBUG="%ANGLE_LIBS_DIR%\\Debug\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatDebug\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib" ^
        QMAKE_LIBS_OPENGL_ES2_RELEASE="%ANGLE_LIBS_DIR%\\Release\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatReleaseWithoutAsm\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib" ^
        -egl ^
        QMAKE_LIBS_EGL_DEBUG="%ANGLE_LIBS_DIR%\\Debug\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatDebug\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
        QMAKE_LIBS_EGL_RELEASE="%ANGLE_LIBS_DIR%\\Release\\tg_angle.lib %ZLIB_LIBS_DIR%\ZlibStatReleaseWithoutAsm\zlibstat.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
        -openssl-linked ^
        -I "%OPENSSL_DIR%\include" ^
        OPENSSL_LIBS_DEBUG="%OPENSSL_LIBS_DIR%.dbg\libssl.lib %OPENSSL_LIBS_DIR%.dbg\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
        OPENSSL_LIBS_RELEASE="%OPENSSL_LIBS_DIR%\libssl.lib %OPENSSL_LIBS_DIR%\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
        -I "%MOZJPEG_DIR%" ^
        LIBJPEG_LIBS_DEBUG="%MOZJPEG_DIR%\Debug\jpeg-static.lib" ^
        LIBJPEG_LIBS_RELEASE="%MOZJPEG_DIR%\Release\jpeg-static.lib" ^
        -mp ^
        -nomake examples ^
        -nomake tests ^
        -platform win32-msvc

    jom -j16
    jom -j16 install
mac:
    find ../../patches/qtbase_5_15_2 -type f -print0 | sort -z | xargs -0 git apply
    cd ..

    CONFIGURATIONS=-debug-and-release
release:
    CONFIGURATIONS=-debug
mac:
    ./configure -prefix "$USED_PREFIX/Qt-5.15.2" \
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

stage('tg_owt', """
    git clone https://github.com/desktop-app/tg_owt.git
    cd tg_owt
    git checkout 91d836dc84
    git submodule init
    git submodule update src/third_party/libvpx/source/libvpx src/third_party/libyuv
win:
    SET MOZJPEG_PATH=$LIBS_DIR/mozjpeg
    SET OPUS_PATH=$LIBS_DIR/opus/include
    SET FFMPEG_PATH=$LIBS_DIR/ffmpeg
mac:
    MOZJPEG_PATH=$USED_PREFIX/include
    OPUS_PATH=$USED_PREFIX/include/opus
    FFMPEG_PATH=$USED_PREFIX/include
common:
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
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
        -DTG_OWT_OPENSSL_INCLUDE_PATH=$LIBS_DIR/openssl/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH ../..
    ninja
""")

runStages()
