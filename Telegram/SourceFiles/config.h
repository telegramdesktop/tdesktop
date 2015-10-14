/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

static const int32 AppVersion = 9006;
static const wchar_t *AppVersionStr = L"0.9.6";
static const bool DevVersion = false;

static const wchar_t *AppNameOld = L"Telegram Win (Unofficial)";
static const wchar_t *AppName = L"Telegram Desktop";

static const wchar_t *AppId = L"{53F49750-6209-4FBF-9CA8-7A333C87D1ED}"; // used in updater.cpp and Setup.iss for Windows
static const wchar_t *AppFile = L"Telegram";

#include "settings.h"

enum {
	MTPShortBufferSize = 65535, // of ints, 256 kb
	MTPPacketSizeMax = 67108864, // 64 mb
	MTPIdsBufferSize = 400, // received msgIds and wereAcked msgIds count stored
	MTPCheckResendTimeout = 10000, // how much time passed from send till we resend request or check it's state, in ms
	MTPCheckResendWaiting = 1000, // how much time to wait for some more requests, when resending request or checking it's state, in ms
	MTPAckSendWaiting = 10000, // how much time to wait for some more requests, when sending msg acks
	MTPResendThreshold = 1, // how much ints should message contain for us not to resend, but to check it's state
	MTPContainerLives = 600, // container lives 10 minutes in haveSent map
	MTPMinReceiveDelay = 4000, // 4 seconds
	MTPMaxReceiveDelay = 64000, // 64 seconds
	MTPMinConnectDelay = 1000, // tcp connect should take less then 1 second
	MTPMaxConnectDelay = 8000, // tcp connect should take 8 seconds max
	MTPConnectionOldTimeout = 192000, // 192 seconds
	MTPTcpConnectionWaitTimeout = 2000, // 2 seconds waiting for tcp, until we accept http
	MTPIPv4ConnectionWaitTimeout = 1000, // 1 seconds waiting for ipv4, until we accept ipv6
	MTPMillerRabinIterCount = 30, // 30 Miller-Rabin iterations for dh_prime primality check

	MTPUploadSessionsCount = 4, // max 4 upload sessions is created
	MTPDownloadSessionsCount = 4, // max 4 download sessions is created
	MTPKillFileSessionTimeout = 5000, // how much time without upload / download causes additional session kill

	MTPEnumDCTimeout = 4000, // 4 seconds timeout for help_getConfig to work (them move to other dc)

	MTPDebugBufferSize = 1024 * 1024, // 1 mb start size

	MTPPingDelayDisconnect = 60, // 1 min
	MTPPingSendAfterAuto = 30, // send new ping starting from 30 seconds (add to existing container)
	MTPPingSendAfter = 45, // send new ping after 45 seconds without ping

	MTPChannelGetDifferenceLimit = 100,

	MaxSelectedItems = 100,

	MaxPhoneCodeLength = 4, // max length of country phone code
	MaxPhoneTailLength = 18, // rest of the phone number, without country code (seen 12 at least)

	MaxScrollSpeed = 37, // 37px per 15ms while select-by-drag
	FingerAccuracyThreshold = 3, // touch flick ignore 3px
	MaxScrollAccelerated = 4000, // 4000px per second
	MaxScrollFlick = 2500, // 2500px per second

	LocalEncryptIterCount = 4000, // key derivation iteration count
	LocalEncryptNoPwdIterCount = 4, // key derivation iteration count without pwd (not secure anyway)
	LocalEncryptSaltSize = 32, // 256 bit
	LocalEncryptKeySize = 256, // 2048 bit

	AnimationTimerDelta = 7,

	SaveRecentEmojisTimeout = 3000, // 3 secs
	SaveWindowPositionTimeout = 1000, // 1 sec

	AutoSearchTimeout = 900, // 0.9 secs
	SearchPerPage = 50,
	SearchManyPerPage = 100,
	LinksOverviewPerPage = 12,
	MediaOverviewStartPerPage = 5,
	MediaOverviewPreloadCount = 4,

	AudioVoiceMsgSimultaneously = 4,
	AudioSongSimultaneously = 4,
	AudioCheckPositionTimeout = 100, // 100ms per check audio pos
	AudioCheckPositionDelta = 2400, // update position called each 2400 samples
	AudioFadeTimeout = 7, // 7ms
	AudioFadeDuration = 500,
	AudioVoiceMsgSkip = 400, // 200ms
	AudioVoiceMsgFade = 300, // 300ms
	AudioPreloadSamples = 5 * 48000, // preload next part if less than 5 seconds remains
	AudioVoiceMsgFrequency = 48000, // 48 kHz
	AudioVoiceMsgMaxLength = 100 * 60, // 100 minutes
	AudioVoiceMsgUpdateView = 100, // 100ms
	AudioVoiceMsgChannels = 2, // stereo
	AudioVoiceMsgBufferSize = 1024 * 1024, // 1 Mb buffers
	AudioVoiceMsgInMemory = 1024 * 1024, // 1 Mb audio is hold in memory and auto loaded
	AudioPauseDeviceTimeout = 3000, // pause in 3 secs after playing is over

	StickerInMemory = 1024 * 1024, // 1024 Kb stickers hold in memory, auto loaded and displayed inline
	StickerMaxSize = 2048, // 2048x2048 is a max image size for sticker

	MediaViewImageSizeLimit = 100 * 1024 * 1024, // show up to 100mb jpg/png/gif docs in app
	MaxZoomLevel = 7, // x8
	ZoomToScreenLevel = 1024, // just constant

	PreloadHeightsCount = 3, // when 3 screens to scroll left make a preload request
	EmojiPanPerRow = 7,
	EmojiPanRowsPerPage = 6,
	StickerPanPerRow = 5,
	StickerPanRowsPerPage = 4,
	StickersUpdateTimeout = 3600000, // update not more than once in an hour

	SearchPeopleLimit = 5,
	MinUsernameLength = 5,
	MaxUsernameLength = 32,
	UsernameCheckTimeout = 200,

	MaxChannelDescription = 120,
	MaxGroupChannelTitle = 255,
	MaxPhotoCaption = 140,

	MaxMessageSize = 4096,
	MaxHttpRedirects = 5, // when getting external data/images

	WriteMapTimeout = 1000,
	SaveDraftTimeout = 1000, // save draft after 1 secs of not changing text
	SaveDraftAnywayTimeout = 5000, // or save anyway each 5 secs

	SetOnlineAfterActivity = 30, // user with hidden last seen stays online for such amount of seconds in the interface

	ServiceUserId = 777000,
	WebPageUserId = 701000,

	CacheBackgroundTimeout = 3000, // cache background scaled image after 3s
	BackgroundsInRow = 3,

	UpdateDelayConstPart = 8 * 3600, // 8 hour min time between update check requests
	UpdateDelayRandPart = 8 * 3600, // 8 hour max - min time between update check requests

	WrongPasscodeTimeout = 1500,
	SessionsShortPollTimeout = 60000,

	ChoosePeerByDragTimeout = 1000, // 1 second mouse not moved to choose dialog when dragging a file
	ReloadChannelMembersTimeout = 1000, // 1 second wait before reload members in channel after adding
};

inline bool isNotificationsUser(uint64 id) {
	return (id == 333000) || (id == ServiceUserId);
}

inline bool isServiceUser(uint64 id) {
	return !(id % 1000);// (id == 333000) || (id == ServiceUserId);
}

#ifdef Q_OS_WIN
inline const GUID &cGUID() {
	static const GUID gGuid = { 0x87a94ab0, 0xe370, 0x4cde, { 0x98, 0xd3, 0xac, 0xc1, 0x10, 0xc5, 0x96, 0x7d } };

	return gGuid;
}
#endif

inline const char *cGUIDStr() {
	static const char *gGuidStr = "{87A94AB0-E370-4cde-98D3-ACC110C5967D}";

	return gGuidStr;
}

inline const char **cPublicRSAKeys(uint32 &cnt) {
	static const char *(keys[]) = {"\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAwVACPi9w23mF3tBkdZz+zwrzKOaaQdr01vAbU4E1pvkfj4sqDsm6\n\
lyDONS789sVoD/xCS9Y0hkkC3gtL1tSfTlgCMOOul9lcixlEKzwKENj1Yz/s7daS\n\
an9tqw3bfUV/nqgbhGX81v/+7RFAEd+RwFnK7a+XYl9sluzHRyVVaTTveB2GazTw\n\
Efzk2DWgkBluml8OREmvfraX3bkHZJTKX4EQSjBbbdJ2ZXIsRrYOXfaA+xayEGB+\n\
8hdlLmAjbCVfaigxX0CDqWeR1yFL9kwd9P0NsZRPsmoqVwMbMu7mStFai6aIhc3n\n\
Slv8kg9qv1m6XHVQY3PnEw+QQtqSIXklHwIDAQAB\n\
-----END RSA PUBLIC KEY-----"};
	cnt = sizeof(keys) / sizeof(const char*);
	return keys;
}

struct BuiltInDc {
	int id;
	const char *ip;
	int port;
};

static const BuiltInDc _builtInDcs[] = {
	{ 1, "149.154.175.50", 443 },
	{ 2, "149.154.167.51", 443 },
	{ 3, "149.154.175.100", 443 },
	{ 4, "149.154.167.91", 443 },
	{ 5, "149.154.171.5", 443 }
};

static const BuiltInDc _builtInDcsIPv6[] = {
	{ 1, "2001:b28:f23d:f001::a", 443 },
	{ 2, "2001:67c:4e8:f002::a", 443 },
	{ 3, "2001:b28:f23d:f003::a", 443 },
	{ 4, "2001:67c:4e8:f004::a", 443 },
	{ 5, "2001:b28:f23f:f005::a", 443 }
};

static const BuiltInDc _builtInTestDcs[] = {
	{ 1, "149.154.175.10", 443 },
	{ 2, "149.154.167.40", 443 },
	{ 3, "149.154.175.117", 443 }
};

static const BuiltInDc _builtInTestDcsIPv6[] = {
	{ 1, "2001:b28:f23d:f001::e", 443 },
	{ 2, "2001:67c:4e8:f002::e", 443 },
	{ 3, "2001:b28:f23d:f003::e", 443 }
};

inline const BuiltInDc *builtInDcs() {
	return cTestMode() ? _builtInTestDcs : _builtInDcs;
}

inline int builtInDcsCount() {
	return (cTestMode() ? sizeof(_builtInTestDcs) : sizeof(_builtInDcs)) / sizeof(BuiltInDc);
}

inline const BuiltInDc *builtInDcsIPv6() {
	return cTestMode() ? _builtInTestDcsIPv6 : _builtInDcsIPv6;
}

inline int builtInDcsCountIPv6() {
	return (cTestMode() ? sizeof(_builtInTestDcsIPv6) : sizeof(_builtInDcsIPv6)) / sizeof(BuiltInDc);
}

static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAMA4ViQrjkPZ9xj0lrer3r23JvxOnrtE8nI69XLGSr+sRERz9YnUptnU\n\
BZpkIfKaRcl6XzNJiN28cVwO1Ui5JSa814UAiDHzWUqCaXUiUEQ6NmNTneiGx2sQ\n\
+9PKKlb8mmr3BB9A45ZNwLT6G9AK3+qkZLHojeSA+m84/a6GP4svAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

static const char *UpdatesPublicDevKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBALWu9GGs0HED7KG7BM73CFZ6o0xufKBRQsdnq3lwA8nFQEvmdu+g/I1j\n\
0LQ+0IQO7GW4jAgzF/4+soPDb6uHQeNFrlVx1JS9DZGhhjZ5rf65yg11nTCIHZCG\n\
w/CVnbwQOw0g5GBwwFV3r0uTTvy44xx8XXxk+Qknu4eBCsmrAFNnAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

#ifdef CUSTOM_API_ID
#include "../../../TelegramPrivate/custom_api_id.h" // Custom API id and API hash
#else
static const int32 ApiId = 17349;
static const char *ApiHash = "344583e45741c457fe1862106095a5eb";
#endif

inline const char *cApiDeviceModel() {
#ifdef Q_OS_WIN
	return "PC";
#elif defined Q_OS_MAC
	return "Mac";
#elif defined Q_OS_LINUX
	return "PC";
#endif
}
inline const char *cApiSystemVersion() {
#ifdef Q_OS_WIN
	return "Windows";
#elif defined Q_OS_MAC
	return "OS X";
#elif defined Q_OS_LINUX
	return "Linux";
#endif
}
inline QString cApiAppVersion() {
	return QString::number(AppVersion);
}
static const char *ApiLang = "en";

extern QString gKeyFile;
inline const QString &cDataFile() {
	if (!gKeyFile.isEmpty()) return gKeyFile;
	static const QString res(qsl("data"));
	return res;
}

inline const QString &cTempDir() {
	static const QString res = cWorkingDir() + qsl("tdata/tdld/");
	return res;
}

static const char *DefaultCountry = "US";
static const char *DefaultLanguage = "en";

enum {
	DefaultChatBackground = 21,

	DialogsFirstLoad = 20, // first dialogs part size requested
	DialogsPerPage = 500, // next dialogs part size

	MessagesFirstLoad = 30, // first history part size requested
	MessagesPerPage = 50, // next history part size

	DownloadPartSize = 64 * 1024, // 64kb for photo
	DocumentDownloadPartSize = 128 * 1024, // 128kb for document
	MaxUploadPhotoSize = 32 * 1024 * 1024, // 32mb photos max
    MaxUploadDocumentSize = 1500 * 1024 * 1024, // 1500mb documents max
    UseBigFilesFrom = 10 * 1024 * 1024, // mtp big files methods used for files greater than 10mb
	MaxFileQueries = 16, // max 16 file parts downloaded at the same time

	UploadPartSize = 32 * 1024, // 32kb for photo
    DocumentMaxPartsCount = 3000, // no more than 3000 parts
    DocumentUploadPartSize0 = 32 * 1024, // 32kb for tiny document ( < 1mb )
    DocumentUploadPartSize1 = 64 * 1024, // 64kb for little document ( <= 32mb )
    DocumentUploadPartSize2 = 128 * 1024, // 128kb for small document ( <= 375mb )
    DocumentUploadPartSize3 = 256 * 1024, // 256kb for medium document ( <= 750mb )
    DocumentUploadPartSize4 = 512 * 1024, // 512kb for large document ( <= 1500mb )
    MaxUploadFileParallelSize = MTPUploadSessionsCount * 512 * 1024, // max 512kb uploaded at the same time in each session
    UploadRequestInterval = 500, // one part each half second, if not uploaded faster

	MaxPhotosInMemory = 50, // try to clear some memory after 50 photos are created
	NoUpdatesTimeout = 60 * 1000, // if nothing is received in 1 min we ping
	NoUpdatesAfterSleepTimeout = 60 * 1000, // if nothing is received in 1 min when was a sleepmode we ping
	WaitForSkippedTimeout = 1000, // 1s wait for skipped seq or pts in updates
	WaitForChannelGetDifference = 1000, // 1s wait after show channel history before sending getChannelDifference

	MemoryForImageCache = 64 * 1024 * 1024, // after 64mb of unpacked images we try to clear some memory
	NotifyWindowsCount = 3, // 3 desktop notifies at the same time
	NotifySettingSaveTimeout = 1000, // wait 1 second before saving notify setting to server
	NotifyDeletePhotoAfter = 60000, // delete notify photo after 1 minute
	UpdateChunk = 100 * 1024, // 100kb parts when downloading the update
	IdleMsecs = 60 * 1000, // after 60secs without user input we think we are idle

	UpdateFullChannelTimeout = 5000, // not more than once in 5 seconds
	SendViewsTimeout = 1000, // send views each second

	ForwardOnAdd = 100, // how many messages from chat history server should forward to user, that was added to this chat
};

inline const QRegularExpression &cWordSplit() {
	static QRegularExpression regexp(qsl("[\\@\\s\\-\\+\\)\\(\\,\\.\\:\\!\\_\\;\\\"\\'\\x0]"));
	return regexp;
}

inline const QRegularExpression &cRussianLetters() {
	static QRegularExpression regexp(QString::fromUtf8("[а-яА-ЯёЁ]"));
	return regexp;
}

inline QStringList cImgExtensions() {
	static QStringList imgExtensions;
	if (imgExtensions.isEmpty()) {
		imgExtensions.reserve(4);
		imgExtensions.push_back(qsl(".jpg"));
		imgExtensions.push_back(qsl(".jpeg"));
		imgExtensions.push_back(qsl(".png"));
		imgExtensions.push_back(qsl(".gif"));
	}
	return imgExtensions;
}

inline QStringList cPhotoExtensions() {
	static QStringList photoExtensions;
	if (photoExtensions.isEmpty()) {
		photoExtensions.push_back(qsl(".jpg"));
		photoExtensions.push_back(qsl(".jpeg"));
	}
	return photoExtensions;
}
