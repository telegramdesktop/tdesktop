# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'type': 'executable',
  'variables': {
    'win_subsystem': '2', # Windows application
  },
  'includes': [
    'common.gypi',
  ],
  'msvs_settings': {
    'VCLinkerTool': {
      'SubSystem': '<(win_subsystem)',
      'ImportLibrary': '<(PRODUCT_DIR)/<(_target_name).lib',
    },
  },
}
