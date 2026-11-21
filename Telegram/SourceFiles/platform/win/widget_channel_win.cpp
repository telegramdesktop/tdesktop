/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/widget_channel_win.h"

#include <gsl/gsl>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kBufferSize = DWORD(64 * 1024);

[[nodiscard]] UniqueHandle Own(HANDLE handle) {
	return UniqueHandle((handle != INVALID_HANDLE_VALUE) ? handle : nullptr);
}

[[nodiscard]] std::string CreateEvents(WinStream::Handles &handles) {
	const auto events = std::array{
		&handles.stop,
		&handles.reading,
		&handles.writing,
	};
	for (const auto event : events) {
		event->reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!*event) {
			return SystemError("CreateEventW", GetLastError());
		}
	}
	return std::string();
}

[[nodiscard]] std::string CreateServer(
		WinStream::Handles &handles,
		const std::wstring &name) {
	auto token = HANDLE();
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return SystemError("OpenProcessToken", GetLastError());
	}
	const auto guard = UniqueHandle(token);
	auto user = std::vector<std::byte>(
		sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE);
	auto size = DWORD();
	if (!GetTokenInformation(
			token,
			TokenUser,
			user.data(),
			DWORD(user.size()),
			&size)) {
		return SystemError("GetTokenInformation", GetLastError());
	}
	const auto sid = reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
	auto list = std::vector<std::byte>(
		sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE);
	const auto acl = reinterpret_cast<ACL*>(list.data());
	auto descriptor = SECURITY_DESCRIPTOR();
	if (!InitializeAcl(acl, DWORD(list.size()), ACL_REVISION)) {
		return SystemError("InitializeAcl", GetLastError());
	} else if (!AddAccessAllowedAce(
			acl,
			ACL_REVISION,
			FILE_ALL_ACCESS,
			sid)) {
		return SystemError("AddAccessAllowedAce", GetLastError());
	} else if (!InitializeSecurityDescriptor(
			&descriptor,
			SECURITY_DESCRIPTOR_REVISION)) {
		return SystemError("InitializeSecurityDescriptor", GetLastError());
	} else if (!SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE)) {
		return SystemError("SetSecurityDescriptorDacl", GetLastError());
	} else if (!SetSecurityDescriptorControl(
			&descriptor,
			SE_DACL_PROTECTED,
			SE_DACL_PROTECTED)) {
		return SystemError("SetSecurityDescriptorControl", GetLastError());
	}
	auto attributes = SECURITY_ATTRIBUTES{
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.lpSecurityDescriptor = &descriptor,
		.bInheritHandle = FALSE,
	};
	handles.pipe = Own(CreateNamedPipeW(
		name.c_str(),
		(PIPE_ACCESS_DUPLEX
			| FILE_FLAG_OVERLAPPED
			| FILE_FLAG_FIRST_PIPE_INSTANCE),
		(PIPE_TYPE_BYTE
			| PIPE_READMODE_BYTE
			| PIPE_WAIT
			| PIPE_REJECT_REMOTE_CLIENTS),
		1,
		kBufferSize,
		kBufferSize,
		0,
		&attributes));
	if (!handles.pipe) {
		return SystemError("CreateNamedPipeW", GetLastError());
	}
	return std::string();
}

[[nodiscard]] std::string Connect(const WinStream::Handles &handles) {
	const auto pipe = handles.pipe.get();
	auto overlapped = OVERLAPPED();
	overlapped.hEvent = handles.reading.get();
	if (ConnectNamedPipe(pipe, &overlapped)) {
		return "ConnectNamedPipe accepted another client";
	}
	const auto error = GetLastError();
	if (error == ERROR_PIPE_CONNECTED) {
		return std::string();
	} else if (error == ERROR_IO_PENDING) {
		CancelIoEx(pipe, &overlapped);
		auto done = DWORD();
		GetOverlappedResult(pipe, &overlapped, &done, TRUE);
	}
	return SystemError("ConnectNamedPipe", error);
}

} // namespace

void HandleCloser::operator()(HANDLE handle) const {
	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
	}
}

WinStream::WinStream(Handles handles) : _handles(std::move(handles)) {
}

template <typename Issue>
std::optional<DWORD> WinStream::transfer(HANDLE event, Issue &&issue) {
	const auto pipe = _handles.pipe.get();
	if (_stopped || !ResetEvent(event)) {
		return std::nullopt;
	}
	auto overlapped = OVERLAPPED();
	overlapped.hEvent = event;
	auto issued = DWORD();
	if (!issue(&overlapped, &issued)) {
		if (GetLastError() != ERROR_IO_PENDING) {
			return std::nullopt;
		}
		const auto events = std::array{ _handles.stop.get(), event };
		const auto waited = WaitForMultipleObjects(
			DWORD(events.size()),
			events.data(),
			FALSE,
			INFINITE);
		if (waited != WAIT_OBJECT_0 + 1) {
			CancelIoEx(pipe, &overlapped);
		}
	}
	auto done = DWORD();
	if (!GetOverlappedResult(pipe, &overlapped, &done, TRUE) || _stopped) {
		return std::nullopt;
	}
	return done;
}

size_t WinStream::read(char *out, size_t size) {
	const auto chunk = DWORD(std::min(size, size_t(MAXDWORD)));
	const auto issue = [&](OVERLAPPED *overlapped, DWORD *issued) {
		return ReadFile(_handles.pipe.get(), out, chunk, issued, overlapped);
	};
	return transfer(_handles.reading.get(), issue).value_or(0);
}

bool WinStream::write(std::string_view bytes) {
	while (!bytes.empty()) {
		const auto chunk = DWORD(std::min(bytes.size(), size_t(MAXDWORD)));
		const auto issue = [&](OVERLAPPED *overlapped, DWORD *issued) {
			return WriteFile(
				_handles.pipe.get(),
				bytes.data(),
				chunk,
				issued,
				overlapped);
		};
		const auto done = transfer(_handles.writing.get(), issue);
		if (!done || !*done) {
			return false;
		}
		bytes.remove_prefix(*done);
	}
	return !_stopped;
}

void WinStream::shutdown() {
	_stopped = true;
	SetEvent(_handles.stop.get());
}

WinChannel CreateWinChannel(const std::wstring &name) {
	auto handles = WinStream::Handles();
	auto error = CreateEvents(handles);
	if (error.empty()) {
		error = CreateServer(handles, name);
	}
	if (!error.empty()) {
		return { .error = std::move(error) };
	}
	auto child = Own(CreateFileW(
		name.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		nullptr));
	if (!child) {
		return { .error = SystemError("CreateFileW", GetLastError()) };
	}
	error = Connect(handles);
	if (!error.empty()) {
		return { .error = std::move(error) };
	}
	return {
		.parent = std::make_unique<WinStream>(std::move(handles)),
		.child = std::move(child),
	};
}

std::unique_ptr<WinStream> AdoptWinChannel(UniqueHandle pipe) {
	const auto raw = pipe.get();
	auto flags = DWORD();
	if (GetFileType(raw) != FILE_TYPE_PIPE
		|| !GetNamedPipeInfo(raw, &flags, nullptr, nullptr, nullptr)
		|| (flags & (PIPE_SERVER_END | PIPE_TYPE_MESSAGE)) != 0
		|| !SetHandleInformation(raw, HANDLE_FLAG_INHERIT, 0)) {
		return nullptr;
	}
	auto handles = WinStream::Handles{ .pipe = std::move(pipe) };
	if (!CreateEvents(handles).empty()) {
		return nullptr;
	}
	return std::make_unique<WinStream>(std::move(handles));
}

std::string Utf8(std::wstring_view text) {
	const auto length = int(text.size());
	const auto size = WideCharToMultiByte(
		CP_UTF8,
		0,
		text.data(),
		length,
		nullptr,
		0,
		nullptr,
		nullptr);
	if (size <= 0) {
		return std::string();
	}
	auto result = std::string(std::size_t(size), char());
	const auto written = WideCharToMultiByte(
		CP_UTF8,
		0,
		text.data(),
		length,
		result.data(),
		size,
		nullptr,
		nullptr);
	if (written != size) {
		return std::string();
	}
	return result;
}

std::string SystemError(const char *call, DWORD error) {
	auto buffer = LPWSTR();
	const auto length = FormatMessageW(
		(FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS),
		nullptr,
		error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&buffer),
		0,
		nullptr);
	const auto guard = gsl::finally([&] {
		LocalFree(buffer);
	});
	auto message = length
		? Utf8(std::wstring_view(buffer, length))
		: std::string();
	message.erase(message.find_last_not_of("\r\n ") + 1);
	return std::string(call)
		+ " failed: "
		+ (message.empty() ? ("error " + std::to_string(error)) : message);
}

} // namespace Platform::WidgetHelper
