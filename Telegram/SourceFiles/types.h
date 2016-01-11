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

template <typename T>
void deleteAndMark(T *&link) {
	delete link;
	link = reinterpret_cast<T*>(0x00000BAD);
}

template <typename T>
T *exchange(T *&ptr) {
	T *result = 0;
	qSwap(result, ptr);
	return result;
}

struct NullType {
};

template <typename T>
class OrderedSet : public QMap<T, NullType> {
public:

	void insert(const T &v) {
		QMap<T, NullType>::insert(v, NullType());
	}

};

//typedef unsigned char uchar; // Qt has uchar
typedef qint16 int16;
typedef quint16 uint16;
typedef qint32 int32;
typedef quint32 uint32;
typedef qint64 int64;
typedef quint64 uint64;

static const int32 ScrollMax = INT_MAX;

extern uint64 _SharedMemoryLocation[];
template <typename T, unsigned int N>
T *SharedMemoryLocation() {
	static_assert(N < 4, "Only 4 shared memory locations!");
	return reinterpret_cast<T*>(_SharedMemoryLocation + N);
}

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

void installSignalHandlers();

void initThirdParty(); // called by Global::Initializer
void deinitThirdParty();

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

inline QString fromUtf8Safe(const char *str, int32 size = -1) {
	if (!str || !size) return QString();
	if (size < 0) size = int32(strlen(str));
	QString result(QString::fromUtf8(str, size));
	QByteArray back = result.toUtf8();
	if (back.size() != size || memcmp(back.constData(), str, size)) return QString::fromLocal8Bit(str, size);
	return result;
}

inline QString fromUtf8Safe(const QByteArray &str) {
	return fromUtf8Safe(str.constData(), str.size());
}

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
	dbiAutoDownload         = 0x34,
	dbiSavedGifsLimit       = 0x35,
	dbiShowingSavedGifs     = 0x36,
	dbiAutoPlay             = 0x37,

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

inline int32 rowscount(int32 count, int32 perrow) {
	return (count + perrow - 1) / perrow;
}
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

enum ShowLayerOption {
	CloseOtherLayers          = 0x00,
	KeepOtherLayers           = 0x01,
	ShowAfterOtherLayers      = 0x03,

	AnimatedShowLayer         = 0x00,
	ForceFastShowLayer        = 0x04,
};
typedef QFlags<ShowLayerOption> ShowLayerOptions;

static int32 FullArcLength = 360 * 16;
static int32 QuarterArcLength = (FullArcLength / 4);
static int32 MinArcLength = (FullArcLength / 360);
static int32 AlmostFullArcLength = (FullArcLength - MinArcLength);

template <typename I>
inline void destroyImplementation(I *&ptr) {
	if (ptr) {
		ptr->destroy();
		ptr = 0;
	}
	deleteAndMark(ptr);
}

template <typename R>
class FunctionImplementation {
public:
	virtual R call() = 0;
	virtual void destroy() { delete this; }
	virtual ~FunctionImplementation() {}
};
template <typename R>
class NullFunctionImplementation : public FunctionImplementation<R> {
public:
	virtual R call() { return R(); }
	virtual void destroy() {}
	static NullFunctionImplementation<R> SharedInstance;
};
template <typename R>
NullFunctionImplementation<R> NullFunctionImplementation<R>::SharedInstance;
template <typename R>
class FunctionCreator {
public:
	FunctionCreator(FunctionImplementation<R> *ptr) : _ptr(ptr) {}
	FunctionCreator(const FunctionCreator<R> &other) : _ptr(other.create()) {}
	FunctionImplementation<R> *create() const { return exchange(_ptr); }
	~FunctionCreator() { destroyImplementation(_ptr); }
private:
	FunctionCreator<R> &operator=(const FunctionCreator<R> &other);
	mutable FunctionImplementation<R> *_ptr;
};
template <typename R>
class Function {
public:
	typedef FunctionCreator<R> Creator;
	static Creator Null() { return Creator(&NullFunctionImplementation<R>::SharedInstance); }
	Function(const Creator &creator) : _implementation(creator.create()) {}
	R call() { return _implementation->call(); }
	~Function() { destroyImplementation(_implementation); }
private:
	Function(const Function<R> &other);
	Function<R> &operator=(const Function<R> &other);
	FunctionImplementation<R> *_implementation;
};

template <typename R>
class WrappedFunction : public FunctionImplementation<R> {
public:
	typedef R(*Method)();
	WrappedFunction(Method method) : _method(method) {}
	virtual R call() { return (*_method)(); }
private:
	Method _method;
};
template <typename R>
inline FunctionCreator<R> func(R(*method)()) {
	return FunctionCreator<R>(new WrappedFunction<R>(method));
}
template <typename O, typename I, typename R>
class ObjectFunction : public FunctionImplementation<R> {
public:
	typedef R(I::*Method)();
	ObjectFunction(O *obj, Method method) : _obj(obj), _method(method) {}
	virtual R call() { return (_obj->*_method)(); }
private:
	O *_obj;
	Method _method;
};
template <typename O, typename I, typename R>
inline FunctionCreator<R> func(O *obj, R(I::*method)()) {
	return FunctionCreator<R>(new ObjectFunction<O, I, R>(obj, method));
}

template <typename R, typename A1>
class Function1Implementation {
public:
	virtual R call(A1 a1) = 0;
	virtual void destroy() { delete this; }
	virtual ~Function1Implementation() {}
};
template <typename R, typename A1>
class NullFunction1Implementation : public Function1Implementation<R, A1> {
public:
	virtual R call(A1 a1) { return R(); }
	virtual void destroy() {}
	static NullFunction1Implementation<R, A1> SharedInstance;
};
template <typename R, typename A1>
NullFunction1Implementation<R, A1> NullFunction1Implementation<R, A1>::SharedInstance;
template <typename R, typename A1>
class Function1Creator {
public:
	Function1Creator(Function1Implementation<R, A1> *ptr) : _ptr(ptr) {}
	Function1Creator(const Function1Creator<R, A1> &other) : _ptr(other.create()) {}
	Function1Implementation<R, A1> *create() const { return exchange(_ptr); }
	~Function1Creator() { destroyImplementation(_ptr); }
private:
	Function1Creator<R, A1> &operator=(const Function1Creator<R, A1> &other);
	mutable Function1Implementation<R, A1> *_ptr;
};
template <typename R, typename A1>
class Function1 {
public:
	typedef Function1Creator<R, A1> Creator;
	static Creator Null() { return Creator(&NullFunction1Implementation<R, A1>::SharedInstance); }
	Function1(const Creator &creator) : _implementation(creator.create()) {}
	R call(A1 a1) { return _implementation->call(a1); }
	~Function1() { _implementation->destroy(); }
private:
	Function1(const Function1<R, A1> &other);
	Function1<R, A1> &operator=(const Function1<R, A1> &other);
	Function1Implementation<R, A1> *_implementation;
};

template <typename R, typename A1>
class WrappedFunction1 : public Function1Implementation<R, A1> {
public:
	typedef R(*Method)(A1);
	WrappedFunction1(Method method) : _method(method) {}
	virtual R call(A1 a1) { return (*_method)(a1); }
private:
	Method _method;
};
template <typename R, typename A1>
inline Function1Creator<R, A1> func(R(*method)(A1)) {
	return Function1Creator<R, A1>(new WrappedFunction1<R, A1>(method));
}
template <typename O, typename I, typename R, typename A1>
class ObjectFunction1 : public Function1Implementation<R, A1> {
public:
	typedef R(I::*Method)(A1);
	ObjectFunction1(O *obj, Method method) : _obj(obj), _method(method) {}
	virtual R call(A1 a1) { return (_obj->*_method)(a1); }
private:
	O *_obj;
	Method _method;
};
template <typename O, typename I, typename R, typename A1>
Function1Creator<R, A1> func(O *obj, R(I::*method)(A1)) {
	return Function1Creator<R, A1>(new ObjectFunction1<O, I, R, A1>(obj, method));
}

template <typename R, typename A1, typename A2>
class Function2Implementation {
public:
	virtual R call(A1 a1, A2 a2) = 0;
	virtual void destroy() { delete this; }
	virtual ~Function2Implementation() {}
};
template <typename R, typename A1, typename A2>
class NullFunction2Implementation : public Function2Implementation<R, A1, A2> {
public:
	virtual R call(A1 a1, A2 a2) { return R(); }
	virtual void destroy() {}
	static NullFunction2Implementation<R, A1, A2> SharedInstance;
};
template <typename R, typename A1, typename A2>
NullFunction2Implementation<R, A1, A2> NullFunction2Implementation<R, A1, A2>::SharedInstance;
template <typename R, typename A1, typename A2>
class Function2Creator {
public:
	Function2Creator(Function2Implementation<R, A1, A2> *ptr) : _ptr(ptr) {}
	Function2Creator(const Function2Creator<R, A1, A2> &other) : _ptr(other.create()) {}
	Function2Implementation<R, A1, A2> *create() const { return exchange(_ptr); }
	~Function2Creator() { destroyImplementation(_ptr); }
private:
	Function2Creator<R, A1, A2> &operator=(const Function2Creator<R, A1, A2> &other);
	mutable Function2Implementation<R, A1, A2> *_ptr;
};
template <typename R, typename A1, typename A2>
class Function2 {
public:
	typedef Function2Creator<R, A1, A2> Creator;
	static Creator Null() { return Creator(&NullFunction2Implementation<R, A1, A2>::SharedInstance); }
	Function2(const Creator &creator) : _implementation(creator.create()) {}
	R call(A1 a1, A2 a2) { return _implementation->call(a1, a2); }
	~Function2() { destroyImplementation(_implementation); }
private:
	Function2(const Function2<R, A1, A2> &other);
	Function2<R, A1, A2> &operator=(const Function2<R, A1, A2> &other);
	Function2Implementation<R, A1, A2> *_implementation;
};

template <typename R, typename A1, typename A2>
class WrappedFunction2 : public Function2Implementation<R, A1, A2> {
public:
	typedef R(*Method)(A1, A2);
	WrappedFunction2(Method method) : _method(method) {}
	virtual R call(A1 a1, A2 a2) { return (*_method)(a1, a2); }
private:
	Method _method;
};
template <typename R, typename A1, typename A2>
Function2Creator<R, A1, A2> func(R(*method)(A1, A2)) {
	return Function2Creator<R, A1, A2>(new WrappedFunction2<R, A1, A2>(method));
}

template <typename O, typename I, typename R, typename A1, typename A2>
class ObjectFunction2 : public Function2Implementation<R, A1, A2> {
public:
	typedef R(I::*Method)(A1, A2);
	ObjectFunction2(O *obj, Method method) : _obj(obj), _method(method) {}
	virtual R call(A1 a1, A2 a2) { return (_obj->*_method)(a1, a2); }
private:
	O *_obj;
	Method _method;
};
template <typename O, typename I, typename R, typename A1, typename A2>
Function2Creator<R, A1, A2> func(O *obj, R(I::*method)(A1, A2)) {
	return Function2Creator<R, A1, A2>(new ObjectFunction2<O, I, R, A1, A2>(obj, method));
}
