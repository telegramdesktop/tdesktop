/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
