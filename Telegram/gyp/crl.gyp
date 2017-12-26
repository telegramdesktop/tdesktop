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
  'includes': [
    'common.gypi',
  ],
  'targets': [{
    'target_name': 'crl',
    'type': 'static_library',
    'dependencies': [],
    'includes': [
      'common.gypi',
      'qt.gypi',
    ],
    'defines': [
    ],
    'conditions': [[ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }]],
    'variables': {
      'crl_src_loc': '../ThirdParty/crl/src/crl',
      'official_build_target%': '',
    },
    'include_dirs': [
      '../ThirdParty/crl/src',
    ],
    'sources': [
      '<(crl_src_loc)/common/crl_common_config.h',
      '<(crl_src_loc)/common/crl_common_list.cpp',
      '<(crl_src_loc)/common/crl_common_list.h',
      '<(crl_src_loc)/common/crl_common_on_main.cpp',
      '<(crl_src_loc)/common/crl_common_on_main.h',
      '<(crl_src_loc)/common/crl_common_queue.cpp',
      '<(crl_src_loc)/common/crl_common_queue.h',
      '<(crl_src_loc)/common/crl_common_sync.h',
      '<(crl_src_loc)/common/crl_common_utils.h',
      '<(crl_src_loc)/dispatch/crl_dispatch_async.cpp',
      '<(crl_src_loc)/dispatch/crl_dispatch_async.h',
      '<(crl_src_loc)/dispatch/crl_dispatch_on_main.h',
      '<(crl_src_loc)/dispatch/crl_dispatch_queue.cpp',
      '<(crl_src_loc)/dispatch/crl_dispatch_queue.h',
      '<(crl_src_loc)/dispatch/crl_dispatch_semaphore.cpp',
      '<(crl_src_loc)/dispatch/crl_dispatch_semaphore.h',
      '<(crl_src_loc)/qt/crl_qt_async.cpp',
      '<(crl_src_loc)/qt/crl_qt_async.h',
      '<(crl_src_loc)/qt/crl_qt_semaphore.cpp',
      '<(crl_src_loc)/qt/crl_qt_semaphore.h',
      '<(crl_src_loc)/winapi/crl_winapi_async.cpp',
      '<(crl_src_loc)/winapi/crl_winapi_async.h',
      '<(crl_src_loc)/winapi/crl_winapi_dll.h',
      '<(crl_src_loc)/winapi/crl_winapi_list.cpp',
      '<(crl_src_loc)/winapi/crl_winapi_list.h',
      '<(crl_src_loc)/winapi/crl_winapi_semaphore.cpp',
      '<(crl_src_loc)/winapi/crl_winapi_semaphore.h',
      '<(crl_src_loc)/crl.h',
      '<(crl_src_loc)/crl_async.h',
      '<(crl_src_loc)/crl_on_main.h',
      '<(crl_src_loc)/crl_queue.h',
      '<(crl_src_loc)/crl_semaphore.h',
    ],
  }],
}
