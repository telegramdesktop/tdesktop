# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'variables': {
    'qrc_files': [
      '<(res_loc)/qrc/telegram.qrc',
      '<(res_loc)/qrc/telegram_emoji_1.qrc',
      '<(res_loc)/qrc/telegram_emoji_2.qrc',
      '<(res_loc)/qrc/telegram_emoji_3.qrc',
      '<(res_loc)/qrc/telegram_emoji_4.qrc',
      '<(res_loc)/qrc/telegram_emoji_5.qrc',
      '<(res_loc)/qrc/telegram_sounds.qrc',
    ],
  },
  'conditions': [
    [ 'build_linux', {
      'variables': {
        'qrc_files': [
          '<(res_loc)/qrc/telegram_linux.qrc',
        ],
      }
    }],
    [ 'build_mac', {
      'variables': {
        'qrc_files': [
          '<(res_loc)/qrc/telegram_mac.qrc',
        ],
      },
    }],
    [ 'build_win', {
      'variables': {
        'qrc_files': [
          '<(res_loc)/qrc/telegram_wnd.qrc',
        ],
      }
    }],
  ],
}
