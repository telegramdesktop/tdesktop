/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Window {

class SessionNavigation;

void ApplyNoPaidToken(
	not_null<SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	const QString &token,
	Fn<void()> open);

} // namespace Window
