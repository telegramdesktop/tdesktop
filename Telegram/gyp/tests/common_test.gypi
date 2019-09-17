# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    '../../ThirdParty/gyp_helpers/common/executable.gypi',
    '../../ThirdParty/gyp_helpers/modules/qt.gypi',
  ],
  'dependencies': [
    '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
  ],
  'include_dirs': [
    '<(src_loc)',
    '<(submodules_loc)/Catch/include',
  ],
  'sources': [
    '<(submodules_loc)/lib_base/base/tests_main.cpp',
  ],
}
