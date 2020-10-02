# Build instructions for Visual Studio 2019 [Easy Build - Python]

- [Prepare folder](#prepare-folder)
- [Install third party software](#install-third-party-software)
- [Clone source code and prepare libraries](#clone-source-code-and-prepare-libraries)
- [Build the project](#build-the-app)
- [Qt Visual Studio Tools](#qt-visual-studio-tools)

## Notice
<details>
    <summary>Click TO See A Important Notice</summary>
    Hello, This Is Akash Pattnaik From India.
    
    I Was Facing A Problem While Building TeleGram
    
    So I Decided To Make A Script Which Auto Builds The
    
    App And We Don't Need To Write Each Command Again And Again
    
    N.B - I Made It For Those Who Are Facing Problem While
    
          Coping Each And Every Code From Github...
          
          Its Really Easy...
          
    Github - https://github.com/BLUE-DEVIL1134/
    
    Telegram - https://t.me/AKASH_AM1
</details>

## Prepare folder

Choose an empty folder for the future build, for example **D:\\TBuild**. It will be named ***BuildPath*** in the rest of this document. Create two folders there, ***BuildPath*\\ThirdParty** and ***BuildPath*\\Libraries**.

All commands (if not stated otherwise) will be launched from **x86 Native Tools Command Prompt for VS 2019.bat** (should be in **Start Menu > Visual Studio 2019** menu folder). Pay attention not to use any other Command Prompt.

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

## Install third party software

* Download **Strawberry Perl** installer from [http://strawberryperl.com/](http://strawberryperl.com/) and install to ***BuildPath*\\ThirdParty\\Strawberry**
* Download **NASM** installer from [http://www.nasm.us](http://www.nasm.us) and install to ***BuildPath*\\ThirdParty\\NASM**
* Download **Yasm** executable from [http://yasm.tortall.net/Download.html](http://yasm.tortall.net/Download.html), rename to *yasm.exe* and put to ***BuildPath*\\ThirdParty\\yasm**
* Download **MSYS2** installer from [http://www.msys2.org/](http://www.msys2.org/) and install to ***BuildPath*\\ThirdParty\\msys64**
* Download **jom** archive from [http://download.qt.io/official_releases/jom/jom.zip](http://download.qt.io/official_releases/jom/jom.zip) and unpack to ***BuildPath*\\ThirdParty\\jom**
* Download **Python 2.7** installer from [https://www.python.org/downloads/](https://www.python.org/downloads/) and install to ***BuildPath*\\ThirdParty\\Python27**
* Download **CMake** installer from [https://cmake.org/download/](https://cmake.org/download/) and install to ***BuildPath*\\ThirdParty\\cmake**
* Download **Ninja** executable from [https://github.com/ninja-build/ninja/releases/download/v1.7.2/ninja-win.zip](https://github.com/ninja-build/ninja/releases/download/v1.7.2/ninja-win.zip) and unpack to ***BuildPath*\\ThirdParty\\Ninja**
* Download **Git** installer from [https://git-scm.com/download/win](https://git-scm.com/download/win) and install it.

Open **x86 Native Tools Command Prompt for VS 2019.bat**, go to ***BuildPath*** and run
```
cd ThirdParty
git clone https://github.com/desktop-app/patches.git
cd patches
git checkout ddd4084
cd ../
git clone https://chromium.googlesource.com/external/gyp
cd gyp
git checkout 9f2a7bb1
git apply ../patches/gyp.diff
cd ..\..
```
Add **GYP** and **Ninja** to your PATH:

* Open **Control Panel** -> **System** -> **Advanced system settings**
* Press **Environment Variables...**
* Select **Path**
* Press **Edit**
* Add ***BuildPath*\\ThirdParty\\gyp** value
* Add ***BuildPath*\\ThirdParty\\Ninja** value

## Clone source code and prepare libraries

Open **x86 Native Tools Command Prompt for VS 2019.bat**, go to ***BuildPath*** and
 Create A File In The File Named **build.py** and then in The **Native Tools Command Prompt** run
```commandline
python build.py
```


## Build The App
* Open ***BuildPath*\\tdesktop\\out\\Telegram.sln** in Visual Studio 2019
* Select Telegram project and press Build > Build Telegram (Debug and Release configurations)
* The result Telegram.exe will be located in **D:\TBuild\tdesktop\out\Debug** (and **Release**)

### Qt Visual Studio Tools

For better debugging you may want to install Qt Visual Studio Tools:

* Open **Extensions** -> **Manage Extensions**
* Go to **Online** tab
* Search for **Qt**
* Install **Qt Visual Studio Tools** extension

[api_credentials]: api_credentials.md
