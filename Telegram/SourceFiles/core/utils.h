/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "logs.h"
#include "base/basic_types.h"
#include "base/flags.h"
#include "base/algorithm.h"
#include "base/assertion.h"
#include "base/bytes.h"

#include <crl/crl_time.h>
#include <QtCore/QReadWriteLock>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkProxy>
#include <cmath>
#include <set>

#define qsl(s) QStringLiteral(s)

namespace base {

template <typename D, typename T>
inline constexpr D up_cast(T object) {
	using DV = std::decay_t<decltype(*D())>;
	using TV = std::decay_t<decltype(*T())>;
	if constexpr (std::is_base_of_v<DV, TV>) {
		return object;
	} else {
		return nullptr;
	}
}

template <typename T>
using set_of_unique_ptr = std::set<std::unique_ptr<T>, base::pointer_comparator<T>>;

template <typename T>
using set_of_shared_ptr = std::set<std::shared_ptr<T>, base::pointer_comparator<T>>;

template <typename Value, typename From, typename Till>
inline bool in_range(Value &&value, From &&from, Till &&till) {
	return (value >= from) && (value < till);
}

} // namespace base

// using for_const instead of plain range-based for loop to ensure usage of const_iterator
// it is important for the copy-on-write Qt containers
// if you have "QVector<T*> v" then "for (T * const p : v)" will still call QVector::detach(),
// while "for_const (T *p, v)" won't and "for_const (T *&p, v)" won't compile
#define for_const(range_declaration, range_expression) for (range_declaration : std::as_const(range_expression))

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
	constexpr str_const(const char *str, std::size_t size)
	: _str(str)
	, _size(size) {
	}
	template <std::size_t N>
	constexpr str_const(const char(&a)[N]) : str_const(a, N - 1) {
	}
	constexpr char operator[](std::size_t n) const {
		return (n < _size) ? _str[n] :
#ifndef OS_MAC_OLD
			throw std::out_of_range("");
#else // OS_MAC_OLD
			throw std::exception();
#endif // OS_MAC_OLD
	}
	constexpr std::size_t size() const { return _size; }
	constexpr const char *c_str() const { return _str; }

private:
	const char* const _str;
	const std::size_t _size;

};

inline QString str_const_toString(const str_const &str) {
	return QString::fromUtf8(str.c_str(), str.size());
}

inline QByteArray str_const_toByteArray(const str_const &str) {
	return QByteArray::fromRawData(str.c_str(), str.size());
}

int GetNextRequestId();

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

} // namespace ThirdParty

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
inline std::array<char, 20> hashSha1(const void *data, int size) {
	auto result = std::array<char, 20>();
	hashSha1(data, size, result.data());
	return result;
}

int32 *hashSha256(const void *data, uint32 len, void *dest); // dest - ptr to 32 bytes, returns (int32*)dest
inline std::array<char, 32> hashSha256(const void *data, int size) {
	auto result = std::array<char, 32>();
	hashSha256(data, size, result.data());
	return result;
}

int32 *hashMd5(const void *data, uint32 len, void *dest); // dest = ptr to 16 bytes, returns (int32*)dest
inline std::array<char, 16> hashMd5(const void *data, int size) {
	auto result = std::array<char, 16>();
	hashMd5(data, size, result.data());
	return result;
}

char *hashMd5Hex(const int32 *hashmd5, void *dest); // dest = ptr to 32 bytes, returns (char*)dest
inline char *hashMd5Hex(const void *data, uint32 len, void *dest) { // dest = ptr to 32 bytes, returns (char*)dest
	return hashMd5Hex(HashMd5(data, len).result(), dest);
}
inline std::array<char, 32> hashMd5Hex(const void *data, int size) {
	auto result = std::array<char, 32>();
	hashMd5Hex(data, size, result.data());
	return result;
}

// good random (using openssl implementation)
void memset_rand(void *data, uint32 len);
template <typename T>
T rand_value() {
	T result;
	memset_rand(&result, sizeof(result));
	return result;
}

class ReadLockerAttempt {
public:
	ReadLockerAttempt(not_null<QReadWriteLock*> lock) : _lock(lock), _locked(_lock->tryLockForRead()) {
	}
	ReadLockerAttempt(const ReadLockerAttempt &other) = delete;
	ReadLockerAttempt &operator=(const ReadLockerAttempt &other) = delete;
	ReadLockerAttempt(ReadLockerAttempt &&other) : _lock(other._lock), _locked(base::take(other._locked)) {
	}
	ReadLockerAttempt &operator=(ReadLockerAttempt &&other) {
		_lock = other._lock;
		_locked = base::take(other._locked);
		return *this;
	}
	~ReadLockerAttempt() {
		if (_locked) {
			_lock->unlock();
		}
	}

	operator bool() const {
		return _locked;
	}

private:
	not_null<QReadWriteLock*> _lock;
	bool _locked = false;

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

QString translitRusEng(const QString &rus);
QString rusKeyboardLayoutSwitch(const QString &from);

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

struct ProxyData {
	enum class Settings {
		System,
		Enabled,
		Disabled,
	};
	enum class Type {
		None,
		Socks5,
		Http,
		Mtproto,
	};
	enum class Status {
		Valid,
		Unsupported,
		Invalid,
	};

	Type type = Type::None;
	QString host;
	uint32 port = 0;
	QString user, password;

	std::vector<QString> resolvedIPs;
	crl::time resolvedExpireAt = 0;

	[[nodiscard]] bool valid() const;
	[[nodiscard]] Status status() const;
	[[nodiscard]] bool supportsCalls() const;
	[[nodiscard]] bool tryCustomResolve() const;
	[[nodiscard]] bytes::vector secretFromMtprotoPassword() const;
	[[nodiscard]] explicit operator bool() const;
	[[nodiscard]] bool operator==(const ProxyData &other) const;
	[[nodiscard]] bool operator!=(const ProxyData &other) const;

	[[nodiscard]] static bool ValidMtprotoPassword(const QString &password);
	[[nodiscard]] static Status MtprotoPasswordStatus(
		const QString &password);

};

ProxyData ToDirectIpProxy(const ProxyData &proxy, int ipIndex = 0);
QNetworkProxy ToNetworkProxy(const ProxyData &proxy);

static const int MatrixRowShift = 40000;

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

static int32 FullArcLength = 360 * 16;
static int32 QuarterArcLength = (FullArcLength / 4);
static int32 MinArcLength = (FullArcLength / 360);
static int32 AlmostFullArcLength = (FullArcLength - MinArcLength);

// This pointer is used for global non-POD variables that are allocated
// on demand by createIfNull(lambda) and are never automatically freed.
template <typename T>
class NeverFreedPointer {
public:
	NeverFreedPointer() = default;
	NeverFreedPointer(const NeverFreedPointer<T> &other) = delete;
	NeverFreedPointer &operator=(const NeverFreedPointer<T> &other) = delete;

	template <typename... Args>
	void createIfNull(Args&&... args) {
		if (isNull()) {
			reset(new T(std::forward<Args>(args)...));
		}
	};

	T *data() const {
		return _p;
	}
	T *release() {
		return base::take(_p);
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
		Assert(!isNull());
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
		return base::take(_p);
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
		Assert(!isNull());
		return *data();
	}
	explicit operator bool() const {
		return !isNull();
	}

private:
	T *_p = nullptr;

};
