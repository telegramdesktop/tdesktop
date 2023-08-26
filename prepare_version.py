# script for full new version preparation

# imports
import os, shutil
from datetime import date

# config
make_setup = True
make_portable = True
repo_folder = "c:/users/xmdnx/source/repos/exteraGramDesktop/"
version = "4.9.1-27082023"

def set_version():
    global version
    with open(repo_folder + "Telegram/SourceFiles/core/version.h", "r") as version_file:
        version_code = version_file.readlines()[25]
        version += version_code.replace('constexpr auto AppVersionStr = "', '').replace('";', date.today().strftime("-%d%m%Y")).replace('\n', '')
    print("version: " + version)

def update_build_date():
    read_iss_file = open(repo_folder + "Telegram/build/setup.iss", "r", encoding="utf-8")
    iss_file_data = read_iss_file.readlines()
    iss_file_data[10] = '#define MyAppVersionFull "' + version + '"\n'
    with open(repo_folder + "Telegram/build/setup.iss", "w", encoding="utf-8") as write_iss_file:
        write_iss_file.writelines(iss_file_data)

def rename_file():
    print("# Renaming file...")

    # check if Telegram.exe exists
    if (not os.path.exists(os.path.join(repo_folder + "out/Release/", "Telegram.exe"))):
        print("#  Telegram.exe does not exist, check if exteraGram.exe exist...")
        if os.path.exists(os.path.join(repo_folder + "out/Release/", "exteraGram.exe")):
            print("#  exteraGram.exe exists, but Telegram.exe not exist. Skipping rename part...")
            return
        else:
            print("#  exteraGram.exe does not exist too, halt...")
            exit()

    # removing old exteraGram.exe
    if os.path.exists(os.path.join(repo_folder + "out/Release/", "exteraGram.exe")):
        os.remove(os.path.join(repo_folder + "out/Release/", "exteraGram.exe"))
        print("#  exteraGram.exe removed successfully, renaming")
    else:
        print("#  exteraGram.exe does not exist, renaming")

    old_path = os.path.join(repo_folder + "out/Release/", "Telegram.exe")
    new_path = os.path.join(repo_folder + "out/Release/", "exteraGram.exe")
    os.rename(old_path, new_path)
    print("#  Renamed Telegram.exe -> exteraGram.exe")

def run_iss_build():
    print("# Running iscc build... If error occurs, check if iscc.exe path added to PATH")
    os.system("iscc " + repo_folder + "Telegram/build/setup.iss")

def make_portable_version():
    print("# Making portable version\n#  Creating 'portable' folder")
    try:
        os.mkdir(os.path.join(repo_folder + "out/Release/portable"))
        print("#   Created 'portable' folder")
    except:
        print("#   Folder 'portable' already exists")
    print("#  Copying portable files")
    try:
        shutil.copyfile(os.path.join(repo_folder + "out/Release/", "exteraGram.exe"), os.path.join(repo_folder + "out/Release/portable", "exteraGram.exe"))
        shutil.copytree(repo_folder + "out/Release/modules", repo_folder + "out/Release/portable/modules")
        print("#   Files copied to 'portable' folder")
    except:
        print("#   Files already exist")
    print("#  Making archive...")
    shutil.make_archive(os.path.join(repo_folder + "out/Release/etgdportable-x64." + version), 'zip', os.path.join(repo_folder + "out/Release/portable"))

# main script part
if version == "":
    set_version()
update_build_date()
rename_file()
if make_setup:
    run_iss_build()
if make_portable:
    make_portable_version()
print("# All done.")