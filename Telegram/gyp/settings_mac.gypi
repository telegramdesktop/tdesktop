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
    [ 'build_mac', {
      'variables': {
        'mac_frameworks': [
          'Cocoa',
          'CoreFoundation',
          'CoreServices',
          'CoreText',
          'CoreGraphics',
          'OpenGL',
          'VideoToolbox',
          'VideoDecodeAcceleration',
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
          'AVFoundation',
          'CoreMedia',
          'AppKit',
          'CoreWLAN',
          'IOKit',
        ],
      },
      'xcode_settings': {
		    'SYMROOT': '../../out',
        'OTHER_CPLUSPLUSFLAGS': [
          '-std=c++14',
          '-Wno-switch',
          '-stdlib=libc++',
        ],
        'OTHER_LDFLAGS': [
          '-stdlib=libc++',
          '<!@(python -c "for s in \'<@(mac_frameworks)\'.split(\' \'): print(\'-framework \' + s)")',
        ],
        'MACOSX_DEPLOYMENT_TARGET': '<(mac_target)',
      },
    }],
  ],
}
