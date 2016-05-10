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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

template <typename T>
void deleteAndMark(T *&link) {
	delete link;
	link = reinterpret_cast<T*>(0x00000BAD);
}

template <typename T>
T *getPointerAndReset(T *&ptr) {
	T *result = nullptr;
	qSwap(result, ptr);
	return result;
}

struct NullType {
};

// ordered set template based on QMap
template <typename T>
class OrderedSet {
	typedef OrderedSet<T> Self;
	typedef QMap<T, NullType> Impl;
	typedef typename Impl::iterator IteratorImpl;
	typedef typename Impl::const_iterator ConstIteratorImpl;
	Impl impl_;

public:

	inline bool operator==(const Self &other) const { return impl_ == other.impl_; }
	inline bool operator!=(const Self &other) const { return impl_ != other.impl_; }
	inline int size() const { return impl_.size(); }
	inline bool isEmpty() const { return impl_.isEmpty(); }
	inline void detach() { return impl_.detach(); }
	inline bool isDetached() const { return impl_.isDetached(); }
	inline void clear() { return impl_.clear(); }
	inline QList<T> values() const { return impl_.keys(); }
	inline const T &first() const { return impl_.firstKey(); }
	inline const T &last() const { return impl_.lastKey(); }

	class const_iterator;
	class iterator {
	public:
		typedef typename IteratorImpl::iterator_category iterator_category;
		typedef typename IteratorImpl::difference_type difference_type;
		typedef T value_type;
		typedef T *pointer;
		typedef T &reference;

		explicit iterator(const IteratorImpl &impl) : impl_(impl) {
		}
		inline const T &operator*() const { return impl_.key(); }
		inline const T *operator->() const { return &impl_.key(); }
		inline bool operator==(const iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const iterator &other) const { return impl_ != other.impl_; }
		inline iterator &operator++() { ++impl_; return *this; }
		inline iterator operator++(int) { return iterator(impl_++); }
		inline iterator &operator--() { --impl_; return *this; }
		inline iterator operator--(int) { return iterator(impl_--); }
		inline iterator operator+(int j) const { return iterator(impl_ + j); }
		inline iterator operator-(int j) const { return iterator(impl_ - j); }
		inline iterator &operator+=(int j) { impl_ += j; return *this; }
		inline iterator &operator-=(int j) { impl_ -= j; return *this; }

		friend class const_iterator;
		inline bool operator==(const const_iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const const_iterator &other) const { return impl_ != other.impl_; }

	private:
		IteratorImpl impl_;
		friend class OrderedSet<T>;

	};
	friend class iterator;

	class const_iterator {
	public:
		typedef typename IteratorImpl::iterator_category iterator_category;
		typedef typename IteratorImpl::difference_type difference_type;
		typedef T value_type;
		typedef T *pointer;
		typedef T &reference;

		explicit const_iterator(const ConstIteratorImpl &impl) : impl_(impl) {
		}
		inline const T &operator*() const { return impl_.key(); }
		inline const T *operator->() const { return &impl_.key(); }
		inline bool operator==(const const_iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const const_iterator &other) const { return impl_ != other.impl_; }
		inline const_iterator &operator++() { ++impl_; return *this; }
		inline const_iterator operator++(int) { return const_iterator(impl_++); }
		inline const_iterator &operator--() { --impl_; return *this; }
		inline const_iterator operator--(int) { return const_iterator(impl_--); }
		inline const_iterator operator+(int j) const { return const_iterator(impl_ + j); }
		inline const_iterator operator-(int j) const { return const_iterator(impl_ - j); }
		inline const_iterator &operator+=(int j) { impl_ += j; return *this; }
		inline const_iterator &operator-=(int j) { impl_ -= j; return *this; }

		friend class iterator;
		inline bool operator==(const iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const iterator &other) const { return impl_ != other.impl_; }

	private:
		ConstIteratorImpl impl_;
		friend class OrderedSet<T>;

	};
	friend class const_iterator;

	// STL style
	inline iterator begin() { return iterator(impl_.begin()); }
	inline const_iterator begin() const { return const_iterator(impl_.cbegin()); }
	inline const_iterator constBegin() const { return const_iterator(impl_.cbegin()); }
	inline const_iterator cbegin() const { return const_iterator(impl_.cbegin()); }
	inline iterator end() { detach(); return iterator(impl_.end()); }
	inline const_iterator end() const { return const_iterator(impl_.cend()); }
	inline const_iterator constEnd() const { return const_iterator(impl_.cend()); }
	inline const_iterator cend() const { return const_iterator(impl_.cend()); }
	inline iterator erase(iterator it) { return iterator(impl_.erase(it.impl_)); }

	inline iterator insert(const T &value) { return iterator(impl_.insert(value, NullType())); }
	inline iterator insert(const_iterator pos, const T &value) { return iterator(impl_.insert(pos.impl_, value, NullType())); }
	inline int remove(const T &value) { return impl_.remove(value); }
	inline bool contains(const T &value) const { return impl_.contains(value); }

	// more Qt
	typedef iterator Iterator;
	typedef const_iterator ConstIterator;
	inline int count() const { return impl_.count(); }
	inline iterator find(const T &value) { return iterator(impl_.find(value)); }
	inline const_iterator find(const T &value) const { return const_iterator(impl_.constFind(value)); }
	inline const_iterator constFind(const T &value) const { return const_iterator(impl_.constFind(value)); }
	inline Self &unite(const Self &other) { impl_.unite(other.impl_); return *this; }

	// STL compatibility
	typedef typename Impl::difference_type difference_type;
	typedef typename Impl::size_type size_type;
	inline bool empty() const { return impl_.empty(); }

};

// thanks Chromium see https://blogs.msdn.microsoft.com/the1/2004/05/07/how-would-you-get-the-count-of-an-array-in-c-2/
template <typename T, size_t N> char(&ArraySizeHelper(T(&array)[N]))[N];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

#define qsl(s) QStringLiteral(s)
#define qstr(s) QLatin1String(s, sizeof(s) - 1)

// using for_const instead of plain range-based for loop to ensure usage of const_iterator
// it is important for the copy-on-write Qt containers
// if you have "QVector<T*> v" then "for (T * const p : v)" will still call QVector::detach(),
// while "for_const (T *p, v)" won't and "for_const (T *&p, v)" won't compile
#define for_const(range_declaration, range_expression) for (range_declaration : std_::as_const(range_expression))

template <typename Enum>
inline QFlags<Enum> qFlags(Enum v) {
	return QFlags<Enum>(v);
}

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

// see https://github.com/boostcon/cppnow_presentations_2012/blob/master/wed/schurr_cpp11_tools_for_class_authors.pdf
class str_const { // constexpr string
public:
	template<std::size_t N>
	constexpr str_const(const char(&a)[N]) : _str(a), _size(N - 1) {
	}
	constexpr char operator[](std::size_t n) const {
		return (n < _size) ? _str[n] :
			throw std::out_of_range("");
	}
	constexpr std::size_t size() const { return _size; }
	const char *c_str() const { return _str; }

private:
	const char* const _str;
	const std::size_t _size;

};

inline QString str_const_toString(const str_const &str) {
	return QString::fromUtf8(str.c_str(), str.size());
}

template <typename T>
inline void accumulate_max(T &a, const T &b) { if (a < b) a = b; }

template <typename T>
inline void accumulate_min(T &a, const T &b) { if (a > b) a = b; }

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

// we copy some parts of C++11/14/17 std:: library, because on OS X 10.6+
// version we can use C++11/14/17, but we can not use its library :(
namespace std_ {

template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;

	using value_type = T;
	using type = integral_constant<T, V>;

	constexpr operator value_type() const noexcept {
		return (value);
	}

	constexpr value_type operator()() const noexcept {
		return (value);
	}
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template <typename T>
struct remove_reference {
	using type = T;
};
template <typename T>
struct remove_reference<T&> {
	using type = T;
};
template <typename T>
struct remove_reference<T&&> {
	using type = T;
};

template <typename T>
struct is_lvalue_reference : false_type {
};
template <typename T>
struct is_lvalue_reference<T&> : true_type {
};

template <typename T>
struct is_rvalue_reference : false_type {
};
template <typename T>
struct is_rvalue_reference<T&&> : true_type {
};

template <typename T>
inline constexpr T &&forward(typename remove_reference<T>::type &value) noexcept {
	return static_cast<T&&>(value);
}
template <typename T>
inline constexpr T &&forward(typename remove_reference<T>::type &&value) noexcept {
	static_assert(!is_lvalue_reference<T>::value, "bad forward call");
	return static_cast<T&&>(value);
}

template <typename T>
inline constexpr typename remove_reference<T>::type &&move(T &&value) noexcept {
	return static_cast<typename remove_reference<T>::type&&>(value);
}

template <typename T>
struct add_const {
	using type = const T;
};
template <typename T>
using add_const_t = typename add_const<T>::type;
template <typename T>
constexpr add_const_t<T> &as_const(T& t) noexcept {
	return t;
}
template <typename T>
void as_const(const T&&) = delete;

// This is not full unique_ptr, but at least with std interface.
template <typename T>
class unique_ptr {
public:
	constexpr unique_ptr() noexcept = default;
	unique_ptr(const unique_ptr<T> &) = delete;
	unique_ptr<T> &operator=(const unique_ptr<T> &) = delete;

	constexpr unique_ptr(std::nullptr_t) {
	}
	unique_ptr<T> &operator=(std::nullptr_t) noexcept {
		reset();
		return (*this);
	}

	explicit unique_ptr(T *p) noexcept : _p(p) {
	}

	template <typename U>
	unique_ptr(unique_ptr<U> &&other) noexcept : _p(other.release()) {
	}
	template <typename U>
	unique_ptr<T> &operator=(unique_ptr<U> &&other) noexcept {
		reset(other.release());
		return (*this);
	}
	unique_ptr<T> &operator=(unique_ptr<T> &&other) noexcept {
		if (this != &other) {
			reset(other.release());
		}
		return (*this);
	}

	void swap(unique_ptr<T> &other) noexcept {
		std::swap(_p, other._p);
	}
	~unique_ptr() noexcept {
		delete _p;
	}

	T &operator*() const {
		return (*get());
	}
	T *operator->() const noexcept {
		return get();
	}
	T *get() const noexcept {
		return _p;
	}
	explicit operator bool() const noexcept {
		return get() != nullptr;
	}

	T *release() noexcept {
		return getPointerAndReset(_p);
	}

	void reset(T *p = nullptr) noexcept {
		T *old = _p;
		_p = p;
		if (old) {
			delete old;
		}
	}

private:
	T *_p = nullptr;

};

template <typename T, typename... Args>
inline unique_ptr<T> make_unique(Args&&... args) {
	return unique_ptr<T>(new T(forward<Args>(args)...));
}

template <typename T>
inline bool operator==(const unique_ptr<T> &a, std::nullptr_t) noexcept {
	return !a;
}
template <typename T>
inline bool operator==(std::nullptr_t, const unique_ptr<T> &b) noexcept {
	return !b;
}
template <typename T>
inline bool operator!=(const unique_ptr<T> &a, std::nullptr_t b) noexcept {
	return !(a == b);
}
template <typename T>
inline bool operator!=(std::nullptr_t a, const unique_ptr<T> &b) noexcept {
	return !(a == b);
}

} // namespace std_

#include "logs.h"

static volatile int *t_assert_nullptr = nullptr;
inline void t_noop() {}
inline void t_assert_fail(const char *message, const char *file, int32 line) {
	QString info(qsl("%1 %2:%3").arg(message).arg(file).arg(line));
	LOG(("Assertion Failed! %1 %2:%3").arg(info));
	SignalHandlers::setCrashAnnotation("Assertion", info);
	*t_assert_nullptr = 0;
}
#define t_assert_full(condition, message, file, line) ((!(condition)) ? t_assert_fail(message, file, line) : t_noop())
#define t_assert_c(condition, comment) t_assert_full(condition, "\"" #condition "\" (" comment ")", __FILE__, __LINE__)
#define t_assert(condition) t_assert_full(condition, "\"" #condition "\"", __FILE__, __LINE__)

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
typedef int32 TimeId;
TimeId myunixtime();
void unixtimeInit();
void unixtimeSet(TimeId servertime, bool force = false);
TimeId unixtime();
TimeId fromServerTime(const MTPint &serverTime);
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

namespace ThirdParty {

	void start();
	void finish();

}

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

// good random (using openssl implementation)
void memset_rand(void *data, uint32 len);
template <typename T>
T rand_value() {
	T result;
	memset_rand(&result, sizeof(result));
	return result;
}

inline void memset_rand_bad(void *data, uint32 len) {
	for (uchar *i = reinterpret_cast<uchar*>(data), *e = i + len; i != e; ++i) {
		*i = uchar(rand() & 0xFF);
	}
}

template <typename T>
inline void memsetrnd_bad(T &value) {
	memset_rand_bad(&value, sizeof(value));
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
	dbiprsNoButton   = 0, // hidden, but not in the cloud settings yet
	dbiprsUnknown    = 1, // contacts not loaded yet
	dbiprsShowButton = 2, // show report spam button, each show peer request setting from cloud
	dbiprsReportSent = 3, // report sent, but the report spam panel is not hidden yet
	dbiprsHidden     = 4, // hidden in the cloud or not needed (bots, contacts, etc), no more requests
	dbiprsRequesting = 5, // requesting the cloud setting right now
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

#include <cmath>

inline int rowscount(int fullCount, int countPerRow) {
	return (fullCount + countPerRow - 1) / countPerRow;
}
inline int floorclamp(int value, int step, int lowest, int highest) {
	return qMin(qMax(value / step, lowest), highest);
}
inline int floorclamp(float64 value, int step, int lowest, int highest) {
	return qMin(qMax(static_cast<int>(std::floor(value / step)), lowest), highest);
}
inline int ceilclamp(int value, int step, int lowest, int highest) {
	return qMax(qMin((value + step - 1) / step, highest), lowest);
}
inline int ceilclamp(float64 value, int32 step, int32 lowest, int32 highest) {
	return qMax(qMin(static_cast<int>(std::ceil(value / step)), highest), lowest);
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

template <typename T, typename... Args>
inline QSharedPointer<T> MakeShared(Args&&... args) {
	return QSharedPointer<T>(new T(std_::forward<Args>(args)...));
}

// This pointer is used for global non-POD variables that are allocated
// on demand by createIfNull(lambda) and are never automatically freed.
template <typename T>
class NeverFreedPointer {
public:
	NeverFreedPointer() = default;
	NeverFreedPointer(const NeverFreedPointer<T> &other) = delete;
	NeverFreedPointer &operator=(const NeverFreedPointer<T> &other) = delete;

	template <typename U>
	void createIfNull(U creator) {
		if (isNull()) {
			reset(creator());
		}
	}

	template <typename... Args>
	void makeIfNull(Args&&... args) {
		if (isNull()) {
			reset(new T(std::forward<Args>(args)...));
		}
	};

	T *data() const {
		return _p;
	}
	T *release() {
		return getPointerAndReset(_p);
	}
	void reset(T *p = nullptr) {
		delete _p;
		_p = p;
	}
	bool isNull() const {
		return data() == nullptr;
	}

	void clear() {
		reset();
	}
	T *operator->() const {
		return data();
	}
	T &operator*() const {
		t_assert(!isNull());
		return *data();
	}
	explicit operator bool() const {
		return !isNull();
	}

private:
	T *_p;

};

// This pointer is used for static non-POD variables that are allocated
// on first use by constructor and are never automatically freed.
template <typename T>
class StaticNeverFreedPointer {
public:
	explicit StaticNeverFreedPointer(T *p) : _p(p) {
	}
	StaticNeverFreedPointer(const StaticNeverFreedPointer<T> &other) = delete;
	StaticNeverFreedPointer &operator=(const StaticNeverFreedPointer<T> &other) = delete;

	T *data() const {
		return _p;
	}
	T *release() {
		return getPointerAndReset(_p);
	}
	void reset(T *p = nullptr) {
		delete _p;
		_p = p;
	}
	bool isNull() const {
		return data() == nullptr;
	}

	void clear() {
		reset();
	}
	T *operator->() const {
		return data();
	}
	T &operator*() const {
		t_assert(!isNull());
		return *data();
	}
	explicit operator bool() const {
		return !isNull();
	}

private:
	T *_p = nullptr;

};

template <typename I>
inline void destroyImplementation(I *&ptr) {
	if (ptr) {
		ptr->destroy();
		ptr = 0;
	}
	deleteAndMark(ptr);
}

class Composer;
typedef void(*ComponentConstruct)(void *location, Composer *composer);
typedef void(*ComponentDestruct)(void *location);
typedef void(*ComponentMove)(void *location, void *waslocation);

struct ComponentWrapStruct {
	// don't init any fields, because it is only created in
	// global scope, so it will be filled by zeros from the start
	ComponentWrapStruct() {
	}
	ComponentWrapStruct(int size, ComponentConstruct construct, ComponentDestruct destruct, ComponentMove move)
	: Size(size)
	, Construct(construct)
	, Destruct(destruct)
	, Move(move) {
	}
	int Size;
	ComponentConstruct Construct;
	ComponentDestruct Destruct;
	ComponentMove Move;
};

template <int Value, int Denominator>
struct CeilDivideMinimumOne {
	static const int Result = ((Value / Denominator) + ((!Value || (Value % Denominator)) ? 1 : 0));
};

extern ComponentWrapStruct ComponentWraps[64];
extern QAtomicInt ComponentIndexLast;

template <typename Type>
struct BaseComponent {
	BaseComponent() {
	}
	BaseComponent(const BaseComponent &other) = delete;
	BaseComponent &operator=(const BaseComponent &other) = delete;
	BaseComponent(BaseComponent &&other) = delete;
	BaseComponent &operator=(BaseComponent &&other) = default;

	static int Index() {
		static QAtomicInt _index(0);
		if (int index = _index.loadAcquire()) {
			return index - 1;
		}
		while (true) {
			int last = ComponentIndexLast.loadAcquire();
			if (ComponentIndexLast.testAndSetOrdered(last, last + 1)) {
				t_assert(last < 64);
				if (_index.testAndSetOrdered(0, last + 1)) {
					ComponentWraps[last] = ComponentWrapStruct(
						CeilDivideMinimumOne<sizeof(Type), sizeof(uint64)>::Result * sizeof(uint64),
						Type::ComponentConstruct, Type::ComponentDestruct, Type::ComponentMove);
				}
				break;
			}
		}
		return _index.loadAcquire() - 1;
	}
	static uint64 Bit() {
		return (1ULL << Index());
	}

protected:
	static void ComponentConstruct(void *location, Composer *composer) {
		new (location) Type();
	}
	static void ComponentDestruct(void *location) {
		((Type*)location)->~Type();
	}
	static void ComponentMove(void *location, void *waslocation) {
		*(Type*)location = std_::move(*(Type*)waslocation);
	}

};

class ComposerMetadata {
public:

	ComposerMetadata(uint64 mask) : size(0), last(64), _mask(mask) {
		for (int i = 0; i < 64; ++i) {
			uint64 m = (1ULL << i);
			if (_mask & m) {
				int s = ComponentWraps[i].Size;
				if (s) {
					offsets[i] = size;
					size += s;
				} else {
					offsets[i] = -1;
				}
			} else if (_mask < m) {
				last = i;
				for (; i < 64; ++i) {
					offsets[i] = -1;
				}
			} else {
				offsets[i] = -1;
			}
		}
	}

	int size, last;
	int offsets[64];

	bool equals(uint64 mask) const {
		return _mask == mask;
	}
	uint64 maskadd(uint64 mask) const {
		return _mask | mask;
	}
	uint64 maskremove(uint64 mask) const {
		return _mask & (~mask);
	}

private:
	uint64 _mask;

};

const ComposerMetadata *GetComposerMetadata(uint64 mask);

class Composer {
public:

	Composer(uint64 mask = 0) : _data(zerodata()) {
		if (mask) {
			const ComposerMetadata *meta = GetComposerMetadata(mask);
			int size = sizeof(meta) + meta->size;
			void *data = operator new(size);
			if (!data) { // terminate if we can't allocate memory
				throw "Can't allocate memory!";
			}

			_data = data;
			_meta() = meta;
			for (int i = 0; i < meta->last; ++i) {
				int offset = meta->offsets[i];
				if (offset >= 0) {
					try {
						ComponentWraps[i].Construct(_dataptrunsafe(offset), this);
					} catch (...) {
						while (i > 0) {
							--i;
							offset = meta->offsets[--i];
							if (offset >= 0) {
								ComponentWraps[i].Destruct(_dataptrunsafe(offset));
							}
						}
						throw;
					}
				}
			}
		}
	}
	Composer(const Composer &other) = delete;
	Composer &operator=(const Composer &other) = delete;
	~Composer() {
		if (_data != zerodata()) {
			const ComposerMetadata *meta = _meta();
			for (int i = 0; i < meta->last; ++i) {
				int offset = meta->offsets[i];
				if (offset >= 0) {
					ComponentWraps[i].Destruct(_dataptrunsafe(offset));
				}
			}
			operator delete(_data);
		}
	}

	template <typename Type>
	bool Has() const {
		return (_meta()->offsets[Type::Index()] >= 0);
	}

	template <typename Type>
	Type *Get() {
		return static_cast<Type*>(_dataptr(_meta()->offsets[Type::Index()]));
	}
	template <typename Type>
	const Type *Get() const {
		return static_cast<const Type*>(_dataptr(_meta()->offsets[Type::Index()]));
	}

protected:
	void UpdateComponents(uint64 mask = 0) {
		if (!_meta()->equals(mask)) {
			Composer tmp(mask);
			tmp.swap(*this);
			if (_data != zerodata() && tmp._data != zerodata()) {
				const ComposerMetadata *meta = _meta(), *wasmeta = tmp._meta();
				for (int i = 0; i < meta->last; ++i) {
					int offset = meta->offsets[i], wasoffset = wasmeta->offsets[i];
					if (offset >= 0 && wasoffset >= 0) {
						ComponentWraps[i].Move(_dataptrunsafe(offset), tmp._dataptrunsafe(wasoffset));
					}
				}
			}
		}
	}
	void AddComponents(uint64 mask = 0) {
		UpdateComponents(_meta()->maskadd(mask));
	}
	void RemoveComponents(uint64 mask = 0) {
		UpdateComponents(_meta()->maskremove(mask));
	}

private:
	static const ComposerMetadata *ZeroComposerMetadata;
	static void *zerodata() {
		return &ZeroComposerMetadata;
	}

	void *_dataptrunsafe(int skip) const {
		return (char*)_data + sizeof(_meta()) + skip;
	}
	void *_dataptr(int skip) const {
		return (skip >= 0) ? _dataptrunsafe(skip) : 0;
	}
	const ComposerMetadata *&_meta() const {
		return *static_cast<const ComposerMetadata**>(_data);
	}
	void *_data;

	void swap(Composer &other) {
		std::swap(_data, other._data);
	}

};

template <typename R, typename... Args>
class SharedCallback {
public:
	virtual R call(Args... args) const = 0;
	virtual ~SharedCallback() {
	}
	typedef QSharedPointer<SharedCallback<R, Args...>> Ptr;
};

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
	FunctionImplementation<R> *create() const { return getPointerAndReset(_ptr); }
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
	Function1Implementation<R, A1> *create() const { return getPointerAndReset(_ptr); }
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
	Function2Implementation<R, A1, A2> *create() const { return getPointerAndReset(_ptr); }
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
