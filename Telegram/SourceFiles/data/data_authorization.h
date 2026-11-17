/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct UnreviewedAuth {
	uint64 hash = 0;
	bool unconfirmed = false;
	TimeId date = 0;
	QString device;
	QString location;
	uint64 botId = 0;
	uint64 botAccessHash = 0;
	QString botName;

	friend bool operator==(
		const UnreviewedAuth &,
		const UnreviewedAuth &) = default;
};

} // namespace Data
