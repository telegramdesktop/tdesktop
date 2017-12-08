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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "core/version.h"
#include "settings.h"

constexpr str_const AppNameOld = "Telegram Win (Unofficial)";
constexpr str_const AppName = "Telegram Desktop";

constexpr str_const AppId = "{53F49750-6209-4FBF-9CA8-7A333C87D1ED}"; // used in updater.cpp and Setup.iss for Windows
constexpr str_const AppFile = "Telegram";

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

	MTPKillFileSessionTimeout = 5000, // how much time without upload / download causes additional session kill

	MTPDebugBufferSize = 1024 * 1024, // 1 mb start size

	MaxUsersPerInvite = 100, // max users in one super group invite request

	MTPPingDelayDisconnect = 60, // 1 min
	MTPPingSendAfterAuto = 30, // send new ping starting from 30 seconds (add to existing container)
	MTPPingSendAfter = 45, // send new ping after 45 seconds without ping

	MTPChannelGetDifferenceLimit = 100,

	MaxSelectedItems = 100,

	MaxPhoneCodeLength = 4, // max length of country phone code
	MaxPhoneTailLength = 32, // rest of the phone number, without country code (seen 12 at least), need more for service numbers

	MaxScrollSpeed = 37, // 37px per 15ms while select-by-drag
	FingerAccuracyThreshold = 3, // touch flick ignore 3px
	MaxScrollAccelerated = 4000, // 4000px per second
	MaxScrollFlick = 2500, // 2500px per second

	LocalEncryptIterCount = 4000, // key derivation iteration count
	LocalEncryptNoPwdIterCount = 4, // key derivation iteration count without pwd (not secure anyway)
	LocalEncryptSaltSize = 32, // 256 bit

	AnimationTimerDelta = 7,
	ClipThreadsCount = 8,
	AverageGifSize = 320 * 240,
	WaitBeforeGifPause = 200, // wait 200ms for gif draw before pausing it
	RecentInlineBotsLimit = 10,

	AVBlockSize = 4096, // 4Kb for ffmpeg blocksize

	SaveWindowPositionTimeout = 1000, // 1 sec

	AutoSearchTimeout = 900, // 0.9 secs
	SearchPerPage = 50,
	SearchManyPerPage = 100,
	LinksOverviewPerPage = 12,
	MediaOverviewStartPerPage = 5,

	AudioVoiceMsgMaxLength = 100 * 60, // 100 minutes
	AudioVoiceMsgUpdateView = 100, // 100ms
	AudioVoiceMsgChannels = 2, // stereo
	AudioVoiceMsgBufferSize = 256 * 1024, // 256 Kb buffers (1.3 - 3.0 secs)

	StickerMaxSize = 2048, // 2048x2048 is a max image size for sticker

	MaxZoomLevel = 7, // x8
	ZoomToScreenLevel = 1024, // just constant

	ShortcutsCountLimit = 256, // how many shortcuts can be in json file

	PreloadHeightsCount = 3, // when 3 screens to scroll left make a preload request

	SearchPeopleLimit = 5,
	MinUsernameLength = 5,
	MaxUsernameLength = 32,
	UsernameCheckTimeout = 200,

	MaxPhotoCaption = 200,

	MaxMessageSize = 4096,

	WriteMapTimeout = 1000,
	SaveDraftTimeout = 1000, // save draft after 1 secs of not changing text
	SaveDraftAnywayTimeout = 5000, // or save anyway each 5 secs
	SaveCloudDraftIdleTimeout = 14000, // save draft to the cloud after 14 more seconds

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
};

inline bool isNotificationsUser(uint64 id) {
	return (id == 333000) || (id == ServiceUserId);
}

inline bool isServiceUser(uint64 id) {
	return !(id % 1000);// (id == 333000) || (id == ServiceUserId);
}

#ifdef Q_OS_WIN
inline const GUID &cGUID() {
#ifndef OS_MAC_STORE
	static const GUID gGuid = { 0x87a94ab0, 0xe370, 0x4cde, { 0x98, 0xd3, 0xac, 0xc1, 0x10, 0xc5, 0x96, 0x7d } };
#else // OS_MAC_STORE
	static const GUID gGuid = { 0xe51fb841, 0x8c0b, 0x4ef9, { 0x9e, 0x9e, 0x5a, 0x0, 0x78, 0x56, 0x76, 0x27 } };
#endif // OS_MAC_STORE

	return gGuid;
}
#endif

inline const char *cGUIDStr() {
#ifndef OS_MAC_STORE
	static const char *gGuidStr = "{87A94AB0-E370-4cde-98D3-ACC110C5967D}";
#else // OS_MAC_STORE
	static const char *gGuidStr = "{E51FB841-8C0B-4EF9-9E9E-5A0078567627}";
#endif // OS_MAC_STORE

	return gGuidStr;
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

static const char *UpdatesPublicAlphaKey = "\
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

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#error "Only little endian is supported!"
#endif // Q_BYTE_ORDER == Q_BIG_ENDIAN

#ifndef BETA_VERSION_MACRO
#error "Beta version macro is not defined."
#endif

#if (defined CUSTOM_API_ID) && (BETA_VERSION_MACRO > 0ULL)
#include "../../../TelegramPrivate/beta_private.h" // private key for downloading closed betas
#else
static const char *BetaPrivateKey = "";
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

enum {
	DialogsFirstLoad = 20, // first dialogs part size requested
	DialogsPerPage = 500, // next dialogs part size

	FileLoaderQueueStopTimeout = 5000,

    UseBigFilesFrom = 10 * 1024 * 1024, // mtp big files methods used for files greater than 10mb

	UploadPartSize = 32 * 1024, // 32kb for photo
    DocumentMaxPartsCount = 3000, // no more than 3000 parts
    DocumentUploadPartSize0 = 32 * 1024, // 32kb for tiny document ( < 1mb )
    DocumentUploadPartSize1 = 64 * 1024, // 64kb for little document ( <= 32mb )
    DocumentUploadPartSize2 = 128 * 1024, // 128kb for small document ( <= 375mb )
    DocumentUploadPartSize3 = 256 * 1024, // 256kb for medium document ( <= 750mb )
    DocumentUploadPartSize4 = 512 * 1024, // 512kb for large document ( <= 1500mb )
    UploadRequestInterval = 500, // one part each half second, if not uploaded faster

	MaxPhotosInMemory = 50, // try to clear some memory after 50 photos are created
	NoUpdatesTimeout = 60 * 1000, // if nothing is received in 1 min we ping
	NoUpdatesAfterSleepTimeout = 60 * 1000, // if nothing is received in 1 min when was a sleepmode we ping
	WaitForSkippedTimeout = 1000, // 1s wait for skipped seq or pts in updates
	WaitForChannelGetDifference = 1000, // 1s wait after show channel history before sending getChannelDifference

	MemoryForImageCache = 64 * 1024 * 1024, // after 64mb of unpacked images we try to clear some memory
	NotifySettingSaveTimeout = 1000, // wait 1 second before saving notify setting to server
	UpdateChunk = 100 * 1024, // 100kb parts when downloading the update
	IdleMsecs = 60 * 1000, // after 60secs without user input we think we are idle

	SendViewsTimeout = 1000, // send views each second

	ForwardOnAdd = 100, // how many messages from chat history server should forward to user, that was added to this chat
};

inline const QRegularExpression &cRussianLetters() {
	static QRegularExpression regexp(QString::fromUtf8("[а-яА-ЯёЁ]"));
	return regexp;
}

inline const QStringList &cImgExtensions() {
	static QStringList result;
	if (result.isEmpty()) {
		result.reserve(4);
		result.push_back(qsl(".jpg"));
		result.push_back(qsl(".jpeg"));
		result.push_back(qsl(".png"));
		result.push_back(qsl(".gif"));
	}
	return result;
}

inline const QStringList &cExtensionsForCompress() {
	static QStringList result;
	if (result.isEmpty()) {
		result.push_back(qsl(".jpg"));
		result.push_back(qsl(".jpeg"));
		result.push_back(qsl(".png"));
	}
	return result;
}
