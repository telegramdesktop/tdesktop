/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/widget_channel_win.h"

#include "base/random.h"

namespace Platform::WidgetHelper {
namespace {

constexpr auto kBufferSize = DWORD(64 * 1024);

[[nodiscard]] QString SystemError(const char *call, DWORD error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(int(error)));
}

[[nodiscard]] UniqueHandle Own(HANDLE handle) {
	return UniqueHandle((handle != INVALID_HANDLE_VALUE) ? handle : nullptr);
}

[[nodiscard]] std::wstring PipeName() {
	return u"\\\\.\\pipe\\telegram-widget-helper-%1-%2%3"_q
		.arg(GetCurrentProcessId())
		.arg(base::RandomValue<uint64>(), 16, 16, QChar('0'))
		.arg(base::RandomValue<uint64>(), 16, 16, QChar('0'))
		.toStdWString();
}

[[nodiscard]] QString CreateEvents(WinStream::Handles &handles) {
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
	return QString();
}

[[nodiscard]] QString CreateServer(
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
	return QString();
}

[[nodiscard]] QString Connect(const WinStream::Handles &handles) {
	const auto pipe = handles.pipe.get();
	auto overlapped = OVERLAPPED();
	overlapped.hEvent = handles.reading.get();
	if (ConnectNamedPipe(pipe, &overlapped)) {
		return u"ConnectNamedPipe accepted another client"_q;
	}
	const auto error = GetLastError();
	if (error == ERROR_PIPE_CONNECTED) {
		return QString();
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
	if (!issue(&overlapped)) {
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
	return transfer(_handles.reading.get(), [&](OVERLAPPED *overlapped) {
		return ReadFile(_handles.pipe.get(), out, chunk, nullptr, overlapped);
	}).value_or(0);
}

bool WinStream::write(std::string_view bytes) {
	while (!bytes.empty()) {
		const auto chunk = DWORD(std::min(bytes.size(), size_t(MAXDWORD)));
		const auto issue = [&](OVERLAPPED *overlapped) {
			return WriteFile(
				_handles.pipe.get(),
				bytes.data(),
				chunk,
				nullptr,
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

WinChannel CreateWinChannel() {
	const auto name = PipeName();
	auto handles = WinStream::Handles();
	auto error = CreateEvents(handles);
	if (error.isEmpty()) {
		error = CreateServer(handles, name);
	}
	if (!error.isEmpty()) {
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
	if (!error.isEmpty()) {
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
	if (!CreateEvents(handles).isEmpty()) {
		return nullptr;
	}
	return std::make_unique<WinStream>(std::move(handles));
}

} // namespace Platform::WidgetHelper
