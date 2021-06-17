/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace internal {

class GSDMediaKeys {
public:
	GSDMediaKeys();

	GSDMediaKeys(const GSDMediaKeys &other) = delete;
	GSDMediaKeys &operator=(const GSDMediaKeys &other) = delete;
	GSDMediaKeys(GSDMediaKeys &&other) = delete;
	GSDMediaKeys &operator=(GSDMediaKeys &&other) = delete;

	~GSDMediaKeys();

private:
	class Private;
	const std::unique_ptr<Private> _private;
};

} // namespace internal
} // namespace Platform
