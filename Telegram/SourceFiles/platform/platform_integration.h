/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

class Integration {
public:
	virtual void init() {
	}

	virtual ~Integration();

	[[nodiscard]] static std::unique_ptr<Integration> Create();
	[[nodiscard]] static Integration &Instance();
};

} // namespace Platform
