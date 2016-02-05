# [Telegram Desktop][telegram_desktop] – Official Messenger

This is the complete source code and the build instructions for the alpha version of the official desktop client for the [Telegram][telegram] messenger, based on the [Telegram API][telegram_api] and the [MTProto][telegram_proto] secure protocol.

The source code is published under GPLv3 with OpenSSL exception, the license is available [here][license].

## Supported systems

* Windows XP - Windows 10 (**not** RT)
* Mac OS X 10.8 - Mac OS X 10.10
* Mac OS X 10.6 - Mac OS X 10.7 (separate build)
* Ubuntu 12.04 - Ubuntu 14.04
* Fedora 22

## Third-party libraries

* Qt 5.3.2 and 5.5.1, slightly patched ([LGPL](http://doc.qt.io/qt-5/lgpl.html))
* OpenSSL 1.0.1g ([OpenSSL License](https://www.openssl.org/source/license.html))
* zlib 1.2.8 ([zlib License](http://www.zlib.net/zlib_license.html))
* libexif 0.6.20 ([LGPL](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Google Breakpad ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
* Google Crashpad ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
* OpenAL Soft ([LGPL](http://kcat.strangesoft.net/openal.html))
* Opus codec ([BSD license](http://www.opus-codec.org/license/))
* FFmpeg ([LGPL](https://www.ffmpeg.org/legal.html))
* Open Sans font ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))

## Build instructions

* [Visual Studio 2015][msvc]
* [XCode 7][xcode]
* [XCode 7 for OS X 10.6 and 10.7][xcode_old]
* [Qt Creator 3.5.1 Ubuntu][qtcreator]

## Projects in Telegram solution

* ### Telegram

  [Telegram Desktop][telegram_desktop] messenger

* ### Updater

  A little app, that is launched by Telegram when update is ready, replaces all files and launches it back.

* ### Packer

  Compiles given files to single update file, compresses it with lzma and signs with a private key. It is not built in **Debug** and **Release** configurations of Telegram solution, because private key is inaccessible.

* ### MetaEmoji

  Creates four sprites and text2emoji replace code
  * SourceFiles/art/emoji.png
  * SourceFiles/art/emoji_125x.png
  * SourceFiles/art/emoji_150x.png
  * SourceFiles/art/emoji_200x.png
  * SourceFiles/art/emoji_250x.png
  * SourceFiles/gui/emoji_config.cpp

* ### MetaStyle

  From two files and two sprites
  * Resources/style_classes.txt
  * Resources/style.txt
  * SourceFiles/art/sprite.png
  * SourceFiles/art/sprite_200x.png

  Creates two other sprites, four sprite grids and style constants code
  * SourceFiles/art/sprite_125x.png
  * SourceFiles/art/sprite_150x.png
  * SourceFiles/art/grid.png
  * SourceFiles/art/grid_125x.png
  * SourceFiles/art/grid_150x.png
  * SourceFiles/art/grid_200x.png
  * GeneratedFiles/style_classes.h
  * GeneratedFiles/style_auto.h
  * GeneratedFiles/style_auto.cpp

* ### MetaLang

  Creates from languagepack file `Resources/lang.txt` language constants code and language file parse code:
  * GeneratedFiles/lang.h
  * GeneratedFiles/lang.cpp

[//]: # (LINKS)
[telegram]: https://telegram.org
[telegram_desktop]: https://desktop.telegram.org
[telegram_api]: https://core.telegram.org
[telegram_proto]: https://core.telegram.org/mtproto
[license]: LICENSE
[msvc]: MSVC.md
[xcode]: XCODE.md
[xcode_old]: XCODEold.md
[qtcreator]: QTCREATOR.md
