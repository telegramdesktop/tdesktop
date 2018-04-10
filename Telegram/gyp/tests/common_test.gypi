# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    '../common_executable.gypi',
    '../qt.gypi',
  ],
  'include_dirs': [
    '<(src_loc)',
    '<(submodules_loc)/GSL/include',
    '<(submodules_loc)/variant/include',
    '<(submodules_loc)/Catch/include',
    '<(libs_loc)/range-v3/include',
  ],
  'sources': [
    '<(src_loc)/base/tests_main.cpp',
  ],
}
