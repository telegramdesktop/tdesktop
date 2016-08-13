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
import glob
import re
import os

# Generate custom environment.x86 for ninja
# We use msbuild.log to extract some variables
variables = [
  'TMP',
  'SYSTEMROOT',
  'TEMP',
  'LIB',
  'LIBPATH',
  'PATH',
  'PATHEXT',
  'INCLUDE',
]
var_values = {}
for var_name in variables:
  var_values[var_name] = os.environ[var_name]

next_contains_var = 0
with open('msbuild.log') as f:
  for line in f:
    if (re.match(r'^\s*Task "SetEnv"\s*$', line)):
      next_contains_var = 1
    elif next_contains_var:
      cleanline = re.sub(r'^\s*|\s*$', '', line)
      name_value_pair = re.match(r'^([A-Z]+)=(.+)$', cleanline)
      if name_value_pair:
        var_values[name_value_pair.group(1)] = name_value_pair.group(2)
      next_contains_var = 0

out = open('environment.x86', 'wb')
for var_name in variables:
  out.write(var_name + '=' + var_values[var_name] + '\0')
out.write('\0')
out.close()
