/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace internal {

class MPRISSupport {
public:
	MPRISSupport();
	~MPRISSupport();

private:
	class Private;
	const std::unique_ptr<Private> _private;
};

} // namespace internal
} // namespace Platform
