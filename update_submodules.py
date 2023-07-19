import os
import re
import subprocess
import time

import requests

with open('.gitmodules') as f:
    local_data = f.read()


def parse_submodules(submodules_content):
    regex = re.compile(r'\[submodule "(.*)"\]\n\s+path = (.*)\n\s+url = (.*)\n', re.MULTILINE)
    submodules = []
    for match in regex.finditer(submodules_content):
        submodules.append({
            'path': match.group(2),
            'url': match.group(3),
        })

    return submodules


def parse_remote_commit(path):
    if path == 'cmake':
        r = requests.get('https://github.com/telegramdesktop/tdesktop/tree/dev')
        regex = re.compile(r'cmake @ (.*?)<')

        try:
            return regex.search(r.text).group(1)
        except:
            return None

    parent = os.path.dirname(path)

    r = requests.get('https://github.com/telegramdesktop/tdesktop/tree/dev/' + parent)

    try:
        data = r.json()
    except:
        return None

    for item in data['payload']['tree']['items']:
        if item['path'] == path:
            return item['submoduleDisplayName'].split(' @ ')[1]


mismatched = {}

local_submodules = parse_submodules(local_data)

for submodule in local_submodules:
    path = submodule['path']
    current_commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path).decode('utf-8').strip()[:7]
    remote_commit = None

    while remote_commit is None:
        remote_commit = parse_remote_commit(path)

        if remote_commit is None:
            time.sleep(3)

    eq = '==' if remote_commit == current_commit else '!='

    print(f'{path:<50} {current_commit} {eq} {remote_commit}')

    if remote_commit != current_commit:
        mismatched[path] = (current_commit, remote_commit)

if mismatched:
    print('\n\n\nMismatched submodules:')
    for path, (current_commit, remote_commit) in mismatched.items():
        print(f'{path:<50} {current_commit} != {remote_commit}')

    s = input('\nSubmodules to update: ')

    for submodule in s.split(','):
        path = submodule.strip()
        if path not in mismatched:
            print(f'Unknown submodule: {path}')
            continue

        print(f'Updating {path}...')

        before = os.getcwd()
        os.chdir(path)
        subprocess.check_call(['git', 'fetch'])
        subprocess.check_call(['git', 'checkout', mismatched[path][1]])
        os.chdir(before)
        subprocess.check_call(['git', 'add', path])

        print(f'Updated {path}')