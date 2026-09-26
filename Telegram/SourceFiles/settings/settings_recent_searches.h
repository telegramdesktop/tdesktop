/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Settings {

class RecentSearches final {
public:
	explicit RecentSearches(not_null<Main::Session*> session);
	~RecentSearches();

	[[nodiscard]] const std::vector<QString> &list() const;

	void bump(const QString &entryId);
	void remove(const QString &entryId);

	[[nodiscard]] QByteArray serialize() const;
	void applyLocal(QByteArray serialized);

private:
	const not_null<Main::Session*> _session;

	std::vector<QString> _list;

};

} // namespace Settings
