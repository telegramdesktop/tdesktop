'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
import sys, os, re, subprocess, shutil

def finish(code):
    global executePath
    os.chdir(executePath)
    sys.exit(code)

if sys.platform == 'win32' and not 'COMSPEC' in os.environ:
    print('[ERROR] COMSPEC environment variable is not set.')
    finish(1)

executePath = os.getcwd()
scriptPath = os.path.dirname(os.path.realpath(__file__))
src = scriptPath + '/../ThirdParty/qtlottie/src/bodymovin'
dst = scriptPath + '/../ThirdParty/qtlottie_helper/QtBodymovin'

shutil.rmtree(dst, ignore_errors=True)
os.makedirs(dst + '/private')

for r, d, f in os.walk(src):
    for file in f:
        if re.search(r'_p\.h$', file):
            shutil.copyfile(src + '/' + file, dst + '/private/' + file)
        elif re.search(r'\.h$', file):
            shutil.copyfile(src + '/' + file, dst + '/' + file)

finish(0)
