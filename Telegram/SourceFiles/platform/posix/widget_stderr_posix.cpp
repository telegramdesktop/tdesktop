/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/posix/widget_stderr_posix.h"

#include "platform/posix/widget_channel_posix.h"

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <future>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kReadBufferSize = size_t(4096);
constexpr auto kMaxLineBytes = size_t(8192);
constexpr auto kMaxRelayedBytes = size_t(32768);
constexpr auto kCutMark = std::string_view(" [cut]");
constexpr auto kDroppedLine = std::string_view(
	"[widget helper output over 32768 bytes dropped]");
constexpr auto kHexDigits = std::string_view("0123456789abcdef");

struct RelayState {
	std::string line;
	size_t lineBytes = 0;
	size_t total = 0;
	bool cut = false;
	bool dropped = false;
	bool writing = true;
};

[[nodiscard]] std::string SystemError(const char *call, int error) {
	return std::string(call)
		+ " failed: "
		+ std::generic_category().message(error);
}

// Only printable ASCII reaches the terminal, which interprets the other bytes.
void AppendEscaped(std::string &line, unsigned char byte) {
	if (byte == '\\') {
		line += "\\\\";
	} else if (byte >= 0x20 && byte <= 0x7e) {
		line += char(byte);
	} else {
		line += "\\x";
		line += kHexDigits[byte >> 4];
		line += kHexDigits[byte & 0x0f];
	}
}

[[nodiscard]] bool WriteAll(int descriptor, std::string_view bytes) {
	while (!bytes.empty()) {
		const auto written = ::write(descriptor, bytes.data(), bytes.size());
		if (written > 0) {
			bytes.remove_prefix(size_t(written));
		} else if (written == 0 || errno != EINTR) {
			return false;
		}
	}
	return true;
}

void Emit(RelayState &state) {
	if (state.cut) {
		state.line += kCutMark;
	}
	state.line += '\n';
	if (state.writing) {
		state.writing = WriteAll(STDERR_FILENO, state.line);
	}
	state.line.clear();
	state.lineBytes = 0;
	state.cut = false;
}

void Feed(RelayState &state, std::string_view bytes) {
	for (const auto byte : bytes) {
		if (state.dropped) {
			return;
		} else if (state.total == kMaxRelayedBytes) {
			if (!state.line.empty()) {
				Emit(state);
			}
			state.line = kDroppedLine;
			Emit(state);
			state.dropped = true;
			return;
		}
		++state.total;
		if (byte == '\n') {
			Emit(state);
		} else if (state.lineBytes < kMaxLineBytes) {
			AppendEscaped(state.line, static_cast<unsigned char>(byte));
			++state.lineBytes;
		} else {
			state.cut = true;
		}
	}
}

void Run(PosixStream &stream) {
	auto state = RelayState();
	auto buffer = std::array<char, kReadBufferSize>();
	while (const auto received = stream.read(buffer.data(), buffer.size())) {
		Feed(state, std::string_view(buffer.data(), received));
	}
	if (!state.line.empty()) {
		Emit(state);
	}
}

} // namespace

HelperStderr PrepareHelperStderr(int minimum) {
	if (!isatty(STDERR_FILENO)) {
		const auto descriptor = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, minimum);
		if (descriptor < 0) {
			return { .error = SystemError("fcntl", errno) };
		}
		return { .descriptor = descriptor };
	}
	auto channel = CreatePosixChannel();
	if (!channel.error.empty()) {
		return { .error = std::move(channel.error) };
	}
	if (::shutdown(channel.parent->descriptor(), SHUT_WR) != 0) {
		return { .error = SystemError("shutdown", errno) };
	}
	auto relay = std::make_unique<StderrRelay>(std::move(channel.parent));
	const auto descriptor = fcntl(
		channel.child->descriptor(),
		F_DUPFD_CLOEXEC,
		minimum);
	if (descriptor < 0) {
		return { .error = SystemError("fcntl", errno) };
	}
	return {
		.descriptor = descriptor,
		.relay = std::move(relay),
	};
}

StderrRelay::StderrRelay(std::unique_ptr<PosixStream> stream)
: _stream(std::move(stream)) {
	auto ended = std::promise<void>();
	_ended = ended.get_future();
	_thread = std::thread([
		stream = _stream.get(),
		ended = std::move(ended)
	]() mutable {
		Run(*stream);
		ended.set_value();
	});
}

StderrRelay::~StderrRelay() {
	::shutdown(_stream->descriptor(), SHUT_RD);
	_thread.join();
}

void StderrRelay::drain(std::chrono::milliseconds timeout) {
	_ended.wait_for(timeout);
}

} // namespace Platform::WidgetHelper
