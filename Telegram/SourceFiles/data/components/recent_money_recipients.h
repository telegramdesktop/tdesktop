/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class RecentMoneyRecipients final {
public:
	explicit RecentMoneyRecipients(not_null<Main::Session*> session);

	[[nodiscard]] const std::vector<not_null<UserData*>> &list() const;
	[[nodiscard]] rpl::producer<> updates() const;

	void bump(not_null<UserData*> user);
	void remove(not_null<UserData*> user);
	void clear(); // Leaves only self, so fillIfEmpty() won't refill it.
	void fillIfEmpty(Fn<bool(not_null<UserData*>)> eligible);

	[[nodiscard]] QByteArray serialize() const;
	void applyLocal(QByteArray serialized);

private:
	const not_null<Main::Session*> _session;
	std::vector<not_null<UserData*>> _list;
	rpl::event_stream<> _updates;

};

} // namespace Data
