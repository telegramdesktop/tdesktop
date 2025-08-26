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
};

} // namespace Data
