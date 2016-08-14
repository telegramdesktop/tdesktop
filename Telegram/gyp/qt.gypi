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
  'variables': {
    'variables': {
      'variables': {
        'qt_version%': '5.6.0',
      },
      'qt_version%': '<(qt_version)',
      'qt_version_loc': '<!(python -c "print(\'<(qt_version)\'.replace(\'.\', \'_\')))',
      'qtlibs_debug': [
        '<(ld_lib_prefix)Qt5Cored<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtmaind<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtpcred<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5Guid<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtfreetyped<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5Widgetsd<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtharfbuzzngd<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5Networkd<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5PlatformSupportd<(ld_lib_postfix)',
        '<(ld_lib_prefix)imageformats\qwebpd<(ld_lib_postfix)',
      ],
      'qtlibs_release': [
        '<(ld_lib_prefix)Qt5Core<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtmain<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtpcre<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5Gui<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtfreetype<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5Widgets<(ld_lib_postfix)',
        '<(ld_lib_prefix)qtharfbuzzng<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5Network<(ld_lib_postfix)',
        '<(ld_lib_prefix)Qt5PlatformSupport<(ld_lib_postfix)',
        '<(ld_lib_prefix)imageformats\qwebp<(ld_lib_postfix)',
      ],
    },
    'qt_version%': '<(qt_version)',
    'conditions': [
      [ 'build_win', {
        'qtlibs_debug': [
          '<(ld_lib_prefix)platforms/qwindowsd<(ld_lib_postfix)',
          '<@(qtlibs_debug)',
        ],
        'qtlibs_release': [
          '<(ld_lib_prefix)platforms/qwindows<(ld_lib_postfix)',
          '<@(qtlibs_release)',
        ],
        'qt_loc': '../../../Libraries/qt<(qt_version_loc)/qtbase',
      }, {
        'qtlibs_debug': [ '<@(qtlibs_debug)' ],
        'qtlibs_release': [ '<@(qtlibs_release)' ],
        'qt_loc': '/usr/local/qt<(qt_version_loc)/qtbase',
      }],
    ],
  },

  'configurations': {
    'Debug': {
      'conditions' : [
        [ 'build_win', {
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalDependencies': [
                '<@(qtlibs_debug)'
              ],
            },
          },
        }],
      ],
    },
    'Release': {
      'conditions' : [
        [ 'build_win', {
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalDependencies': [
                '<@(qtlibs_release)',
              ],
            },
          },
        }],
      ],
    },
  },

  'include_dirs': [
    '<(qt_loc)/include',
    '<(qt_loc)/include/QtCore/<(qt_version)/QtCore',
    '<(qt_loc)/include/QtGui/<(qt_version)/QtGui',
  ],
  'library_dirs': [
    '<(qt_loc)/lib',
    '<(qt_loc)/plugins',
  ],
  'defines': [
    'QT_WIDGETS_LIB',
    'QT_NETWORK_LIB',
    'QT_GUI_LIB',
    'QT_CORE_LIB',
  ],

  'rules': [{
    'rule_name': 'qt_moc',
    'extension': 'h',
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/moc/moc_<(RULE_INPUT_ROOT).cpp',
    ],
    'action': [
      '<(qt_loc)/bin/moc.exe',

      # Silence "Note: No relevant classes found. No output generated."
      '--no-notes',

      '<!@(python -c "for s in \'<@(_defines)\'.split(\' \'): print(\'-D\' + s))',
      # '<!@(python -c "for s in \'<@(_include_dirs)\'.split(\' \'): print(\'-I\' + s))',
      '<(RULE_INPUT_PATH)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/moc/moc_<(RULE_INPUT_ROOT).cpp',
    ],
    'message': 'Moc-ing <(RULE_INPUT_ROOT).h..',
    'process_outputs_as_sources': 1,
  }, {
    'rule_name': 'qt_rcc',
    'extension': 'qrc',
    'inputs': [
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_qrc.timestamp',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/qrc/qrc_<(RULE_INPUT_ROOT).cpp',
    ],
    'action': [
      '<(qt_loc)/bin/rcc.exe',
      '-name', '<(RULE_INPUT_ROOT)',
      '-no-compress',
      '<(RULE_INPUT_PATH)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/qrc/qrc_<(RULE_INPUT_ROOT).cpp',
    ],
    'message': 'Rcc-ing <(RULE_INPUT_ROOT).qrc..',
    'process_outputs_as_sources': 1,
  }],
}
