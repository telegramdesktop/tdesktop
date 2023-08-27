# new version preparation script
# libraries
import os, shutil
from datetime import date

# config
config = {
    "make_setup": True,          # set True if you want to make setup version
    "make_portable": True,       # set True if you want to make portable version
    "repo_path": "",             # leave it blank if this script located in repo folder
    "version": "",               # leave it blank to fill with version from SourceFiles/core/version.h and script runtime date
    "client_name": "exteraGram",
}

# functions
def log(text, level):
    print("#" + " " * level + text)

def set_repo_path():
    log("Setting repo path...", 1)
    config["repo_path"] = os.getcwd()
    log("Repo path was not set in config", 2)
    log("Set repo path to: " + config["repo_path"], 2)

def set_version():
    log("Setting version...", 1)
    version_file = open(config["repo_path"] + "/Telegram/SourceFiles/core/version.h", "r")
    version_code = version_file.readlines()[25]
    config["version"] += version_code.replace('constexpr auto AppVersionStr = "', '').replace('";', date.today().strftime("-%d%m%Y")).replace('\n', '')

def set_iss():
    log("Updating iss file...", 1)
    iss_file = open(config["repo_path"] + "/Telegram/build/setup.iss", "r", encoding="utf-8")
    iss_file_data = iss_file.readlines()
    iss_file_data[10] = '#define MyAppVersionFull "' + config["version"] + '"\n'
    iss_file.close()
    iss_file = open(config["repo_path"] + "/Telegram/build/setup.iss", "w", encoding="utf-8")
    iss_file.writelines(iss_file_data)

def rename_files():
    log("# Renaming files...", 1)

    if (not os.path.exists(os.path.join(config["repo_path"] + "/out/Release/", "Telegram.exe"))):
        log("Telegram.exe does not exist, check if exteraGram.exe exist...", 2)
        if os.path.exists(os.path.join(config["repo_path"] + "/out/Release/", "exteraGram.exe")):
            log("exteraGram.exe exists, but Telegram.exe not exist. Skipping rename part...", 2)
            return
        else:
            log("exteraGram.exe does not exist too, halt...", 2)
            exit()

    if os.path.exists(os.path.join(config["repo_path"] + "/out/Release/", "exteraGram.exe")):
        os.remove(os.path.join(config["repo_path"] + "/out/Release/", "exteraGram.exe"))
        log("exteraGram.exe removed successfully, renaming", 2)
    else:
        log("exteraGram.exe does not exist, renaming", 2)

    old_path = os.path.join(config["repo_path"] + "/out/Release/", "Telegram.exe")
    new_path = os.path.join(config["repo_path"] + "/out/Release/", "exteraGram.exe")
    os.rename(old_path, new_path)
    log("Renamed Telegram.exe -> exteraGram.exe", 2)

def run_iss_build():
    log("Running iscc build... If error occurs, check if iscc.exe path added to PATH", 1)
    os.system("iscc " + config["repo_path"] + "/Telegram/build/setup.iss")

def make_portable():
    log("Making portable version", 1)
    log("Creating 'portable' folder", 2)
    try:
        os.mkdir(os.path.join(config["repo_path"] + "/out/Release/portable"))
        log("Created 'portable' folder", 3)
    except:
        log("Folder 'portable' already exists", 3)
    log("Copying portable files", 2)
    try:
        shutil.copyfile(os.path.join(config["repo_path"] + "/out/Release/", "exteraGram.exe"), os.path.join(config["repo_path"] + "/out/Release/portable", "exteraGram.exe"))
        shutil.copytree(config["repo_path"] + "/out/Release/modules", config["repo_path"] + "/out/Release/portable/modules")
        log("Files copied to 'portable' folder", 3)
    except:
        log("Files already exist", 3)
    log("Making archive...", 2)
    shutil.make_archive(os.path.join(config["repo_path"] + "/out/Release/etgdportable-x64." + config["version"]), 'zip', os.path.join(config["repo_path"] + "/out/Release/portable"))

if config["repo_path"] == "":
    set_repo_path()
if config["version"] == "":
    set_version()
rename_files()
if config["make_setup"]:
    set_iss()
    run_iss_build()
if config["make_portable"]:
    make_portable()

log("All done!", 1)