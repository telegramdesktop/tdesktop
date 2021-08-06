/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Core {

[[nodiscard]] QString FormatVersionDisplay(int version);
[[nodiscard]] QString FormatVersionPrecise(int version);

class Changelogs final : public base::has_weak_ptr {
public:
	Changelogs(not_null<Main::Session*> session, int oldVersion);

	static std::unique_ptr<Changelogs> Create(
		not_null<Main::Session*> session);

private:
	void requestCloudLogs();
	void addLocalLogs();
	void addLocalLog(const QString &text);
	void addBetaLogs();
	void addBetaLog(int changeVersion, const char *changes);

	const not_null<Main::Session*> _session;
	const int _oldVersion = 0;
	rpl::lifetime _chatsSubscription;
	bool _addedSomeLocal = false;

};

} // namespace Core
