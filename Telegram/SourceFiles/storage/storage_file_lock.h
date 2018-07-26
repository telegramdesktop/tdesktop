/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Storage {

class FileLock {
public:
	FileLock();

	bool lock(const QFile &file);
	void unlock();

	static constexpr auto kSkipBytes = size_type(4);

	~FileLock();

private:
	class Lock;
	struct Descriptor;
	struct LockingPid;

	static constexpr auto kLockOffset = index_type(0);
	static constexpr auto kLockLimit = kSkipBytes;

	std::unique_ptr<Lock> _lock;

};

} // namespace Storage
