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

QString translitRusEng(const QString &rus);
QString rusKeyboardLayoutSwitch(const QString &from);

static const int MatrixRowShift = 40000;

inline int rowscount(int fullCount, int countPerRow) {
	return (fullCount + countPerRow - 1) / countPerRow;
}
inline int floorclamp(int value, int step, int lowest, int highest) {
	return std::clamp(value / step, lowest, highest);
}
inline int floorclamp(float64 value, int step, int lowest, int highest) {
	return std::clamp(
		static_cast<int>(std::floor(value / step)),
		lowest,
		highest);
}
inline int ceilclamp(int value, int step, int lowest, int highest) {
	return std::clamp((value + step - 1) / step, lowest, highest);
}
inline int ceilclamp(float64 value, int32 step, int32 lowest, int32 highest) {
	return std::clamp(
		static_cast<int>(std::ceil(value / step)),
		lowest,
		highest);
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
