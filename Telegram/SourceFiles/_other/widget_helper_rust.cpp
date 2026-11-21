/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "twidget/engine_platform.h"
#include "twidget/remote_stream.h"
#include "twidget_helper.h"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace WidgetHelper {
namespace {

#ifdef _DEBUG
constexpr auto kTapped = true;
#else // _DEBUG
constexpr auto kTapped = false;
#endif // _DEBUG

struct LockdownState {
	const std::function<std::string()> &lockdown;
	std::string reason;
};

[[nodiscard]] twidget::SandboxRemoteStream &StreamOf(void *context) {
	return *static_cast<twidget::SandboxRemoteStream*>(context);
}

size_t Read(void *context, uint8_t *out, size_t size) noexcept {
	return StreamOf(context).read(reinterpret_cast<char*>(out), size);
}

bool Write(void *context, const uint8_t *bytes, size_t size) noexcept {
	return StreamOf(context).write(std::string_view(
		reinterpret_cast<const char*>(bytes),
		size));
}

void Shutdown(void *context) noexcept {
	StreamOf(context).shutdown();
}

double ThreadCpuSeconds(void *) noexcept {
	return twidget::platform::threadCpuSeconds();
}

double SteadySeconds(void *) noexcept {
	return twidget::platform::steadySeconds();
}

int64_t SteadyMicros(void *) noexcept {
	using namespace std::chrono;
	return duration_cast<microseconds>(
		steady_clock::now().time_since_epoch()).count();
}

int32_t TimeZoneOffset(void *, int64_t epochSeconds) noexcept {
	return twidget::platform::timeZoneOffset(epochSeconds);
}

void RandomBytes(void *, uint8_t *out, size_t size) noexcept {
	twidget::platform::randomBytes(out, size);
}

const uint8_t *LockDown(void *context, size_t *size) noexcept {
	auto &state = *static_cast<LockdownState*>(context);
	try {
		state.reason = state.lockdown();
	} catch (...) {
		state.reason = "the engine process could not lock itself down";
	}
	*size = state.reason.size();
	return reinterpret_cast<const uint8_t*>(state.reason.data());
}

} // namespace

std::string Serve(
		twidget::SandboxRemoteStream &stream,
		const std::string &build,
		const std::function<std::string()> &lockdown) {
	const auto channel = tw_helper_stream{
		.context = &stream,
		.read = Read,
		.write = Write,
		.shutdown = Shutdown,
	};
	const auto platform = tw_helper_platform{
		.context = nullptr,
		.thread_cpu_seconds = ThreadCpuSeconds,
		.steady_seconds = SteadySeconds,
		.steady_micros = SteadyMicros,
		.time_zone_offset = TimeZoneOffset,
		.random = RandomBytes,
	};
	auto state = LockdownState{ .lockdown = lockdown };
	const auto locking = tw_helper_lockdown{
		.context = &state,
		.lockdown = LockDown,
	};
	auto size = size_t();
	const auto reason = tw_helper_serve(
		&channel,
		&platform,
		reinterpret_cast<const uint8_t*>(build.data()),
		build.size(),
		lockdown ? &locking : nullptr,
		kTapped,
		&size);
	auto result = reason
		? std::string(reinterpret_cast<const char*>(reason), size)
		: std::string();
	tw_helper_free(reason, size);
	return result;
}

} // namespace WidgetHelper
