'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
