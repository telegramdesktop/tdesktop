# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'conditions': [[ 'build_win', {
    'msvs_precompiled_source': '<(src_loc)/stdafx.cpp',
    'msvs_precompiled_header': '<(src_loc)/stdafx.h',
    'msbuild_toolset': 'v141',
    'sources': [
      '<(res_loc)/winrc/Telegram.rc',
    ],
    'library_dirs': [
      '<(libs_loc)/ffmpeg',
    ],
    'libraries': [
      '-llibeay32',
      '-lssleay32',
      '-lCrypt32',
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
        'include_dirs': [
          '<(libs_loc)/openssl/Debug/include',
        ],
        'library_dirs': [
          '<(libs_loc)/openssl/Debug/lib',
          '<(libs_loc)/lzma/C/Util/LzmaLib/Debug',
          '<(libs_loc)/opus/win32/VS2015/Win32/Debug',
          '<(libs_loc)/openal-soft/build/Debug',
          '<(libs_loc)/zlib/contrib/vstudio/vc14/x86/ZlibStatDebug',
          '<(libs_loc)/breakpad/src/out/Debug/obj/client',
        ],
      },
      'Release': {
        'include_dirs': [
          '<(libs_loc)/openssl/Release/include',
        ],
        'library_dirs': [
          '<(libs_loc)/openssl/Release/lib',
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
