# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    'helpers/common/common.gypi',
  ],
  'targets': [{
    'target_name': 'Telegram',
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'minizip_loc': '<(third_party_loc)/minizip',
      'sp_media_key_tap_loc': '<(third_party_loc)/SPMediaKeyTap',
      'emoji_suggestions_loc': '<(third_party_loc)/emoji_suggestions',
      'style_files': [
        '<(src_loc)/boxes/boxes.style',
        '<(src_loc)/calls/calls.style',
        '<(src_loc)/dialogs/dialogs.style',
        '<(src_loc)/export/view/export.style',
        '<(src_loc)/history/history.style',
        '<(src_loc)/info/info.style',
        '<(src_loc)/intro/intro.style',
        '<(src_loc)/media/view/mediaview.style',
        '<(src_loc)/media/player/media_player.style',
        '<(src_loc)/overview/overview.style',
        '<(src_loc)/passport/passport.style',
        '<(src_loc)/profile/profile.style',
        '<(src_loc)/settings/settings.style',
        '<(src_loc)/chat_helpers/chat_helpers.style',
        '<(src_loc)/window/window.style',
      ],
      'dependent_style_files': [
        '<(submodules_loc)/lib_ui/ui/colors.palette',
        '<(submodules_loc)/lib_ui/ui/basic.style',
        '<(submodules_loc)/lib_ui/ui/layers/layers.style',
        '<(submodules_loc)/lib_ui/ui/widgets/widgets.style',
      ],
      'style_timestamp': '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
      'qrc_timestamp': '<(SHARED_INTERMEDIATE_DIR)/update_dependent_qrc.timestamp',
      'langpacks': [
        'en',
        'de',
        'es',
        'it',
        'nl',
        'ko',
        'pt-BR',
      ],
      'list_sources_command': 'python <(submodules_loc)/lib_base/gyp/list_sources.py --input <(DEPTH)/telegram/sources.txt --replace src_loc=<(src_loc)',
      'pch_source': '<(src_loc)/stdafx.cpp',
      'pch_header': '<(src_loc)/stdafx.h',
    },
    'includes': [
      'helpers/common/executable.gypi',
      'helpers/modules/openssl.gypi',
      'helpers/modules/qt.gypi',
      'helpers/modules/qt_moc.gypi',
      'helpers/modules/pch.gypi',
      '../lib_ui/gyp/qrc_rule.gypi',
      '../lib_ui/gyp/styles_rule.gypi',
      'telegram/qrc.gypi',
      'telegram/win.gypi',
      'telegram/mac.gypi',
      'telegram/linux.gypi',
      'codegen/rules.gypi',
    ],

    'dependencies': [
      '<(submodules_loc)/codegen/codegen.gyp:codegen_lang',
      '<(submodules_loc)/codegen/codegen.gyp:codegen_numbers',
      '<(submodules_loc)/codegen/codegen.gyp:codegen_style',
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
      '<(submodules_loc)/lib_ui/lib_ui.gyp:lib_ui',
      '<(submodules_loc)/lib_qr/lib_qr.gyp:lib_qr',
      '<(third_party_loc)/libtgvoip/libtgvoip.gyp:libtgvoip',
      '<(submodules_loc)/lib_lottie/lib_lottie.gyp:lib_lottie',
      'tests/tests.gyp:tests',
      'utils.gyp:Updater',
      'lib_export.gyp:lib_export',
      'lib_storage.gyp:lib_storage',
      'lib_ffmpeg.gyp:lib_ffmpeg',
      'lib_mtproto.gyp:lib_mtproto',
    ],

    'defines': [
      'AL_LIBTYPE_STATIC',
      'AL_ALEXT_PROTOTYPES',
      'TGVOIP_USE_CXX11_LIB',
      'TDESKTOP_API_ID=<(api_id)',
      'TDESKTOP_API_HASH=<(api_hash)',
      '<!@(python -c "for s in \'<(build_defines)\'.split(\',\'): print(s)")',
    ],

    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(libs_loc)/breakpad/src',
      '<(libs_loc)/lzma/C',
      '<(libs_loc)/openal-soft/include',
      '<(libs_loc)/opus/include',
      '<(minizip_loc)',
      '<(sp_media_key_tap_loc)',
      '<(emoji_suggestions_loc)',
    ],
    'sources': [
      '<@(qrc_files)',
      '<@(style_files)',
      '<(res_loc)/langs/cloud_lang.strings',
      '<(res_loc)/export_html/css/style.css',
      '<(res_loc)/export_html/js/script.js',
      '<(res_loc)/export_html/images/back.png',
      '<(res_loc)/export_html/images/back@2x.png',
      '<(DEPTH)/telegram/sources.txt',
      '<!@(<(list_sources_command) <(qt_moc_list_sources_arg))',
    ],
    'sources!': [
      '<!@(<(list_sources_command) <(qt_moc_list_sources_arg) --exclude_for <(build_os))',
    ],
    'conditions': [[ 'not build_osx', {
      'dependencies': [
        '<(submodules_loc)/lib_spellcheck/lib_spellcheck.gyp:lib_spellcheck',
      ],
    }, {
      'defines': [
        'TDESKTOP_DISABLE_SPELLCHECK',
      ],
    }], [ '"<(special_build_target)" != ""', {
      'defines': [
        'TDESKTOP_ALLOW_CLOSED_ALPHA',
        'TDESKTOP_FORCE_GTK_FILE_DIALOG',
      ],
      'dependencies': [
        'utils.gyp:Packer',
      ],
    }]],
  }],
}
