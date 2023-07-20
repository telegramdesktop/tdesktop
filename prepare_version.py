# script for full new version preparation

import os, shutil

make_portable = True
version = "4.8.4-20072023"

def rename_file():
    print("# Renaming file...")
    try:
        old_path = os.path.join("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/", "Telegram.exe")
        new_path = os.path.join("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/", "exteraGram.exe")
        os.rename(old_path, new_path)
        print("#  Renamed Telegram.exe -> exteraGram.exe")
    except:
        print("#  Telegram.exe does not exist")
        return

def run_iss_build():
    print("# Running iscc build... If error occurs, check if iscc.exe path added to PATH")
    os.system("iscc c:/users/xmdnx/source/repos/exteraGramDesktop/Telegram/build/setup.iss")

def make_portable_version():
    print("# Making portable version\n#  Creating 'portable' folder")
    try:
        os.mkdir(os.path.join("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/portable"))
        print("#   Created 'portable' folder")
    except:
        print("#   Folder 'portable' already exists")
    print("#  Copying portable files")
    try:
        shutil.copyfile(os.path.join("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/", "exteraGram.exe"), os.path.join("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/portable", "exteraGram.exe"))
        shutil.copytree("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/modules", "C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/portable/modules")
        print("#   Files copied to 'portable' folder")
    except:
        print("#   Files already exist")
    print("#  Making archive...")
    shutil.make_archive(os.path.join("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/etgdportable-x64." + version), 'zip', os.path.join("C:/users/xmdnx/source/repos/exteraGramDesktop/out/Release/portable"))

# main script part
rename_file()
run_iss_build()
if make_portable:
    make_portable_version()
print("# All done.")