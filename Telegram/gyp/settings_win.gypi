# This file is part of Telegram Desktop,
# the official desktop version of Telegram messaging app, see https://telegram.org
#
# Telegram Desktop is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# It is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# In addition, as a special exception, the copyright holders give permission
# to link the code of portions of this program with the OpenSSL library.
#
# Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
# Copyright (c) 2014 John Preston, https://desktop.telegram.org

{
  'conditions': [
    [ 'build_win', {
      'msvs_cygwin_shell': 0,
      'msvs_settings': {
        'VCCLCompilerTool': {
          'ProgramDataBaseFileName': '$(OutDir)\\$(ProjectName).pdb',
          'DebugInformationFormat': '3',          # Program Database (/Zi)
          'AdditionalOptions': [
            '/MP',   # Enable multi process build.
            '/EHsc', # Catch C++ exceptions only, extern C functions never throw a C++ exception.
          ],
          'PreprocessorDefinitions': [
            'WIN32',
            '_WINDOWS',
            '_UNICODE',
            'UNICODE',
            'HAVE_STDINT_H',
            'ZLIB_WINAPI',
            '_SCL_SECURE_NO_WARNINGS',
          ],
          'TreatWChar_tAsBuiltInType': 'false',
        },
        'VCLinkerTool': {
          'SubSystem': '<(win_subsystem)',
          'ImageHasSafeExceptionHandlers': 'false',   # Disable /SAFESEH
        },
      },
      'libraries': [
        'winmm',
        'imm32',
        'ws2_32',
        'kernel32',
        'user32',
        'gdi32',
        'winspool',
        'comdlg32',
        'advapi32',
        'shell32',
        'ole32',
        'oleaut32',
        'uuid',
        'odbc32',
        'odbccp32',
#        'glu32',
#        'opengl32',
        'Shlwapi',
        'Iphlpapi',
        'Gdiplus',
        'Strmiids',
      ],

      'configurations': {
        'Debug': {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'Optimization': '0',                # Disabled (/Od)
              'PreprocessorDefinitions': [
              ],
              'RuntimeLibrary': '1',              # Multi-threaded Debug (/MTd)
            },
            'VCLinkerTool': {
              'GenerateDebugInformation': 'true', # true (/DEBUG)
              'LinkIncremental': '2',             # Yes (/INCREMENTAL)
              'AdditionalDependencies': [
                'msvcrtd.lib',
              ],
              'AdditionalOptions': [
                '/NODEFAULTLIB:LIBCMTD'
              ],
            },
          },
        },
        'Release': {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'Optimization': '2',                # Maximize Speed (/O2)
              'PreprocessorDefinitions': [
              ],
              'FavorSizeOrSpeed': '1',            # Favor fast code (/Ot)
              'RuntimeLibrary': '0',              # Multi-threaded (/MT)
              'EnableEnhancedInstructionSet': '2',# Streaming SIMD Extensions 2 (/arch:SSE2)
            },
            'VCLinkerTool': {
              'GenerateDebugInformation': 'true', # /DEBUG
              'AdditionalDependencies': [
                'msvcrt.lib',
              ],
              'AdditionalOptions': [
                '/NODEFAULTLIB:LIBCMT'
              ],
            },
          },
        },
      },
    }],
  ],
}
