import os, sys, requests, re
from os.path import expanduser

filename = ''
for arg in sys.argv:
  if re.match(r'.*\.strings$', arg):
    filename = expanduser(arg)

result = ''
if not os.path.isfile(filename):
  print("File not found.")
  sys.exit(1)

with open(filename) as f:
  for line in f:
    if re.match(r'\s*\/\* new from [a-zA-Z0-9\.]+ \*\/\s*', line):
      continue
    if re.match(r'\"lng_[a-z_]+\#(zero|two|few|many)\".+', line):
      continue
    result = result + line

remove = 0
while (len(result) > remove + 1) and (result[len(result) - remove - 1] == '\n') and (result[len(result) - remove - 2] == '\n'):
  remove = remove + 1
result = result[:len(result) - remove]

with open('lang.strings', 'w') as out:
  out.write(result)

sys.exit()
