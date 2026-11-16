/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "twidget/remote_stream.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Platform::WidgetHelper {

struct HandleCloser {
	void operator()(HANDLE handle) const;
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

class WinStream final : public twidget::SandboxRemoteStream {
public:
	struct Handles {
		UniqueHandle pipe;
		UniqueHandle stop;
		UniqueHandle reading;
		UniqueHandle writing;
	};

	explicit WinStream(Handles handles);

	size_t read(char *out, size_t size) override;
	bool write(std::string_view bytes) override;
	void shutdown() override;

private:
	template <typename Issue>
	[[nodiscard]] std::optional<DWORD> transfer(HANDLE event, Issue &&issue);

	const Handles _handles;
	std::atomic<bool> _stopped = false;

};

struct WinChannel {
	std::unique_ptr<WinStream> parent;
	UniqueHandle child;
	std::string error;
};

[[nodiscard]] WinChannel CreateWinChannel(const std::wstring &name);
[[nodiscard]] std::unique_ptr<WinStream> AdoptWinChannel(UniqueHandle pipe);

[[nodiscard]] std::string Utf8(std::wstring_view text);
[[nodiscard]] std::string SystemError(const char *call, DWORD error);

} // namespace Platform::WidgetHelper
