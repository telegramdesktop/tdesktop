/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

static const int32 AppVersion = 5005;
static const wchar_t *AppVersionStr = L"0.5.5";
#ifdef Q_OS_WIN
static const wchar_t *AppName = L"Telegram Win (Unofficial)";
#else
static const wchar_t *AppName = L"Telegram Desktop";
#endif
static const wchar_t *AppId = L"{53F49750-6209-4FBF-9CA8-7A333C87D1ED}";

#include "settings.h"

enum {
	MTPShortBufferSize = 65535, // of ints, 256 kb
	MTPPacketSizeMax = 67108864, // 64 mb
	MTPIdsBufferSize = 400, // received msgIds and wereAcked msgIds count stored
	MTPCheckResendTimeout = 5000, // how much time passed from send till we resend request or check it's state, in ms
	MTPCheckResendWaiting = 1000, // how much time to wait for some more requests, when resending request or checking it's state, in ms
	MTPResendThreshold = 1, // how much ints should message contain for us not to resend, but to check it's state
	MTPContainerLives = 600, // container lives 10 minutes in haveSent map
	MTPMaxReceiveDelay = 64000, // 64 seconds
	MTPConnectionOldTimeout = 192000, // 192 seconds
	MTPTcpConnectionWaitTimeout = 3000, // 3 seconds waiting for tcp, until we accept http
	MTPMillerRabinIterCount = 30, // 30 Miller-Rabin iterations for dh_prime primality check

	MinReceiveDelay = 1000, // 1 seconds
	MaxSelectedItems = 100,

	MaxPhoneTailLength = 18, // rest of the phone number, without country code (seen 12 at least)

	MaxScrollSpeed = 37, // 37px per 15ms while select-by-drag
	FingerAccuracyThreshold = 3, // touch flick ignore 3px
	MaxScrollAccelerated = 4000, // 4000px per second
	MaxScrollFlick = 2500, // 2500px per second

	LocalEncryptIterCount = 4000, // key derivation iteration count
	LocalEncryptNoPwdIterCount = 4, // key derivation iteration count without pwd (not secure anyway)
	LocalEncryptSaltSize = 32, // 256 bit
	LocalEncryptKeySize = 256, // 2048 bit

	SaveRecentEmojisTimeout = 3000, // 3 secs
};

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

inline const char *cFirstDCIp() {
	return cTestMode() ? "173.240.5.253" : "173.240.5.1";
}

inline int32 cFirstDCPort() {
	return 443;
}

static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAMA4ViQrjkPZ9xj0lrer3r23JvxOnrtE8nI69XLGSr+sRERz9YnUptnU\n\
BZpkIfKaRcl6XzNJiN28cVwO1Ui5JSa814UAiDHzWUqCaXUiUEQ6NmNTneiGx2sQ\n\
+9PKKlb8mmr3BB9A45ZNwLT6G9AK3+qkZLHojeSA+m84/a6GP4svAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

#ifdef CUSTOM_API_ID
#include "../../../TelegramPrivate/custom_api_id.h" // Custom API id and API hash
#else
static const int32 ApiId = 17349;
static const char *ApiHash = "344583e45741c457fe1862106095a5eb";
#endif

inline const char *cApiDeviceModel() {
	return "x86 desktop";
}
inline const char *cApiSystemVersion() {
	return "windows";
}
inline QString cApiAppVersion() {
	return QString::number(AppVersion);
}
static const char *ApiLang = "en";

extern QString gKeyFile;
inline const QString &cDataFile() {
	if (!gKeyFile.isEmpty()) return gKeyFile;
	static const QString res(cTestMode() ? qsl("data_test") : qsl("data"));
	return res;
}

inline const QString &cTempDir() {
	static const QString res = cWorkingDir() + qsl("tdata/tdld/");
	return res;
}

static const char *DefaultCountry = "US";
static const char *DefaultLanguage = "en";

enum {
	DialogsFirstLoad = 20, // first dialogs part size requested
	DialogsPerPage = 40, // next dialogs part size

	MessagesFirstLoad = 30, // first history part size requested
	MessagesPerPage = 50, // next history part size

	LinkCropLimit = 360, // 360px link length max

	DownloadPartSize = 32 * 1024, // 32kb for photo
	DocumentDownloadPartSize = 128 * 1024, // 128kb for document
	MaxUploadPhotoSize = 10 * 1024 * 1024, // 10mb photos max
    MaxUploadDocumentSize = 1500 * 1024 * 1024, // 1500mb documents max
    UseBigFilesFrom = 10 * 1024 * 1024, // mtp big files methods used for files greater than 10mb
	MaxFileQueries = 32, // max 32 file parts downloaded at the same time

	UploadPartSize = 32 * 1024, // 32kb for photo
    DocumentMaxPartsCount = 3000, // no more than 3000 parts
    DocumentUploadPartSize0 = 32 * 1024, // 32kb for tiny document ( < 1mb )
    DocumentUploadPartSize1 = 64 * 1024, // 64kb for little document ( <= 32mb )
    DocumentUploadPartSize2 = 128 * 1024, // 128kb for small document ( <= 375mb )
    DocumentUploadPartSize3 = 256 * 1024, // 256kb for medium document ( <= 750mb )
    DocumentUploadPartSize4 = 512 * 1024, // 512kb for large document ( <= 1500mb )
    MaxUploadFileParallelSize = 512 * 1024, // max 512kb uploaded at the same time
    UploadRequestInterval = 500, // one part each half second, if not uploaded faster

	MaxPhotosInMemory = 50, // try to clear some memory after 50 photos are created
	NoUpdatesTimeout = 180 * 1000, // if nothing is received in 3 min we reconnect

	MemoryForImageCache = 64 * 1024 * 1024, // after 64mb of unpacked images we try to clear some memory
	NotifyWindows = 3, // 3 desktop notifies at the same time
	NotifyWaitTimeout = 1200, // 1.2 seconds timeout before notification
	NotifySettingSaveTimeout = 1000, // wait 1 second before saving notify setting to server
	UpdateChunk = 100 * 1024, // 100kb parts when downloading the update
	IdleMsecs = 60 * 1000, // after 60secs without user input we think we are idle

	ForwardOnAdd = 120, // how many messages from chat history server should forward to user, that was added to this chat
};

inline const QRegularExpression &cWordSplit() {
	static QRegularExpression regexp(qsl("[\\s\\-\\+\\)\\(\\,\\.\\:\\!\\_\\;\\\"\\'\\x0]"));
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
