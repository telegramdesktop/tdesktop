/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Iv {

class Data;
class Shown;

class Instance final {
public:
	Instance();
	~Instance();

	void show(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Data*> data,
		bool local);

	[[nodiscard]] bool hasActiveWindow(
		not_null<Main::Session*> session) const;

	bool closeActive();
	bool minimizeActive();

	void closeAll();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void processOpenChannel(const QString &context);
	void processJoinChannel(const QString &context);

	std::unique_ptr<Shown> _shown;
	Main::Session *_shownSession = nullptr;
	base::flat_set<not_null<Main::Session*>> _tracking;
	base::flat_set<not_null<ChannelData*>> _joining;

	rpl::lifetime _lifetime;

};

} // namespace Iv
