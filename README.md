## [Telegram D](https://tdesktop.com) – Unofficial Telegram Desktop App

This is complete source code and build instructions for alpha version of unofficial desktop client for [Telegram](https://telegram.org) messenger, based on [Telegram API](https://core.telegram.org/) and [MTProto](https://core.telegram.org/mtproto) secure protocol.

Source code is published under GPL v3, license is available [here](https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE).

###Supported systems

Only Windows and OS X systems are supported at this moment, Linux builds are on their way.

* Windows XP
* Windows Vista
* Windows 7
* Windows 8 (**not** RT)
* Windows 8.1 (**not** RT)
* OS X 10.9
* OS X 10.7-10.8 (not tested)
* Ubuntu 13.04
* Ubuntu 14.04
* Fedora (not tested)

###Third-party

* Qt 5.3.1, slightly patched ([GPL](http://qt-project.org/doc/qt-5/gpl.html))
* OpenSSL 1.0.1g ([OpenSSL License](https://www.openssl.org/source/license.html))
* zlib 1.2.8 ([zlib License](http://www.zlib.net/zlib_license.html))
* libexif 0.6.20 ([LGPL](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Open Sans font ([Apache License](http://www.apache.org/licenses/LICENSE-2.0.html))
* DejaVu Sans font ([Free license](http://dejavu-fonts.org/wiki/License))

###[Build instructions for Visual Studio 2013](https://github.com/telegramdesktop/tdesktop/blob/master/MSVC.md)

###[Build instructions for XCode 5.1.1](https://github.com/telegramdesktop/tdesktop/blob/master/XCODE.md)

###[Build instructions for Qt Creator 2.7 Ubuntu](https://github.com/telegramdesktop/tdesktop/blob/master/QTCREATOR.md)

##Projects in Telegram solution

####Telegram

tdesktop messenger

####Updater

little app, that is launched by Telegram when update is ready, replaces all files and launches it back

####Packer

compiles given files to single update file, compresses it with lzma and signs with a private key, it is not built in **Debug** and **Release** configurations of Telegram solution, because private key is inaccessible

####Prepare

prepares a release for deployment, puts all files to deploy/{version} folder, for Win:
* current tsetup{version}exe installer
* current Telegram.exe
* current Telegram.pdb (debug info for crash minidumps view)
* current tupdate{updversion} binary lzma update archive

for Mac:
* current tsetup{version}dmg
* current Telegram.app
* current tmacupd{updversion} binary lzma update archive

####MetaEmoji

from two folders
* SourceFiles/art/Emoji
* SourceFiles/art/Emoji_200x

and some inner config creates four sprites and text2emoji replace code
* SourceFiles/art/emoji.png
* SourceFiles/art/emoji_125x.png
* SourceFiles/art/emoji_150x.png
* SourceFiles/art/emoji_200x.png
* SourceFiles/gui/emoji_config.cpp

####MetaStyle

from two files and two sprites
* Resources/style_classes.txt
* Resources/style.txt
* SourceFiles/art/sprite.png
* SourceFiles/art/sprite_200x.png

creates two other sprites, four sprite grids and style constants code
* SourceFiles/art/sprite_125x.png
* SourceFiles/art/sprite_150x.png
* SourceFiles/art/grid.png
* SourceFiles/art/grid_125x.png
* SourceFiles/art/grid_150x.png
* SourceFiles/art/grid_200x.png
* GeneratedFiles/style_classes.h
* GeneratedFiles/style_auto.h
* GeneratedFiles/style_auto.cpp

####MetaLang

from langpack file
* Resources/lang.txt

creates lang constants code and lang file parse code
* GeneratedFiles/lang.h
* GeneratedFiles/lang.cpp
