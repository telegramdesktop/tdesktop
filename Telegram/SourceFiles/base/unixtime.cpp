/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/unixtime.h"

#include "logs.h"

#include <QDateTime>
#include <QReadWriteLock>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined Q_OS_MAC
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

namespace base {
namespace unixtime {
namespace {

std::atomic<bool> ValueUpdated/* = false*/;
std::atomic<TimeId> ValueShift/* = 0*/;
std::atomic<bool> HttpValueValid/* = false*/;
std::atomic<TimeId> HttpValueShift/* = 0*/;

class MsgIdManager {
public:
	MsgIdManager();

	void update();
	[[nodiscard]] uint64 next();

private:
	void initialize();

	QReadWriteLock _lock;
	uint64 _startId = 0;
	std::atomic<uint32> _incrementedPart = 0;
	uint64 _startCounter = 0;
	uint64 _randomPart = 0;
	float64 _multiplier = 0.;

};

MsgIdManager GlobalMsgIdManager;

[[nodiscard]] float64 GetMultiplier() {
	// 0xFFFF0000 instead of 0x100000000 to make msgId grow slightly slower,
	// than unixtime and we had time to reconfigure.

#ifdef Q_OS_WIN
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	return float64(0xFFFF0000L) / float64(li.QuadPart);
#elif defined Q_OS_MAC // Q_OS_WIN
	mach_timebase_info_data_t tb = { 0, 0 };
	mach_timebase_info(&tb);
	const auto frequency = (float64(tb.numer) / tb.denom) / 1000000.;
	return frequency * (float64(0xFFFF0000L) / 1000.);
#else // Q_OS_MAC || Q_OS_WIN
	return float64(0xFFFF0000L) / 1000000000.;
#endif // Q_OS_MAC || Q_OS_WIN
}

[[nodiscard]] uint64 GetCounter() {
#ifdef Q_OS_WIN
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
#elif defined Q_OS_MAC // Q_OS_WIN
	return mach_absolute_time();
#else // Q_OS_MAC || Q_OS_WIN
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return 1000000000 * uint64(ts.tv_sec) + uint64(ts.tv_nsec);
#endif // Q_OS_MAC || Q_OS_WIN
}

MsgIdManager::MsgIdManager() {
	auto generator = std::mt19937(std::random_device()());
	auto distribution = std::uniform_int_distribution<uint32>();
	_randomPart = distribution(generator);
	_multiplier = GetMultiplier();
	initialize();

	srand(uint32(_startCounter & 0xFFFFFFFFUL));
}

void MsgIdManager::update() {
	QWriteLocker lock(&_lock);
	initialize();
}

void MsgIdManager::initialize() {
	_startCounter = GetCounter();
	_startId = ((uint64(uint32(now()))) << 32) | _randomPart;
}

uint64 MsgIdManager::next() {
	const auto counter = GetCounter();

	QReadLocker lock(&_lock);
	const auto delta = (counter - _startCounter);
	const auto result = _startId + (uint64)floor(delta * _multiplier);
	lock.unlock();

	return (result & ~0x03L) + (_incrementedPart += 4);
}

TimeId local() {
	return (TimeId)time(nullptr);
}

} // namespace

TimeId now() {
	return local() + ValueShift.load();
}

void update(TimeId now, bool force) {
	if (force) {
		DEBUG_LOG(("MTP Info: forcing client unixtime to %1"
			).arg(now));
		ValueUpdated = true;
	} else {
		auto expected = false;
		if (!ValueUpdated.compare_exchange_strong(expected, true)) {
			return;
		}
		DEBUG_LOG(("MTP Info: setting client unixtime to %1").arg(now));
	}
	const auto shift = now + 1 - local();
	ValueShift = shift;
	DEBUG_LOG(("MTP Info: now unixtimeDelta is %1").arg(shift));

	HttpValueShift = 0;
	HttpValueValid = false;

	GlobalMsgIdManager.update();
}

QDateTime parse(TimeId value) {
	return (value > 0)
		? QDateTime::fromTime_t(value - ValueShift)
		: QDateTime();
}

TimeId serialize(const QDateTime &date) {
	return date.isNull() ? TimeId(0) : date.toTime_t() + ValueShift;
}

bool http_valid() {
	return HttpValueValid;
}

TimeId http_now() {
	return now() + HttpValueShift;
}

void http_update(TimeId now) {
	HttpValueShift = now - base::unixtime::now();
	HttpValueValid = true;
}

void http_invalidate() {
	HttpValueValid = false;
}

uint64 mtproto_msg_id() {
	return GlobalMsgIdManager.next();
}

} // namespace unixtime
} // namespace base
