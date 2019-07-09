'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

def check_non_empty_moc(file_path):
  if not os.path.isfile(file_path):
    return False
  if re.search(r'\.h$', file_path):
    with codecs.open(file_path, mode="r", encoding="utf-8") as f:
      for line in f:
        if re.search(r'(^|\s)Q_OBJECT(\s|$)', line):
          return True
  return False

def should_exclude(rules, exclude_for):
  for rule in rules:
    if rule[0:1] == '!':
      return (rule[1:] == exclude_for)
    elif rule == exclude_for:
      return False
  return len(rules) > 0

my_path = os.path.dirname(os.path.realpath(__file__)).replace('\\', '/')

file_paths = []
platform_rules = {}
next_input_path = 0
input_path = ''
next_moc_prefix = 0
moc_prefix = ''
next_replace = 0
replaces = []
next_exclude_for = 0
exclude_for = ''
next_self = 1
for arg in sys.argv:
  if next_self != 0:
    next_self = 0
    continue

  if arg == '--moc-prefix':
    next_moc_prefix = 1
    continue
  elif next_moc_prefix == 1:
    next_moc_prefix = 0
    moc_prefix = arg.replace('SHARED_INTERMEDIATE_DIR', '<(SHARED_INTERMEDIATE_DIR)')
    continue

  if arg == '--input':
    next_input_path = 1
    continue
  elif next_input_path == 1:
    next_input_path = 0
    input_path = arg
    continue

  if arg == '--replace':
    next_replace = 1
    continue
  elif next_replace == 1:
    next_replace = 0
    replaces.append(arg)
    continue

  if arg == '--exclude_for':
    next_exclude_for = 1
    continue
  elif next_exclude_for == 1:
    next_exclude_for = 0
    exclude_for = arg
    continue

  file_paths.append(arg)

if input_path != '':
  if len(file_paths) != 0:
    eprint('You need to specify input file or input paths in command line.')
  elif not os.path.isfile(input_path):
    eprint('Input path not found.')
  else:
    platforms = []
    with open(input_path, 'r') as f:
      for line in f:
        file_path = line.strip()
        if file_path[0:10] == 'platforms:':
          platforms_list = file_path[10:].split(' ')
          platforms = []
          for platform in file_path[10:].split(' '):
            platform = platform.strip()
            if platform != '':
              platforms.append(platform)
        elif file_path[0:2] != '//' and file_path != '':
          file_paths.append(file_path)
          if len(platforms):
            platform_rules[file_path] = platforms
          elif '/platform/win/' in file_path:
            platform_rules[file_path] = [ 'win' ]
          elif '/platform/mac/' in file_path:
            platform_rules[file_path] = [ 'mac' ]
          elif '/platform/linux/' in file_path:
            platform_rules[file_path] = [ 'linux' ]

for replace in replaces:
  replace_parts = replace.split('=', 1)
  if len(replace_parts) != 2:
    eprint('Bad replace: ' + replace)
  real_paths = []
  real_platform_rules = {}
  for file_path in file_paths:
    real_path = file_path.replace('<(' + replace_parts[0] + ')', replace_parts[1])
    real_paths.append(real_path)
    if file_path in platform_rules:
      real_platform_rules[real_path] = platform_rules[file_path]
  file_paths = real_paths
  platform_rules = real_platform_rules

if exclude_for != '':
  real_paths = []
  for file_path in file_paths:
    if not file_path in platform_rules:
      continue
    if not should_exclude(platform_rules[file_path], exclude_for):
      continue
    real_paths.append(file_path)
  file_paths = real_paths

for file_path in file_paths:
  print(file_path)
if moc_prefix != '':
  for file_path in file_paths:
    if check_non_empty_moc(file_path):
      m = re.search(r'(^|/)([^/]+)\.h$', file_path)
      if not m:
        eprint('Bad file path: ' + file_path)
      print(moc_prefix + m.group(2) + '.cpp')
