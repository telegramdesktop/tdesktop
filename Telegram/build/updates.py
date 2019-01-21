import os, sys, re, subprocess, datetime

executePath = os.getcwd()
scriptPath = os.path.dirname(os.path.realpath(__file__))

lastCommit = ''
today = ''
nextLast = False
nextDate = False
building = True
composing = False
for arg in sys.argv:
    if nextLast:
        lastCommit = arg
        nextLast = False
    elif nextDate:
        today = arg
        nextDate = False
    elif arg == 'send':
        building = False
        composing = False
    elif arg == 'from':
        nextLast = True
        building = False
        composing = True
    elif arg == 'date':
        nextDate = True

def finish(code):
    global executePath
    os.chdir(executePath)
    sys.exit(code)

os.chdir(scriptPath + '/..')

if today == '':
    today = datetime.datetime.now().strftime("%d_%m_%y")
outputFolder = 'updates/' + today

archive = 'tdesktop_macOS_' + today + '.zip'

if building:
    print('Building debug version for OS X 10.8+..')

    if os.path.exists('../out/Debug/' + outputFolder):
        print('[ERROR] Todays updates version exists.')
        finish(1)

    result = subprocess.call('gyp/refresh.sh', shell=True)
    if result != 0:
        print('[ERROR] While calling GYP.')
        finish(1)

    result = subprocess.call('xcodebuild -project Telegram.xcodeproj -alltargets -configuration Debug build', shell=True)
    if result != 0:
        print('[ERROR] While building Telegram.')
        finish(1)

    os.chdir('../out/Debug')
    if not os.path.exists('Telegram.app'):
        print('[ERROR] Telegram.app not found.')
        finish(1)

    result = subprocess.call('strip Telegram.app/Contents/MacOS/Telegram', shell=True)
    if result != 0:
        print('[ERROR] While stripping Telegram.')
        finish(1)

    result = subprocess.call('codesign --force --deep --sign "Developer ID Application: John Preston" Telegram.app', shell=True)
    if result != 0:
        print('[ERROR] While signing Telegram.')
        finish(1)

    if not os.path.exists('Telegram.app/Contents/Frameworks/Updater'):
        print('[ERROR] Updater not found.')
        finish(1)
    elif not os.path.exists('Telegram.app/Contents/Helpers/crashpad_handler'):
        print('[ERROR] crashpad_handler not found.')
        finish(1)
    elif not os.path.exists('Telegram.app/Contents/Resources/Icon.icns'):
        print('[ERROR] Icon not found.')
        finish(1)
    elif not os.path.exists('Telegram.app/Contents/_CodeSignature'):
        print('[ERROR] Signature not found.')
        finish(1)

    if os.path.exists(today):
        subprocess.call('rm -rf ' + today, shell=True)
    result = subprocess.call('mkdir -p ' + today + '/TelegramForcePortable', shell=True)
    if result != 0:
        print('[ERROR] Creating folder ' + today + '/TelegramForcePortable')
        finish(1)

    result = subprocess.call('cp -r Telegram.app ' + today + '/', shell=True)
    if result != 0:
        print('[ERROR] Cloning Telegram.app to ' + today + '.')
        finish(1)

    result = subprocess.call('zip -r ' + archive + ' ' + today, shell=True)
    if result != 0:
        print('[ERROR] Adding tdesktop to archive.')
        finish(1)

    subprocess.call('mkdir -p ' + outputFolder, shell=True)
    subprocess.call('mv ' + archive + ' ' + outputFolder + '/', shell=True)
    subprocess.call('rm -rf ' + today, shell=True)
    print('Finished.')
    finish(0)

if composing:
    templatePath = scriptPath + '/../../../TelegramPrivate/updates_template.txt'
    if not os.path.exists(templatePath):
        print('[ERROR] Template file "' + templatePath + '" not found.')
        finish(1)

    if not re.match(r'^[a-f0-9]{40}$', lastCommit):
        print('[ERROR] Wrong last commit: ' + lastCommit)
        finish(1)

    log = subprocess.check_output(['git', 'log', lastCommit+'..HEAD'])
    logLines = log.split('\n')
    firstCommit = ''
    commits = []
    for line in logLines:
        if line.startswith('commit '):
            commit = line.split(' ')[1]
            if not len(firstCommit):
                firstCommit = commit
            commits.append('')
        elif line.startswith('    '):
            stripped = line[4:]
            if not len(stripped):
                continue
            elif not len(commits):
                print('[ERROR] Bad git log output:')
                print(log)
                finish(1)
            if len(commits[len(commits) - 1]):
                commits[len(commits) - 1] += '\n' + stripped
            else:
                commits[len(commits) - 1] = '- ' + stripped
    commits.reverse()
    if not len(commits):
        print('[ERROR] No commits since last build :(')
        finish(1)

    changelog = '\n'.join(commits)
    print('\n\nReady! File: ' + archive + '\nChangelog:\n' + changelog)
    with open(templatePath, 'r') as template:
        with open(scriptPath + '/../../out/Debug/' + outputFolder + '/command.txt', 'w') as f:
            for line in template:
                if line.startswith('//'):
                    continue
                line = line.replace('{path}', scriptPath + '/../../out/Debug/' + outputFolder + '/' + archive)
                line = line.replace('{caption}', 'TDesktop at ' + today.replace('_', '.') + ':\n\n' + changelog)
                f.write(line)
    finish(0)

commandPath = scriptPath + '/../../out/Debug/' + outputFolder + '/command.txt'
if not os.path.exists(commandPath):
    print('[ERROR] Command file not found.')
    finish(1)

readingCaption = False
caption = ''
with open(commandPath, 'r') as f:
    for line in f:
        if readingCaption:
            caption = caption + line
        elif line.startswith('caption: '):
            readingCaption = True
            caption = line[len('caption: '):]
            if not caption.startswith('TDesktop at ' + today.replace('_', '.') + ':'):
                print('[ERROR] Wrong caption start.')
                finish(1)
print('\n\nSending! File: ' + archive + '\nChangelog:\n' + caption)
if len(caption) > 1024:
    print('[ERROR] Length: ' + str(len(caption)))
    print('vi ' + commandPath)
    finish(1)

if not os.path.exists('../out/Debug/' + outputFolder + '/' + archive):
    print('[ERROR] Not built yet.')
    finish(1)

subprocess.call(scriptPath + '/../../out/Debug/Telegram.app/Contents/MacOS/Telegram -sendpath interpret://' + scriptPath + '/../../out/Debug/' + outputFolder + '/command.txt', shell=True)

finish(0)
