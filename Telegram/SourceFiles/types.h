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

struct NullType {
};

//typedef unsigned char uchar; // Qt has uchar
typedef qint16 int16;
typedef quint16 uint16;
typedef qint32 int32;
typedef quint32 uint32;
typedef qint64 int64;
typedef quint64 uint64;

static const int32 ScrollMax = INT_MAX;

#ifdef Q_OS_WIN
typedef float float32;
typedef double float64;
#else
typedef float float32;
typedef double float64;
#endif

#include <string>
#include <exception>

#include <QtCore/QReadWriteLock>

#include <ctime>

using std::string;
using std::exception;
using std::swap;

#include "logs.h"

class Exception : public exception {
public:

    Exception(const QString &msg, bool isFatal = true) : _fatal(isFatal), _msg(msg.toUtf8()) {
		LOG(("Exception: %1").arg(msg));
	}
	bool fatal() const {
		return _fatal;
	}

    virtual const char *what() const throw() {
        return _msg.constData();
    }
    virtual ~Exception() throw() {
    }

private:
	bool _fatal;
    QByteArray _msg;
};

class MTPint;

int32 myunixtime();
void unixtimeInit();
void unixtimeSet(int32 servertime, bool force = false);
int32 unixtime();
int32 fromServerTime(const MTPint &serverTime);
uint64 msgid();
int32 reqid();

inline QDateTime date(int32 time = -1) {
	QDateTime result;
	if (time >= 0) result.setTime_t(time);
	return result;
}

inline QDateTime date(const MTPint &time) {
	return date(fromServerTime(time));
}

inline void mylocaltime(struct tm * _Tm, const time_t * _Time) {
#ifdef Q_OS_WIN
    localtime_s(_Tm, _Time);
#else
    localtime_r(_Time, _Tm);
#endif
}

class InitOpenSSL {
public:
	InitOpenSSL();
	~InitOpenSSL();
};

bool checkms(); // returns true if time has changed
uint64 getms(bool checked = false);

class SingleTimer : public QTimer { // single shot timer with check
	Q_OBJECT
	
public:

	SingleTimer();

	void setSingleShot(bool); // is not available
	void start(); // is not available

public slots:

	void start(int msec);
	void startIfNotActive(int msec);
	void adjust() {
		uint64 n = getms(true);
		if (isActive()) {
			if (n >= _finishing) {
				start(0);
			} else {
				start(_finishing - n);
			}
		}
	}

private:
	uint64 _finishing;
	bool _inited;

};

const static uint32 _md5_block_size = 64;
class HashMd5 {
public:

	HashMd5(const void *input = 0, uint32 length = 0);
	void feed(const void *input, uint32 length);
	int32 *result();

private:

	void init();
	void finalize();
	void transform(const uchar *block);

	bool _finalized;
	uchar _buffer[_md5_block_size];
	uint32 _count[2];
	uint32 _state[4];
	uchar _digest[16];

};

int32 hashCrc32(const void *data, uint32 len);
int32 *hashSha1(const void *data, uint32 len, void *dest); // dest - ptr to 20 bytes, returns (int32*)dest
int32 *hashSha256(const void *data, uint32 len, void *dest); // dest - ptr to 32 bytes, returns (int32*)dest
int32 *hashMd5(const void *data, uint32 len, void *dest); // dest = ptr to 16 bytes, returns (int32*)dest
char *hashMd5Hex(const int32 *hashmd5, void *dest); // dest = ptr to 32 bytes, returns (char*)dest
inline char *hashMd5Hex(const void *data, uint32 len, void *dest) { // dest = ptr to 32 bytes, returns (char*)dest
	return hashMd5Hex(HashMd5(data, len).result(), dest);
}

void memset_rand(void *data, uint32 len);

template <typename T>
inline void memsetrnd(T &value) {
	memset_rand(&value, sizeof(value));
}

class ReadLockerAttempt {
public:

    ReadLockerAttempt(QReadWriteLock *_lock) : success(_lock->tryLockForRead()), lock(_lock) {
	}
	~ReadLockerAttempt() {
		if (success) {
			lock->unlock();
		}
	}

	operator bool() const {
		return success;
	}

private:

	bool success;
	QReadWriteLock *lock;

};

#define qsl(s) QStringLiteral(s)
#define qstr(s) QLatin1String(s, sizeof(s) - 1)

static const QRegularExpression::PatternOptions reMultiline(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::MultilineOption);

template <typename T>
inline T snap(const T &v, const T &_min, const T &_max) {
	return (v < _min) ? _min : ((v > _max) ? _max : v);
}

template <typename T>
class ManagedPtr {
public:
	ManagedPtr() : ptr(0) {
	}
	ManagedPtr(T *p) : ptr(p) {
	}
	T *operator->() const {
		return ptr;
	}
	T *v() const {
		return ptr;
	}

protected:

	T *ptr;
	typedef ManagedPtr<T> Parent;
};

QString translitRusEng(const QString &rus);
QString rusKeyboardLayoutSwitch(const QString &from);

enum DataBlockId {
	dbiKey                  = 0x00,
	dbiUser                 = 0x01,
	dbiDcOptionOld          = 0x02,
	dbiMaxGroupCount        = 0x03,
	dbiMutePeer             = 0x04,
	dbiSendKey              = 0x05,
	dbiAutoStart            = 0x06,
	dbiStartMinimized       = 0x07,
	dbiSoundNotify          = 0x08,
	dbiWorkMode             = 0x09,
	dbiSeenTrayTooltip      = 0x0a,
	dbiDesktopNotify        = 0x0b,
	dbiAutoUpdate           = 0x0c,
	dbiLastUpdateCheck      = 0x0d,
	dbiWindowPosition       = 0x0e,
	dbiConnectionType       = 0x0f,
// 0x10 reserved
	dbiDefaultAttach        = 0x11,
	dbiCatsAndDogs          = 0x12,
	dbiReplaceEmojis        = 0x13,
	dbiAskDownloadPath      = 0x14,
	dbiDownloadPathOld      = 0x15,
	dbiScale                = 0x16,
	dbiEmojiTabOld          = 0x17,
	dbiRecentEmojisOld      = 0x18,
	dbiLoggedPhoneNumber    = 0x19,
	dbiMutedPeers           = 0x1a,
// 0x1b reserved
	dbiNotifyView           = 0x1c,
	dbiSendToMenu           = 0x1d,
	dbiCompressPastedImage  = 0x1e,
	dbiLang                 = 0x1f,
	dbiLangFile             = 0x20,
	dbiTileBackground       = 0x21,
	dbiAutoLock             = 0x22,
	dbiDialogLastPath       = 0x23,
	dbiRecentEmojis         = 0x24,
	dbiEmojiVariants        = 0x25,
	dbiRecentStickers       = 0x26,
	dbiDcOption             = 0x27,
	dbiTryIPv6              = 0x28,
	dbiSongVolume           = 0x29,
	dbiWindowsNotifications = 0x30,
	dbiIncludeMuted         = 0x31,
	dbiMaxMegaGroupCount    = 0x32,
	dbiDownloadPath         = 0x33,

	dbiEncryptedWithSalt    = 333,
	dbiEncrypted            = 444,

	// 500-600 reserved

	dbiVersion              = 666,
};

enum DBISendKey {
	dbiskEnter = 0,
	dbiskCtrlEnter = 1,
};

enum DBINotifyView {
	dbinvShowPreview = 0,
	dbinvShowName = 1,
	dbinvShowNothing = 2,
};

enum DBIWorkMode {
	dbiwmWindowAndTray = 0,
	dbiwmTrayOnly = 1,
	dbiwmWindowOnly = 2,
};

enum DBIConnectionType {
	dbictAuto = 0,
	dbictHttpAuto = 1, // not used
	dbictHttpProxy = 2,
	dbictTcpProxy = 3,
};

enum DBIDefaultAttach {
	dbidaDocument = 0,
	dbidaPhoto = 1,
};

struct ConnectionProxy {
	ConnectionProxy() : port(0) {
	}
	QString host;
	uint32 port;
	QString user, password;
};

enum DBIScale {
	dbisAuto          = 0,
	dbisOne           = 1,
	dbisOneAndQuarter = 2,
	dbisOneAndHalf    = 3,
	dbisTwo           = 4,

	dbisScaleCount    = 5,
};

static const int MatrixRowShift = 40000;

enum DBIEmojiTab {
	dbietRecent   = -1,
	dbietPeople   =  0,
	dbietNature   =  1,
	dbietFood     =  2,
	dbietActivity =  3,
	dbietTravel   =  4,
	dbietObjects  =  5,
	dbietSymbols  =  6,
	dbietStickers =  666,
};
static const int emojiTabCount = 8;
inline DBIEmojiTab emojiTabAtIndex(int index) {
	return (index < 0 || index >= emojiTabCount) ? dbietRecent : DBIEmojiTab(index - 1);
}

enum DBIPlatform {
    dbipWindows  = 0,
    dbipMac      = 1,
    dbipLinux64  = 2,
    dbipLinux32  = 3,
	dbipMacOld   = 4,
};

enum DBIPeerReportSpamStatus {
	dbiprsNoButton,
	dbiprsUnknown,
	dbiprsShowButton,
	dbiprsReportSent,
};

typedef enum {
	HitTestNone = 0,
	HitTestClient,
	HitTestSysButton,
	HitTestIcon,
	HitTestCaption,
	HitTestTop,
	HitTestTopRight,
	HitTestRight,
	HitTestBottomRight,
	HitTestBottom,
	HitTestBottomLeft,
	HitTestLeft,
	HitTestTopLeft,
} HitTestType;

inline QString strMakeFromLetters(const uint32 *letters, int32 len) {
	QString result;
	result.reserve(len);
	for (int32 i = 0; i < len; ++i) {
		result.push_back(QChar((((letters[i] << 16) & 0xFF) >> 8) | (letters[i] & 0xFF)));
	}
	return result;
}

class MimeType {
public:

	enum TypeEnum {
		Unknown,
		WebP,
	};

	MimeType(const QMimeType &type) : _typeStruct(type), _type(Unknown) {
	}
	MimeType(TypeEnum type) : _type(type) {
	}
	QStringList globPatterns() const;
	QString filterString() const;
	QString name() const;

private:

	QMimeType _typeStruct;
	TypeEnum _type;

};

MimeType mimeTypeForName(const QString &mime);
MimeType mimeTypeForFile(const QFileInfo &file);
MimeType mimeTypeForData(const QByteArray &data);

inline int32 floorclamp(int32 value, int32 step, int32 lowest, int32 highest) {
	return qMin(qMax(value / step, lowest), highest);
}
inline int32 floorclamp(float64 value, int32 step, int32 lowest, int32 highest) {
	return qMin(qMax(qFloor(value / step), lowest), highest);
}
inline int32 ceilclamp(int32 value, int32 step, int32 lowest, int32 highest) {
	return qMax(qMin((value / step) + ((value % step) ? 1 : 0), highest), lowest);
}
inline int32 ceilclamp(float64 value, int32 step, int32 lowest, int32 highest) {
	return qMax(qMin(qCeil(value / step), highest), lowest);
}

enum ForwardWhatMessages {
	ForwardSelectedMessages,
	ForwardContextMessage,
	ForwardPressedMessage,
	ForwardPressedLinkMessage
};
