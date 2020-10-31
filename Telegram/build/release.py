import os, sys, requests, pprint, re, json
from uritemplate import URITemplate, expand
from subprocess import call, Popen, PIPE
from os.path import expanduser

changelog_file = '../../changelog.txt'
token_file = '../../../DesktopPrivate/github-releases-token.txt'

version = ''
commit = ''
for arg in sys.argv:
  if re.match(r'\d+\.\d+', arg):
    version = arg
  elif re.match(r'^[a-f0-9]{40}$', arg):
    commit = arg

# thanks http://stackoverflow.com/questions/13909900/progress-of-python-requests-post
class upload_in_chunks(object):
  def __init__(self, filename, chunksize=1 << 13):
    self.filename = filename
    self.chunksize = chunksize
    self.totalsize = os.path.getsize(filename)
    self.readsofar = 0

  def __iter__(self):
    with open(self.filename, 'rb') as file:
      while True:
        data = file.read(self.chunksize)
        if not data:
          sys.stderr.write("\n")
          break
        self.readsofar += len(data)
        percent = self.readsofar * 1e2 / self.totalsize
        sys.stderr.write("\r{percent:3.0f}%".format(percent=percent))
        yield data

  def __len__(self):
    return self.totalsize

class IterableToFileAdapter(object):
  def __init__(self, iterable):
    self.iterator = iter(iterable)
    self.length = len(iterable)

  def read(self, size=-1): # TBD: add buffer for `len(data) > size` case
    return next(self.iterator, b'')

  def __len__(self):
    return self.length

def checkResponseCode(result, right_code):
  if (result.status_code != right_code):
    print('Wrong result code: ' + str(result.status_code) + ', should be ' + str(right_code))
    sys.exit(1)

def getOutput(command):
  p = Popen(command.split(), stdout=PIPE)
  output, err = p.communicate()
  if err != None or p.returncode != 0:
    print('ERROR!')
    print(err)
    print(p.returncode)
    sys.exit(1)
  return output.decode('utf-8')

def prepareSources():
  workpath = os.getcwd()
  os.chdir('../..')
  rootpath = os.getcwd()
  finalpath = rootpath + '/out/Release/sources.tar'
  if os.path.exists(finalpath):
    os.remove(finalpath)
  if os.path.exists(finalpath + '.gz'):
    os.remove(finalpath + '.gz')
  tmppath = rootpath + '/out/Release/tmp.tar'
  print('Preparing source tarball...')
  if (call(('git archive --prefix=tdesktop-' + version + '-full/ -o ' + finalpath + ' v' + version).split()) != 0):
    os.remove(finalpath)
    sys.exit(1)
  lines = getOutput('git submodule foreach').split('\n')
  for line in lines:
    if len(line) == 0:
      continue
    match = re.match(r"^Entering '([^']+)'$", line)
    if not match:
      print('Bad line: ' + line)
      sys.exit(1)
    path = match.group(1)
    revision = getOutput('git rev-parse v' + version + ':' + path).split('\n')[0]
    print('Adding submodule ' + path + '...')
    os.chdir(path)
    if (call(('git archive --prefix=tdesktop-' + version + '-full/' + path + '/ ' + revision + ' -o ' + tmppath).split()) != 0):
      os.remove(finalpath)
      os.remove(tmppath)
      sys.exit(1)
    if (call(('gtar --concatenate --file=' + finalpath + ' ' + tmppath).split()) != 0):
      os.remove(finalpath)
      os.remove(tmppath)
      sys.exit(1)
    os.remove(tmppath)
    os.chdir(rootpath)
  print('Compressing...')
  if (call(('gzip -9 ' + finalpath).split()) != 0):
    os.remove(finalpath)
    sys.exit(1)
  os.chdir(workpath)
  return finalpath + '.gz'

pp = pprint.PrettyPrinter(indent=2)
url = 'https://api.github.com/'

version_parts = version.split('.')

stable = 1
beta = 0

if len(version_parts) < 2:
  print('Error: expected at least major version ' + version)
  sys.exit(1)
if len(version_parts) > 4:
  print('Error: bad version passed ' + version)
  sys.exit(1)
version_major = version_parts[0] + '.' + version_parts[1]
if len(version_parts) == 2:
  version = version_major + '.0'
  version_full = version
else:
  version = version_major + '.' + version_parts[2]
  version_full = version
  if len(version_parts) == 4:
    if version_parts[3] == 'beta':
      beta = 1
      stable = 0
      version_full = version + '.beta'
    else:
      print('Error: unexpected version part ' + version_parts[3])
      sys.exit(1)

access_token = ''
if os.path.isfile(token_file):
  with open(token_file) as f:
    for line in f:
      access_token = line.replace('\n', '')

if access_token == '':
  print('Access token not found!')
  sys.exit(1)

print('Version: ' + version_full)
local_folder = expanduser("~") + '/Projects/backup/tdesktop/' + version_major + '/' + version_full

if stable == 1:
  if os.path.isdir(local_folder + '.beta'):
    beta = 1
    stable = 0
    version_full = version + '.beta'
    local_folder = local_folder + '.beta'

if not os.path.isdir(local_folder):
  print('Storage path not found: ' + local_folder)
  sys.exit(1)

local_folder = local_folder + '/'

files = []
files.append({
  'local': 'tsetup.' + version_full + '.exe',
  'remote': 'tsetup.' + version_full + '.exe',
  'backup_folder': 'tsetup',
  'mime': 'application/octet-stream',
  'label': 'Windows: Installer',
})
files.append({
  'local': 'tportable.' + version_full + '.zip',
  'remote': 'tportable.' + version_full + '.zip',
  'backup_folder': 'tsetup',
  'mime': 'application/zip',
  'label': 'Windows: Portable',
})
files.append({
  'local': 'tsetup.' + version_full + '.dmg',
  'remote': 'tsetup.' + version_full + '.dmg',
  'backup_folder': 'tmac',
  'mime': 'application/octet-stream',
  'label': 'macOS 10.12+: Installer',
})
files.append({
  'local': 'tsetup.' + version_full + '.tar.xz',
  'remote': 'tsetup.' + version_full + '.tar.xz',
  'backup_folder': 'tlinux',
  'mime': 'application/octet-stream',
  'label': 'Linux 64 bit: Binary',
})
files.append({
  'local': 'sources',
  'remote': 'tdesktop-' + version + '-full.tar.gz',
  'mime': 'application/x-gzip',
  'label': 'Source code (tar.gz, full)',
})

r = requests.get(url + 'repos/telegramdesktop/tdesktop/releases/tags/v' + version)
if r.status_code == 404:
  print('Release not found, creating.')
  if commit == '':
    print('Error: specify the commit.')
    sys.exit(1)
  if not os.path.isfile(changelog_file):
    print('Error: Changelog file not found.')
    sys.exit(1)
  changelog = ''
  started = 0
  with open(changelog_file) as f:
    for line in f:
      if started == 1:
        if re.match(r'^\d+\.\d+', line):
          break
        changelog += line
      else:
        if re.match(r'^\d+\.\d+', line):
          if line[0:len(version) + 1] == version + ' ':
            started = 1
          elif line[0:len(version_major) + 1] == version_major + ' ':
            if version_major + '.0' == version:
              started = 1
  if started != 1:
    print('Error: Changelog not found.')
    sys.exit(1)

  changelog = changelog.strip()
  print('Changelog: ')
  print(changelog)

  r = requests.post(url + 'repos/telegramdesktop/tdesktop/releases', headers={'Authorization': 'token ' + access_token}, data=json.dumps({
    'tag_name': 'v' + version,
    'target_commitish': commit,
    'name': 'v ' + version,
    'body': changelog,
    'prerelease': (beta == 1),
  }))
  checkResponseCode(r, 201)

tagname = 'v' + version
call("git fetch origin".split())
if stable == 1:
  call("git push launchpad {}:master".format(tagname).split())
else:
  call("git push launchpad {}:beta".format(tagname).split())
call("git push --tags launchpad".split())

r = requests.get(url + 'repos/telegramdesktop/tdesktop/releases/tags/v' + version)
checkResponseCode(r, 200)

release_data = r.json()
#pp.pprint(release_data)

release_id = release_data['id']
print('Release ID: ' + str(release_id))

r = requests.get(url + 'repos/telegramdesktop/tdesktop/releases/' + str(release_id) + '/assets')
checkResponseCode(r, 200)

assets = release_data['assets']
for asset in assets:
  name = asset['name']
  found = 0
  for file in files:
    if file['remote'] == name:
      print('Already uploaded: ' + name)
      file['already'] = 1
      found = 1
      break
  if found == 0:
    print('Warning: strange asset: ' + name)

for file in files:
  if 'already' in file:
    continue
  if file['local'] == 'sources':
    file_path = prepareSources()
  else:
    file_path = local_folder + file['backup_folder'] + '/' + file['local']
  if not os.path.isfile(file_path):
    print('Warning: file not found ' + file['local'])
    continue

  upload_url = expand(release_data['upload_url'], {'name': file['remote'], 'label': file['label']}) + '&access_token=' + access_token

  content = upload_in_chunks(file_path, 10)

  print('Uploading: ' + file['remote'] + ' (' + str(round(len(content) / 10000) / 100.) + ' MB)')
  r = requests.post(upload_url, headers={"Content-Type": file['mime']}, data=IterableToFileAdapter(content))

  checkResponseCode(r, 201)

  print('Success! Removing.')
  return_code = call(["rm", file_path])
  if return_code != 0:
    print('Bad rm code: ' + str(return_code))
    sys.exit(1)

sys.exit()
