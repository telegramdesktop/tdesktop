import os, sys, re, subprocess, datetime, time

executePath = os.getcwd()
scriptPath = os.path.dirname(os.path.realpath(__file__))

lastCommit = ''
today = ''
uuid = ''
nextLast = False
nextDate = False
nextUuid = False
building = True
composing = False
conf = 'Release'
for arg in sys.argv:
    if nextLast:
        lastCommit = arg
        nextLast = False
    elif nextDate:
        today = arg
        nextDate = False
    elif nextUuid:
        uuid = arg
        nextUuid = False
    elif arg == 'send':
        building = False
        composing = False
    elif arg == 'from':
        nextLast = True
        building = False
        composing = True
    elif arg == 'date':
        nextDate = True
    elif arg == 'request_uuid':
        nextUuid = True
    elif arg == 'debug':
        conf = 'Debug'

def finish(code, error = ''):
    if error != '':
        print('[ERROR] ' + error)
    global executePath
    os.chdir(executePath)
    sys.exit(code)

os.chdir(scriptPath + '/..')

if 'AC_USERNAME' not in os.environ:
    finish(1, 'AC_USERNAME not found!')
username = os.environ['AC_USERNAME']

if today == '':
    today = datetime.datetime.now().strftime("%d_%m_%y")
outputFolder = 'updates/' + today

archive = 'tdesktop_macOS_' + today + '.zip'

if building:
    print('Building ' + conf + ' version for OS X 10.13+..')

    if os.path.exists('../out/' + conf + '/' + outputFolder):
        finish(1, 'Todays updates version exists.')

    if uuid == '':
        result = subprocess.call('./configure.sh', shell=True)
        if result != 0:
            finish(1, 'While calling GYP.')

    os.chdir('../out')
    if uuid == '':
        result = subprocess.call('cmake --build . --config ' + conf + ' --target Telegram', shell=True)
        if result != 0:
            finish(1, 'While building Telegram.')

    os.chdir(conf);
    if uuid == '':
        if not os.path.exists('Telegram.app'):
            finish(1, 'Telegram.app not found.')

        result = subprocess.call('strip Telegram.app/Contents/MacOS/Telegram', shell=True)
        if result != 0:
            finish(1, 'While stripping Telegram.')

        result = subprocess.call('codesign --force --deep --timestamp --options runtime --sign "Developer ID Application: John Preston" Telegram.app --entitlements "../../Telegram/Telegram/Telegram.entitlements"', shell=True)
        if result != 0:
            finish(1, 'While signing Telegram.')

        if not os.path.exists('Telegram.app/Contents/Frameworks/Updater'):
            finish(1, 'Updater not found.')
        elif not os.path.exists('Telegram.app/Contents/Helpers/crashpad_handler'):
            finish(1, 'crashpad_handler not found.')
        elif not os.path.exists('Telegram.app/Contents/Resources/Icon.icns'):
            finish(1, 'Icon not found.')
        elif not os.path.exists('Telegram.app/Contents/_CodeSignature'):
            finish(1, 'Signature not found.')

        if os.path.exists(today):
            subprocess.call('rm -rf ' + today, shell=True)
        result = subprocess.call('mkdir -p ' + today + '/TelegramForcePortable', shell=True)
        if result != 0:
            finish(1, 'Creating folder ' + today + '/TelegramForcePortable')

        result = subprocess.call('cp -r Telegram.app ' + today + '/', shell=True)
        if result != 0:
            finish(1, 'Cloning Telegram.app to ' + today + '.')

        result = subprocess.call('zip -r ' + archive + ' ' + today, shell=True)
        if result != 0:
            finish(1, 'Adding tdesktop to archive.')

        print('Beginning notarization process.')
        result = subprocess.call('xcrun notarytool submit "' + archive + '" --keychain-profile "preston" --wait', shell=True)
        if result != 0:
            finish(1, 'Notarizing the archive.')
    result = subprocess.call('xcrun stapler staple Telegram.app', shell=True)
    if result != 0:
        finish(1, 'Error calling stapler')

    subprocess.call('rm -rf ' + today + '/Telegram.app', shell=True)
    subprocess.call('rm ' + archive, shell=True)
    result = subprocess.call('cp -r Telegram.app ' + today + '/', shell=True)
    if result != 0:
        finish(1, 'Re-Cloning Telegram.app to ' + today + '.')

    result = subprocess.call('zip -r ' + archive + ' ' + today, shell=True)
    if result != 0:
        finish(1, 'Re-Adding tdesktop to archive.')
    print('Re-Archived.')

    subprocess.call('mkdir -p ' + outputFolder, shell=True)
    subprocess.call('mv ' + archive + ' ' + outputFolder + '/', shell=True)
    subprocess.call('rm -rf ' + today, shell=True)
    print('Finished.')
    finish(0)

commandPath = scriptPath + '/../../out/' + conf + '/' + outputFolder + '/command.txt'

if composing:
    templatePath = scriptPath + '/../../../DesktopPrivate/updates_template.txt'
    if not os.path.exists(templatePath):
        finish(1, 'Template file "' + templatePath + '" not found.')

    if not re.match(r'^[a-f0-9]{9,40}$', lastCommit):
        finish(1, 'Wrong last commit: ' + lastCommit)

    log = subprocess.check_output(['git', 'log', lastCommit+'..HEAD']).decode('utf-8')
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
                print(log)
                finish(1, 'Bad git log output.')
            if len(commits[len(commits) - 1]):
                commits[len(commits) - 1] += '\n' + stripped
            else:
                commits[len(commits) - 1] = '- ' + stripped
    commits.reverse()
    if not len(commits):
        finish(1, 'No commits since last build :(')

    changelog = '\n'.join(commits)
    print('\n\nReady! File: ' + archive + '\nChangelog:\n' + changelog)
    with open(templatePath, 'r') as template:
        with open(commandPath, 'w') as f:
            for line in template:
                if line.startswith('//'):
                    continue
                line = line.replace('{path}', scriptPath + '/../../out/' + conf + '/' + outputFolder + '/' + archive)
                line = line.replace('{caption}', 'TDesktop at ' + today.replace('_', '.') + ':\n\n' + changelog)
                f.write(line)
    print('\n\nEdit:\n')
    print('vi ' + commandPath)
    finish(0)

if not os.path.exists(commandPath):
    finish(1, 'Command file not found.')

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
                finish(1, 'Wrong caption start.')
print('\n\nSending! File: ' + archive + '\nChangelog:\n' + caption)
if len(caption) > 1024:
    print('Length: ' + str(len(caption)))
    print('vi ' + commandPath)
    finish(1, 'Too large.')

if not os.path.exists('../out/' + conf + '/' + outputFolder + '/' + archive):
    finish(1, 'Not built yet.')

subprocess.call(scriptPath + '/../../out/' + conf + '/Telegram.app/Contents/MacOS/Telegram -sendpath interpret://' + scriptPath + '/../../out/' + conf + '/' + outputFolder + '/command.txt', shell=True)

finish(0)
