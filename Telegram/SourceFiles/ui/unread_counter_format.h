/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

[[nodiscard]] QString FormatUnreadCounter(
	int unreadCounter,
	bool hasMentionOrReaction,
	bool narrow = false);