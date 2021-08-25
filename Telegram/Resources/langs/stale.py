import os, sys, requests, re

os.chdir()

keys = []
with open('lang.strings') as f:
    for line in f:
        m = re.match(r'\"(lng_[a-z_]+)(\#[a-z]+)?\"', line)
        if m:
            keys.append(m.group(1))
        elif not re.match(r'^\s*$', line):
            print('Bad line: ' + line)
            sys.exit(1)

print('Keys: ' + str(len(keys)))

sys.exit()
