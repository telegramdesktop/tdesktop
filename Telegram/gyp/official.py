'''
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
'''
import sys
import os
import re

my_path = os.path.dirname(os.path.realpath(__file__)).replace('\\', '/')

if len(sys.argv) > 1 and sys.argv[1] == '--read-target':
  target_path = my_path + '/../build/target'
  if os.path.isfile(target_path):
    with open(target_path) as f:
      for line in f:
        cleanline = re.sub(r'^\s*|\s*$', '', line);
        if cleanline != '':
          print(cleanline);
else:
  print('This is a helper script, it should not be called directly.')
