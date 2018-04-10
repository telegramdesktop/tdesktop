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

def eprint(*args, **kwargs):
  print(*args, file=sys.stderr, **kwargs)
  sys.exit(1)

my_path = os.path.dirname(os.path.realpath(__file__)).replace('\\', '/')

def get_qrc_dependencies(file_path):
  global one_modified
  dependencies = {}
  if not os.path.isfile(file_path):
    eprint('File not found: ' + file_path)
  dir_name = os.path.dirname(file_path).replace('\\', '/')
  with open(file_path) as f:
    for line in f:
      file_match = re.match('^\s*<file(\s[^>]*)?>([^<]+)</file>', line)
      if file_match:
        full_path = dir_name + '/' + file_match.group(2)
        dependencies[full_path] = 1
  return dependencies

def list_qrc_dependencies(file_path):
  global one_modified
  dependencies = get_qrc_dependencies(file_path)
  for path in dependencies:
    print(path)
  sys.exit(0)

one_modified = 0
def handle_qrc_dependencies(file_path):
  global one_modified
  dependencies = get_qrc_dependencies(file_path)
  file_modified = os.path.getmtime(file_path)
  latest_modified = file_modified
  for path in dependencies:
    if os.path.isfile(path):
      dependency_modified = os.path.getmtime(path)
      if latest_modified < dependency_modified:
        latest_modified = dependency_modified
    else:
      eprint('File not found: ' + path)
  if file_modified < latest_modified:
    os.utime(file_path, None);
    one_modified = 1

def get_direct_style_dependencies(file_path):
  dependencies = {}
  dependencies[file_path] = 1
  if not os.path.isfile(file_path):
    eprint('File not found: ' + file_path)
  with open(file_path) as f:
    for line in f:
      using_match = re.match('^\s*using "([^"]+)"', line)
      if using_match:
        path = using_match.group(1)
        found = 0
        for include_dir in include_dirs:
          full_path = include_dir + '/' + path
          if os.path.isfile(full_path):
            try:
              if dependencies[full_path]:
                eprint('Cyclic dependencies: ' + full_path)
            except KeyError:
              dependencies[full_path] = 1
            found = 1
            break
        if found != 1:
          eprint('File not found: ' + path)
  return dependencies

include_dirs = []
def handle_style_dependencies(file_path):
  global one_modified
  all_dependencies = {}
  all_dependencies[file_path] = 1
  added_from = {}
  while len(added_from) != len(all_dependencies):
    for dependency in all_dependencies:
      try:
        if added_from[dependency]:
          continue
      except KeyError:
        added_from[dependency] = 1
        add = get_direct_style_dependencies(dependency)
        for new_dependency in add:
          all_dependencies[new_dependency] = 1
        break

  file_modified = os.path.getmtime(file_path)
  latest_modified = file_modified
  for path in all_dependencies:
    if path != file_path:
      dependency_modified = os.path.getmtime(path)
      if latest_modified < dependency_modified:
        latest_modified = dependency_modified
  if file_modified < latest_modified:
    os.utime(file_path, None);
    one_modified = 1

file_paths = []
request = ''
output_file = ''
next_include_dir = 0
next_output_file = 0
next_self = 1
for arg in sys.argv:
  if next_self != 0:
    next_self = 0
    continue
  if arg == '--styles' or arg == '--qrc_list' or arg == '--qrc':
    if request == '':
      request = arg[2:]
    else:
      eprint('Only one request required.')
    continue
  if next_include_dir != 0:
    next_include_dir = 0
    include_dirs.append(arg)
    continue
  if next_output_file != 0:
    next_output_file = 0
    output_file = arg
    continue

  include_dir_match = re.match(r'^\-I(.*)$', arg)
  if include_dir_match:
    include_dir = include_dir_match.group(1)
    if include_dir == '':
      next_include_dir = 1
    else:
      include_dirs.append(include_dir)
    continue

  output_match = re.match(r'^-o(.*)$', arg)
  if output_match:
    output_file = output_match.group(1)
    if output_file == '':
      next_output_file = 1
    continue

  file_paths.append(arg)

if request == 'styles':
  for file_path in file_paths:
    handle_style_dependencies(file_path)
elif request == 'qrc':
  for file_path in file_paths:
    handle_qrc_dependencies(file_path)
elif request == 'qrc_list':
  for file_path in file_paths:
    list_qrc_dependencies(file_path)
else:
  eprint('Request required.')

if not os.path.isfile(output_file):
  with open(output_file, "w") as f:
    f.write('1')
elif one_modified != 0:
  os.utime(output_file, None);
