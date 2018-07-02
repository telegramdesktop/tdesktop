# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'conditions': [
    [ 'build_mac', {
      'variables': {
        'mac_frameworks': [
          'Cocoa',
          'CoreFoundation',
          'CoreServices',
          'CoreText',
          'CoreGraphics',
          'OpenGL',
          'AudioUnit',
          'ApplicationServices',
          'Foundation',
          'AGL',
          'Security',
          'SystemConfiguration',
          'Carbon',
          'AudioToolbox',
          'CoreAudio',
          'QuartzCore',
          'AppKit',
          'CoreWLAN',
          'IOKit',
        ],
        'mac_common_flags': [
          '-pipe',
          '-g',
          '-Wall',
          '-Werror',
          '-W',
          '-fPIE',
          '-Wno-unused-variable',
          '-Wno-unused-parameter',
          '-Wno-unused-function',
          '-Wno-switch',
          '-Wno-comment',
          '-Wno-missing-field-initializers',
          '-Wno-sign-compare',
        ],
      },
      'xcode_settings': {
        'SYMROOT': '../../out',
        'OTHER_CFLAGS': [
          '<@(mac_common_flags)',
        ],
        'OTHER_CPLUSPLUSFLAGS': [
          '<@(mac_common_flags)',
        ],
        'OTHER_LDFLAGS': [
          '<!@(python -c "for s in \'<@(mac_frameworks)\'.split(\' \'): print(\'-framework \' + s)")',
        ],
        'MACOSX_DEPLOYMENT_TARGET': '<(mac_target)',
        'COMBINE_HIDPI_IMAGES': 'YES',
        'COPY_PHASE_STRIP': 'NO',
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++1z',
        'GCC_INLINES_ARE_PRIVATE_EXTERN': 'YES',
      },
      'configurations': {
        'Debug': {
          'xcode_settings': {
            'ENABLE_TESTABILITY': 'YES',
            'ONLY_ACTIVE_ARCH': 'YES',
          },
        },
      },
      'conditions': [
        [ '"<(official_build_target)" != "" and "<(official_build_target)" != "mac" and "<(official_build_target)" != "mac32" and "<(official_build_target)" != "macstore"', {
          'sources': [ '__Wrong_Official_Build_Target__' ],
        }],
      ],
    }],
    [ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [
          '-Wno-inconsistent-missing-override',
        ],
        'OTHER_LDFLAGS': [
          '-w', # Suppress 'libstdc++ is deprecated' warning.
        ],
      },
      'defines': [
        'RANGES_CXX_THREAD_LOCAL=0',
      ],
    }, {
      'xcode_settings': {
        'CLANG_CXX_LIBRARY': 'libc++',
        'CLANG_ENABLE_OBJC_WEAK': 'YES',
        'OTHER_LDFLAGS': [
          '-framework', 'VideoToolbox',
          '-framework', 'VideoDecodeAcceleration',
          '-framework', 'AVFoundation',
          '-framework', 'CoreMedia',
        ],
      },
    }],
  ],
}
