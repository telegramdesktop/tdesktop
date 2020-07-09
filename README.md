# Unofficial Telegram Desktop x64

The source code is published under GPLv3 with OpenSSL exception, the license is available [here][license].

[![Preview of Telegram Desktop x64][preview_image]][preview_image_url]

## Project Goal

Provide Windows 64bit build with some enhancements. *(Linux build is a PLUS!)*

Cause official Telegram Desktop do not provide Windows 64bit build, so [Project TDesktop x64](https://github.com/TDesktop-x64) is aimed at provide Windows native x64 build(with few enhancements) to everybody.

## Roadmap

1. Drop Windows 32bit support in 1 September.

## Features 

*(Some features were taken from [Kotatogram](https://github.com/kotatogram/kotatogram-desktop))*

1. Show Chat ID
2. Show admin titles in member list
3. Network(Download/Upload) Boost Setting
4. Show chat restriction reason on profile page
5. Ban members option in Recent Actions
6. Always show discuss button if channel has discussion group
7. Copy inline button callback data to Clipboard
8. Expose all chat permissions setting
9. Show bot privacy in member list and profile
10. Show admin title in admin list
11. Search Messages From User(Right click user pic or member list)
12. Repeat user message to current group
13. Recent Actions/Admins list button in top bar
14. Show service message time
15. Show message ID in tooltip
16. Gif Shared Media section
17. Don't share my phone number when add someone to contacts
18. Multiple accounts increase to 10
19. Support multiple chat forward
20. Support Forward Message without quote

## Supported systems

Windows 7 and above

Linux 64 bit

The latest version is available on the [Release](https://github.com/TDesktop-x64/tdesktop/releases) page.

## Build instructions

* [Visual Studio 2019 x64][msvc_x64]
* [CMake on GNU/Linux][cmake]

## Links

* [Official Telegram Channel](https://t.me/tg_x64)

[//]: # (LINKS)
[license]: LICENSE
[msvc_x64]: docs/building-msvc_x64.md
[cmake]: docs/building-cmake.md
[preview_image]: https://github.com/TDesktop-x64/tdesktop/blob/dev/docs/assets/preview.png "Preview of Telegram Desktop x64"
[preview_image_url]: https://raw.githubusercontent.com/TDesktop-x64/tdesktop/dev/docs/assets/preview.png
