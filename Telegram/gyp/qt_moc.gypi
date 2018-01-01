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
  'rules': [{
    'rule_name': 'qt_moc',
    'extension': 'h',
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/moc/moc_<(RULE_INPUT_ROOT).cpp',
    ],
    'action': [
      '<(qt_loc)/bin/moc<(exe_ext)',

      # Silence "Note: No relevant classes found. No output generated."
      '--no-notes',

      '<!@(python -c "for s in \'<@(_defines)\'.split(\' \'): print(\'-D\' + s)")',
      # '<!@(python -c "for s in \'<@(_include_dirs)\'.split(\' \'): print(\'-I\' + s)")',
      '<(RULE_INPUT_PATH)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/moc/moc_<(RULE_INPUT_ROOT).cpp',
    ],
    'message': 'Moc-ing <(RULE_INPUT_ROOT).h..',
  }],
}
