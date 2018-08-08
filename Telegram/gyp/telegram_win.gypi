# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'conditions': [[ 'build_win', {
    'msbuild_toolset': 'v141',
    'library_dirs': [
      '<(libs_loc)/ffmpeg',
    ],
    'libraries': [
      '-lzlibstat',
      '-lLzmaLib',
      '-lUxTheme',
      '-lDbgHelp',
      '-lOpenAL32',
      '-lcommon',
      '-lopus',
      'windows/common',
      'windows/handler/exception_handler',
      'windows/crash_generation/crash_generation_client',
    ],
    'msvs_settings': {
      'VCLinkerTool': {
        'AdditionalOptions': [
          'libavformat/libavformat.a',
          'libavcodec/libavcodec.a',
          'libavutil/libavutil.a',
          'libswresample/libswresample.a',
          'libswscale/libswscale.a',
        ],
      },
    },
    'configurations': {
      'Debug': {
        'library_dirs': [
          '<(libs_loc)/lzma/C/Util/LzmaLib/Debug',
          '<(libs_loc)/opus/win32/VS2015/Win32/Debug',
          '<(libs_loc)/openal-soft/build/Debug',
          '<(libs_loc)/zlib/contrib/vstudio/vc14/x86/ZlibStatDebug',
          '<(libs_loc)/breakpad/src/out/Debug/obj/client',
        ],
      },
      'Release': {
        'library_dirs': [
          '<(libs_loc)/lzma/C/Util/LzmaLib/Release',
          '<(libs_loc)/opus/win32/VS2015/Win32/Release',
          '<(libs_loc)/openal-soft/build/Release',
          '<(libs_loc)/zlib/contrib/vstudio/vc14/x86/ZlibStatReleaseWithoutAsm',
          '<(libs_loc)/breakpad/src/out/Release/obj/client',
        ],
      },
    },
  }], [ 'build_uwp', {
    'defines': [
      'TDESKTOP_DISABLE_AUTOUPDATE',
      'OS_WIN_STORE',
    ]
  }]],
}
