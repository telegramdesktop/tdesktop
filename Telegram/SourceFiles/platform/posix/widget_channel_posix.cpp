/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/posix/widget_channel_posix.h"

#include <gsl/gsl>

#include <sys/socket.h>
#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <unistd.h>

namespace Platform::WidgetHelper {
namespace {

#ifdef SO_NOSIGPIPE
constexpr auto kSendFlags = 0;
#else // SO_NOSIGPIPE
constexpr auto kSendFlags = MSG_NOSIGNAL;
#endif // SO_NOSIGPIPE

[[nodiscard]] std::string SystemError(const char *call, int error) {
	return std::string(call)
		+ " failed: "
		+ std::generic_category().message(error);
}

[[nodiscard]] std::string Prepare(int descriptor) {
	if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) {
		return SystemError("fcntl", errno);
	}
#ifdef SO_NOSIGPIPE
	auto already = 0;
	auto size = socklen_t(sizeof(already));
	if (getsockopt(
			descriptor,
			SOL_SOCKET,
			SO_NOSIGPIPE,
			&already,
			&size) != 0) {
		return SystemError("getsockopt", errno);
	} else if (already) {
		return {};
	}
	const auto on = 1;
	if (setsockopt(
			descriptor,
			SOL_SOCKET,
			SO_NOSIGPIPE,
			&on,
			sizeof(on)) != 0) {
		return SystemError("setsockopt", errno);
	}
#endif // SO_NOSIGPIPE
	return {};
}

} // namespace

PosixStream::PosixStream(int descriptor) : _descriptor(descriptor) {
}

PosixStream::~PosixStream() {
	::close(_descriptor);
}

int PosixStream::descriptor() const {
	return _descriptor;
}

size_t PosixStream::read(char *out, size_t size) {
	while (!_stopped) {
		const auto received = ::recv(_descriptor, out, size, 0);
		if (received > 0) {
			return size_t(received);
		} else if (received == 0 || errno != EINTR) {
			break;
		}
	}
	return 0;
}

bool PosixStream::write(std::string_view bytes) {
	if (_stopped) {
		return false;
	}
	while (!bytes.empty()) {
		const auto sent = ::send(
			_descriptor,
			bytes.data(),
			bytes.size(),
			kSendFlags);
		if (sent > 0) {
			bytes.remove_prefix(size_t(sent));
		} else if (sent == 0 || errno != EINTR || _stopped) {
			return false;
		}
	}
	return true;
}

void PosixStream::shutdown() {
	_stopped = true;
	::shutdown(_descriptor, SHUT_WR);
	::shutdown(_descriptor, SHUT_RD);
}

PosixChannel CreatePosixChannel() {
#ifdef SOCK_CLOEXEC
	constexpr auto kType = SOCK_STREAM | SOCK_CLOEXEC;
#else // SOCK_CLOEXEC
	constexpr auto kType = SOCK_STREAM;
#endif // SOCK_CLOEXEC
	auto descriptors = std::array{ -1, -1 };
	if (socketpair(AF_UNIX, kType, 0, descriptors.data()) != 0) {
		return { .error = SystemError("socketpair", errno) };
	}
	const auto guard = gsl::finally([&] {
		for (const auto descriptor : descriptors) {
			if (descriptor >= 0) {
				::close(descriptor);
			}
		}
	});
	for (const auto descriptor : descriptors) {
		auto error = Prepare(descriptor);
		if (!error.empty()) {
			return { .error = std::move(error) };
		}
	}
	const auto own = [](int &descriptor) {
		auto result = std::make_unique<PosixStream>(descriptor);
		descriptor = -1;
		return result;
	};
	return {
		.parent = own(descriptors[0]),
		.child = own(descriptors[1]),
	};
}

std::unique_ptr<PosixStream> AdoptPosixChannel(int descriptor) {
	struct stat data = {};
	if (fstat(descriptor, &data) != 0 || !S_ISSOCK(data.st_mode)) {
		return nullptr;
	}
	auto result = std::make_unique<PosixStream>(descriptor);
	if (!Prepare(descriptor).empty()) {
		return nullptr;
	}
	return result;
}

} // namespace Platform::WidgetHelper
