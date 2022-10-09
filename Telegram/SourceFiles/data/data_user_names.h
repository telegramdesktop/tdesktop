/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct Username final {
	QString username;
	bool active = false;
	bool editable = false;
};

using Usernames = std::vector<Username>;

} // namespace Data
