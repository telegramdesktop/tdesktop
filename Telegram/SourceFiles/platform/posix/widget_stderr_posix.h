/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace Platform::WidgetHelper {

class PosixStream;
class StderrRelay;

struct HelperStderr {
	int descriptor = -1;
	std::unique_ptr<StderrRelay> relay;
	std::string error;
};

[[nodiscard]] HelperStderr PrepareHelperStderr(int minimum);

class StderrRelay final {
public:
	explicit StderrRelay(std::unique_ptr<PosixStream> stream);
	StderrRelay(const StderrRelay &other) = delete;
	StderrRelay &operator=(const StderrRelay &other) = delete;
	~StderrRelay();

	void drain(std::chrono::milliseconds timeout);

private:
	const std::unique_ptr<PosixStream> _stream;
	std::future<void> _ended;
	std::thread _thread;

};

} // namespace Platform::WidgetHelper
