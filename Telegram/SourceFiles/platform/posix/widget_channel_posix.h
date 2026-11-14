/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "twidget/remote_stream.h"

#include <atomic>
#include <memory>
#include <string>

namespace Platform::WidgetHelper {

constexpr auto kChannelDescriptor = 3;

class PosixStream final : public twidget::SandboxRemoteStream {
public:
	explicit PosixStream(int descriptor);
	~PosixStream();

	[[nodiscard]] int descriptor() const;

	size_t read(char *out, size_t size) override;
	bool write(std::string_view bytes) override;
	void shutdown() override;

private:
	const int _descriptor = -1;
	std::atomic<bool> _stopped = false;

};

struct PosixChannel {
	std::unique_ptr<PosixStream> parent;
	std::unique_ptr<PosixStream> child;
	std::string error;
};

[[nodiscard]] PosixChannel CreatePosixChannel();
[[nodiscard]] std::unique_ptr<PosixStream> AdoptPosixChannel(int descriptor);

} // namespace Platform::WidgetHelper
