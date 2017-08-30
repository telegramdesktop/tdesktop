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
from __future__ import print_function
import sys
import os
import re
import time
import codecs

def eprint(*args, **kwargs):
  print(*args, file=sys.stderr, **kwargs)
  sys.exit(1)

my_path = os.path.dirname(os.path.realpath(__file__)).replace('\\', '/')

next_input_path = 0
input_path = ''
write_sources = 0
next_self = 1
for arg in sys.argv:
  if next_self != 0:
    next_self = 0
    continue

  if arg == '--sources':
    write_sources = 1
    continue

  if arg == '--input':
    next_input_path = 1
    continue
  elif next_input_path == 1:
    next_input_path = 0
    input_path = arg
    continue

tests_names = []

if input_path != '':
  if not os.path.isfile(input_path):
    eprint('Input path not found.')
  else:
    with open(input_path, 'r') as f:
      for line in f:
        test_name = line.strip()
        if test_name[0:2] != '//' and test_name != '':
          tests_names.append(test_name)

if write_sources != 0:
  tests_path = my_path + '/';
  if not os.path.isdir(tests_path):
    os.mkdir(tests_path)
  for test_name in tests_names:
    test_path = tests_path + test_name + '.test'
    if not os.path.isfile(test_path):
      with open(test_path, 'w') as out:
        out.write('1')
    print(test_name + '.test')
else:
  for test_name in tests_names:
    print(test_name)
