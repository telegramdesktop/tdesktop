# Threat model

## What Telegram Desktop is and where untrusted input enters

Telegram Desktop is the official desktop client for Telegram, written in C++20 with Qt 6, for Windows, macOS and
Linux. It speaks MTProto 2.0 to Telegram's servers itself (`Telegram/SourceFiles/mtproto/`), keeps an encrypted local
cache (`Telegram/SourceFiles/storage/`), and renders everything that users, bots and channels send. The desktop-app
libraries are submodules under `Telegram/lib_*` (UI and text layout in `lib_ui`, Lottie in `lib_lottie`, TL
serialization in `lib_tl`, webviews in `lib_webview`, call glue in `lib_webrtc`, and more). They are part of this
project and in scope. Paths below are relative to `Telegram/SourceFiles/` unless they start with `Telegram/`.

The main attacker is any other Telegram user, bot or channel. Anyone can message the victim, add them to a group or
post in a channel they read. Everything such a party controls is untrusted, even though it arrives from Telegram's
servers over an authenticated MTProto session:

* message text, entities and formatting (`api/api_text_entities.cpp`, `Telegram/lib_ui/ui/text/`), rich messages and
  Instant View pages, including markdown and formulas (`iv/`, `iv/markdown/`);
* names, usernames, bios, file names, captions, polls, contacts, locations, reply and inline keyboards, inline bot
  results (`inline_bots/`), and link previews (`data/data_web_page.cpp`);
* media: photos, videos, GIFs, voice and video messages, documents, stickers in every format (Lottie TGS through
  `lib_lottie`, WebM, WebP), custom emoji, wallpapers and patterns, themes (`window/themes/`), and cloud language
  packs (`lang/`). Much of this is downloaded and decoded automatically, without a click (`media/`, `ffmpeg/`,
  `history/view/media/`);
* links: `tg://` and `t.me` links handled in `core/local_url_handlers.cpp`, including proxy links that change
  network settings;
* bot Mini Apps, payment forms and Instant View webviews. They load web pages written by arbitrary bot or site
  owners, which talk to the client through the JavaScript bridge in `ui/chat/attach/attach_bot_webview.cpp`,
  `inline_bots/bot_attach_web_view.cpp`, `iv/` and `Telegram/lib_webview`;
* calls and group calls (`calls/`, `tde2e/`, `Telegram/ThirdParty/tgcalls`): the other party's signaling and media;
* received files that the user saves or opens: `core/file_utilities.cpp`, `data/data_document_resolver.cpp`, and
  `core/mime_type.cpp` (`Core::DetectNameType`, which decides when to warn that a file may be dangerous to open).

The second attacker is on the network: a malicious MTProxy, SOCKS5 or HTTP proxy, or anyone on the path. It sees
only MTProto ciphertext but controls everything before decryption: the transports and the fake-TLS handshake
(`mtproto/connection_*.cpp`, `mtproto/details/mtproto_tls_socket.cpp`, `mtproto/details/mtproto_tcp_socket.cpp`),
auth key creation and server RSA key checks (`mtproto/details/mtproto_dc_key_creator.cpp`,
`mtproto/details/mtproto_rsa_public_key.cpp`), and decryption and integrity checks of incoming packets
(`mtproto/session_private.cpp`).

The third is the update channel. `core/update_checker.cpp`, `core/update_verify.cpp` and `core/update_unpack.cpp`
download, verify and unpack signed update packages.

Lower priority: other local programs that talk to the single-instance socket in `core/sandbox.cpp` or start the app
with a `tg://` link on the command line, and attacker content written into Export Chat History output (`export/`),
which the user later opens in a browser.

We treat Telegram's servers as honest relays. Data that only the server controls (configuration, data center
options, fields no other user can set) is less important than data another user controls, and a report that needs a
malicious Telegram server should say so. The local user, the operating system and the files in the user's profile
are trusted.

## Components that matter most and least

Most important:

* `mtproto/`: everything before and around decryption, and session state;
* `api/`, `data/`, `history/`: turning server updates into client objects, and rendering messages;
* `media/`, `ffmpeg/`, `Telegram/lib_lottie`, `Telegram/lib_ui` (text layout, images, emoji): decoding and
  drawing attacker-supplied content;
* `iv/`, `inline_bots/`, `ui/chat/attach/`, `payments/`, `Telegram/lib_webview`: webviews and their bridges to
  native code;
* `core/` (link handlers, file opening, the updater), `window/themes/`, `lang/`;
* `storage/` (local encrypted storage and serialization), `passport/` (Telegram Passport decryption), `calls/`,
  `tde2e/`, `webauthn/` (passkeys, including the caBLE tunnel).

Lower priority: settings, boxes and other UI layout code, `info/`, `statistics/`, `intro/`, `export/`. Code in
`platform/win/` and `platform/mac/` is in scope for review, but it isn't built in this Linux image.

Out of scope:

* the libraries prebuilt in the base image (Qt, FFmpeg, OpenSSL, WebRTC, OpenAL and others), except where
  Telegram Desktop misuses them;
* vendored code in `Telegram/ThirdParty/`, unless content from Telegram reaches the bug. Then name the upstream
  project;
* `Telegram/SourceFiles/test/` and anything else under `#ifdef _DEBUG`. This image is a Debug build, but that code
  is not in release builds;
* `Telegram/build/`, `docs/`, and `Telegram/SourceFiles/tests/`.

## How to exercise it

* `/src/out/Debug/Telegram` is built with `-O0 -g` and AddressSanitizer. The image sets
  `ASAN_OPTIONS=detect_leaks=0`. The build tree uses Ninja Multi-Config:
  `cmake --build /src/out --config Debug --target Telegram` rebuilds incrementally, and
  `/src/out/compile_commands.json` has the exact flags for every source file.
* The app needs a display and Telegram's servers, and the scan has neither. The practical way to reproduce a bug is
  a small harness that feeds the crafted input to the function in question. Build it with the flags from
  `compile_commands.json` and link it against the static libraries in `/src/out` (`find /src/out -name '*.a'`).
  The generated TL (de)serialization code is under `/src/out` too (`find /src/out -name 'scheme*'`).
* `out/Debug/test_update_verify` and `out/Debug/test_update_unpack` are standalone harnesses of this kind. Their
  sources are in `Telegram/SourceFiles/tests/` and their CMake in `Telegram/cmake/test_update_*.cmake`.
* For received content, show the TL object (constructor and fields) another user can get delivered, and say whether
  the server is likely to relay it unchanged. Entity offsets, file names and sizes may be validated server-side. For
  network issues, show the bytes the proxy or network attacker sends.

## How we rate severity

* Critical: code execution or memory corruption from content another user, bot or channel sends, with no
  interaction beyond receiving it or opening the chat (auto-downloaded media, sticker, emoji and link preview
  rendering). Getting an attacker's file executed without the dangerous-file warning. Bypassing update signature
  verification. Reading or forging MTProto traffic, or obtaining the auth key, as a proxy or network attacker.
  Remotely reading local files or session keys.
* High: memory corruption that needs one ordinary action, such as opening a file, playing media, or opening an Instant
  View page, a Mini App or a link. A Mini App or web page reaching native features without the confirmation the user
  normally sees: sending messages, sharing the phone number, reading local files, or opening other URL schemes.
  Writing outside the chosen folder through an attacker-chosen file name. Out-of-bounds reads that send memory
  contents back to the attacker. A received message that crashes the client on every launch.
* Medium: crashes from received content that go away after a restart (null dereference, uncaught exception, or a
  failed `Assert`, `Expects` or `Unexpected`, which stay fatal in release builds). Leaking the user's IP address to a
  third party without a click or around the configured proxy. UI spoofing that hides a link's real target or a
  file's real extension.
* Low: resource exhaustion without a crash. Script injection into Export Chat History output. Issues that need a
  malicious Telegram server rank one level below the same issue from a field another user controls.

A buffer overflow, use-after-free or double free reachable from received content is High at least, even without a
demonstrated exploit.

## Anything to leave alone

* Memory leaks. LeakSanitizer is off on purpose.
* `api_id` and `api_hash` values in the source, which are public by design (`docs/api_credentials.md`).
* Attacks by someone who already has the user's local account or files, including reading the profile folder of an
  account without a local passcode.
* Bugs in the prebuilt libraries listed above, unless Telegram Desktop's use of them is the problem.
