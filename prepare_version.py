# script for full new version preparation

import os, subprocess, shutil

make_portable = True
version = "4.8.4-19072023"

def rename_file():
    old_path = os.path.join("%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/", "Telegram.exe")
    new_path = os.path.join("%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/", "exteraGram.exe")
    os.rename(old_path, new_path)

def run_iss_build():
    subprocess.check_call(["iscc", "c:/users/xmdnx/source/repos/exteraGramDesktop/Telegram/build/setup.iss"])

def make_portable_version():
    os.mkdir(os.path.join("%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/portable"))
    shutil.copyfile(os.path.join("%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/", "exteraGram.exe"), os.path.join("%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/portable", "exteraGram.exe"))
    shutil.copytree("%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/modules", "%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/portable/modules")
    shutil.make_archive(os.path.join("%USERPROFILE%", "/source/repos/exteraGramDesktop/out/Release/etgd-portable-", version), 'zip', os.path.join("%USERPROFILE%" + "/source/repos/exteraGramDesktop/out/Release/portable"))

# main script part
rename_file()
run_iss_build()
if make_portable:
    make_portable_version()