# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'cmake_precompiled_header': '<(pch_header)',
  'cmake_precompiled_header_script': 'PrecompiledHeader.cmake',
  'msvs_precompiled_source': '<(pch_source)',
  'msvs_precompiled_header': '<(pch_header)',
  'xcode_settings': {
    'GCC_PREFIX_HEADER': '<(pch_header)',
    'GCC_PRECOMPILE_PREFIX_HEADER': 'YES',
  },
  'sources': [
    '<(pch_source)',
    '<(pch_header)',
  ],
}
