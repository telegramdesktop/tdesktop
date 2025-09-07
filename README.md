# Telegram Desktop

[![Version](https://badge.fury.io/gh/telegramdesktop%2Ftdesktop.svg)](https://github.com/telegramdesktop/tdesktop/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Windows Build](https://github.com/telegramdesktop/tdesktop/workflows/Windows./badge.svg)](https://github.com/telegramdesktop/tdesktop/actions)
[![macOS Build](https://github.com/telegramdesktop/tdesktop/workflows/MacOS./badge.svg)](https://github.com/telegramdesktop/tdesktop/actions)
[![Linux Build](https://github.com/telegramdesktop/tdesktop/workflows/Linux./badge.svg)](https://github.com/telegramdesktop/tdesktop/actions)

This is the official open-source repository for the **Telegram Desktop** messenger, built on the [Telegram API](https://core.telegram.org) and the [MTProto](https://core.telegram.org/mtproto) secure protocol.

![Preview of Telegram Desktop](https://raw.githubusercontent.com/telegramdesktop/tdesktop/dev/docs/assets/preview.png)

## Key Features

*   **Fast and Secure:** Send messages, photos, videos, and files of any type (doc, zip, mp3, etc.) quickly and securely.
*   **Groups & Channels:** Create group chats with up to 200,000 members and create channels for broadcasting to unlimited audiences.
*   **Voice & Video Calls:** Make crystal-clear, end-to-end encrypted voice and video calls.
*   **Stickers, Emoji, and GIFs:** Express yourself with a massive collection of custom sticker sets and animated GIFs.
*   **Cloud Storage:** Enjoy free, unlimited cloud storage for your chats and media, accessible from any device.
*   **Cross-Device Sync:** Seamlessly start a conversation on your phone and finish it on your desktop.
*   **Customization:** Tailor the look and feel of your app with customizable themes and chat backgrounds.
*   **Bots and API:** An open API and bot platform for developers to build integrated tools and services.

## Installation

Telegram Desktop is available for a variety of operating systems.

| Platform                | Installer                                                              | Portable Version                                                              |
| ----------------------- | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| **Windows (x64)**       | [Download](https://telegram.org/dl/desktop/win64)                      | [Download](https://telegram.org/dl/desktop/win64_portable)                    |
| **Windows (x86)**       | [Download](https://telegram.org/dl/desktop/win)                        | [Download](https://telegram.org/dl/desktop/win_portable)                      |
| **macOS (10.13+)**      | [Download](https://telegram.org/dl/desktop/mac)                        |                                                                               |
| **Linux (x64)**         | [Static Binary](https://telegram.org/dl/desktop/linux)                 |                                                                               |
| **Linux (Snap)**        | `sudo snap install telegram-desktop`                                   |                                                                               |
| **Linux (Flatpak)**     | [Install from Flathub](https://flathub.org/apps/details/org.telegram.desktop) |                                                                               |

### Support for Older Systems

For users on legacy operating systems, specific older versions of Telegram Desktop are available:

*   **Version 4.9.9:**
    *   macOS 10.12: [Download](https://updates.tdesktop.com/tmac/tsetup.4.9.9.dmg)
    *   Linux with glibc < 2.28: [Download](https://updates.tdesktop.com/tlinux/tsetup.4.9.9.tar.xz)
*   **Version 2.4.4:**
    *   OS X 10.10 and 10.11: [Download](https://updates.tdesktop.com/tosx/tsetup-osx.2.4.4.dmg)
    *   Linux (32-bit): [Download](https://updates.tdesktop.com/tlinux32/tsetup32.2.4.4.tar.xz)
*   **Version 1.8.15:**
    *   Windows XP and Vista: [Installer](https://updates.tdesktop.com/tsetup/tsetup.1.8.15.exe) / [Portable](https://updates.tdesktop.com/tsetup/tportable.1.8.15.zip)
    *   OS X 10.8 and 10.9: [Download](https://updates.tdesktop.com/tmac/tsetup.1.8.15.dmg)
    *   OS X 10.6 and 10.7: [Download](https://updates.tdesktop.com/tmac32/tsetup32.1.8.15.dmg)

## Building from Source

If you prefer to compile Telegram Desktop yourself, follow the detailed build instructions for your platform:

*   [Building on Windows (64-bit)](docs/building-win-x64.md)
*   [Building on Windows (32-bit)](docs/building-win.md)
*   [Building on macOS](docs/building-mac.md)
*   [Building on GNU/Linux](docs/building-linux.md)

## Contributing

We welcome contributions from the community! Please read `CONTRIBUTING.md` and `CODE_OF_CONDUCT.md` before opening issues or pull requests.

If you'd like to contribute, fork the repository, create a branch for your change, and submit a pull request. For bug reports and feature requests, please open an issue in the [Issues](https://github.com/telegramdesktop/tdesktop/issues) section.

Quick pull request checklist:

* Keep changes small and focused (one logical change per PR).
* Include a clear description of what the change does and why.
* Ensure the project builds and any existing tests pass locally.
* Reference the related issue (if any) in the PR description.

Thanks for contributing — small, thoughtful improvements are always appreciated.

## License

The source code is published under the **GNU General Public License v3.0 with an OpenSSL exception**. See the [LICENSE](LICENSE) file for the full text.

## Third-Party Libraries

Telegram Desktop is built using a number of fantastic open-source libraries and tools.

<details>
<summary>Click to view all third-party libraries and their licenses</summary>

*   **Ada** ([Apache License 2.0](https://github.com/ada-url/ada/blob/main/LICENSE-APACHE))
*   **CMake** ([New BSD License](https://github.com/Kitware/CMake/blob/master/Copyright.txt))
*   **Emoji alpha codes** ([MIT License](https://github.com/emojione/emojione/blob/master/extras/alpha-codes/LICENSE.md))
*   **FFmpeg** ([LGPL](https://www.ffmpeg.org/legal.html))
*   **Google Breakpad** ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
*   **Google Crashpad** ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
*   **Guideline Support Library** ([MIT License](https://github.com/Microsoft/GSL/blob/master/LICENSE))
*   **GYP** ([BSD License](https://github.com/bnoordhuis/gyp/blob/master/LICENSE))
*   **Hunspell** ([LGPL](https://github.com/hunspell/hunspell/blob/master/COPYING.LESSER))
*   **liblzma** ([Public Domain](http://tukaani.org/xz/))
*   **LZMA SDK 9.20** ([Public Domain](http://www.7-zip.org/sdk.html))
*   **Ninja** ([Apache License 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING))
*   **OpenAL Soft** ([LGPL](https://github.com/kcat/openal-soft/blob/master/COPYING))
*   **Open Sans font** ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))
*   **OpenSSL 3.2.1** ([Apache License 2.0](https://www.openssl.org/source/apache-license-2.0.txt))
*   **Opus codec** ([BSD License](http://www.opus-codec.org/license/))
*   **QR Code generator** ([MIT License](https://github.com/nayuki/QR-Code-generator#license))
*   **Qt 6** ([LGPL](http://doc.qt.io/qt-6/lgpl.html)) and **Qt 5.15** ([LGPL](http://doc.qt.io/qt-5/lgpl.html))
*   **Range-v3** ([Boost License](https://github.com/ericniebler/range-v3/blob/master/LICENSE.txt))
*   **Vazirmatn font** ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))
*   **WebRTC** ([New BSD License](https://github.com/desktop-app/tg_owt/blob/master/LICENSE))
*   **xxHash** ([BSD License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE))
*   **zlib 1.2.11** ([zlib License](http://www.zlib.net/zlib_license.html))

</details>