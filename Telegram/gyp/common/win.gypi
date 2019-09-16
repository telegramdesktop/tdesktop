# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'conditions': [
    [ 'build_win', {
      'defines': [
        'WIN32',
        '_WINDOWS',
        '_UNICODE',
        'UNICODE',
        'HAVE_STDINT_H',
        'ZLIB_WINAPI',
        '_SCL_SECURE_NO_WARNINGS',
        '_USING_V110_SDK71_',
      ],
      'msbuild_toolset': 'v142',
      'msvs_cygwin_shell': 0,
      'msvs_settings': {
        'VCCLCompilerTool': {
          'ProgramDataBaseFileName': '$(OutDir)\\$(ProjectName).pdb',
          'DebugInformationFormat': '3',          # Program Database (/Zi)
          'WarnAsError': 'true',
          'AdditionalOptions': [
            '/std:<(build_standard_win)',
            '/permissive-',
            '/Qspectre',
            '/MP',     # Enable multi process build.
            '/EHsc',   # Catch C++ exceptions only, extern C functions never throw a C++ exception.
            '/w14834', # [[nodiscard]]
            '/w15038', # wrong initialization order
            '/w14265', # class has virtual functions, but destructor is not virtual
          ],
          'TreatWChar_tAsBuiltInType': 'false',
        },
        'VCLinkerTool': {
          'MinimumRequiredVersion': '5.01',
          'ImageHasSafeExceptionHandlers': 'false',   # Disable /SAFESEH
        },
      },
      'msvs_external_builder_build_cmd': [
        'ninja.exe',
        '-C',
        '$(OutDir)',
        '-k0',
        '$(ProjectName)',
      ],
      'libraries': [
        '-lwinmm',
        '-limm32',
        '-lws2_32',
        '-lkernel32',
        '-luser32',
        '-lgdi32',
        '-lwinspool',
        '-lcomdlg32',
        '-ladvapi32',
        '-lshell32',
        '-lole32',
        '-loleaut32',
        '-luuid',
        '-lodbc32',
        '-lodbccp32',
        '-lShlwapi',
        '-lIphlpapi',
        '-lGdiplus',
        '-lStrmiids',
      ],

      'configurations': {
        'Debug': {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'Optimization': '0',                # Disabled (/Od)
              'RuntimeLibrary': '1',              # Multi-threaded Debug (/MTd)
            },
            'VCLinkerTool': {
              'GenerateDebugInformation': 'true', # true (/DEBUG)
              'IgnoreDefaultLibraryNames': 'LIBCMT',
              'LinkIncremental': '2',             # Yes (/INCREMENTAL)
            },
          },
        },
        'Release': {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'Optimization': '2',                 # Maximize Speed (/O2)
              'InlineFunctionExpansion': '2',      # Any suitable (/Ob2)
              'EnableIntrinsicFunctions': 'true',  # Yes (/Oi)
              'FavorSizeOrSpeed': '1',             # Favor fast code (/Ot)
              'RuntimeLibrary': '0',               # Multi-threaded (/MT)
              'EnableEnhancedInstructionSet': '2', # Streaming SIMD Extensions 2 (/arch:SSE2)
              'WholeProgramOptimization': 'true',  # /GL
            },
            'VCLinkerTool': {
              'GenerateDebugInformation': 'true',  # /DEBUG
              'OptimizeReferences': '2',
              'LinkTimeCodeGeneration': '1',       # /LTCG
            },
            'VCLibrarianTool': {
              'LinkTimeCodeGeneration': 'true',    # /LTCG
            },
          },
        },
      },
      'conditions': [
        [ '"<(official_build_target)" != "" and "<(official_build_target)" != "win" and "<(official_build_target)" != "uwp"', {
          'sources': [ '__Wrong_Official_Build_Target__' ],
        }],
      ],
    }],
  ],
}
