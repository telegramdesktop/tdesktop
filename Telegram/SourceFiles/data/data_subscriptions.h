/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct PeerSubscription final {
	uint64 credits = 0;
	int period = 0;

	explicit operator bool() const {
		return credits > 0 && period > 0;
	}
};

} // namespace Data
