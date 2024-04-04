/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_delegate.h"

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Iv {

class Data;
class Shown;

class Instance final {
public:
	explicit Instance(not_null<Delegate*> delegate);
	~Instance();

	void show(
		not_null<Window::SessionController*> controller,
		not_null<Data*> data,
		QString hash);
	void show(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Data*> data,
		QString hash);
	void show(
		not_null<Main::Session*> session,
		not_null<Data*> data,
		QString hash);

	void openWithIvPreferred(
		not_null<Window::SessionController*> controller,
		QString uri,
		QVariant context = {});
	void openWithIvPreferred(
		not_null<Main::Session*> session,
		QString uri,
		QVariant context = {});

	[[nodiscard]] bool hasActiveWindow(
		not_null<Main::Session*> session) const;

	bool closeActive();
	bool minimizeActive();

	void closeAll();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void processOpenChannel(const QString &context);
	void processJoinChannel(const QString &context);
	void requestFull(not_null<Main::Session*> session, const QString &id);

	void trackSession(not_null<Main::Session*> session);

	const not_null<Delegate*> _delegate;

	std::unique_ptr<Shown> _shown;
	Main::Session *_shownSession = nullptr;
	base::flat_set<not_null<Main::Session*>> _tracking;
	base::flat_map<
		not_null<Main::Session*>,
		base::flat_set<not_null<ChannelData*>>> _joining;
	base::flat_map<
		not_null<Main::Session*>,
		base::flat_set<QString>> _fullRequested;

	base::flat_map<
		not_null<Main::Session*>,
		base::flat_map<QString, WebPageData*>> _ivCache;
	Main::Session *_ivRequestSession = nullptr;
	QString _ivRequestUri;
	mtpRequestId _ivRequestId = 0;


	rpl::lifetime _lifetime;

};

[[nodiscard]] bool PreferForUri(const QString &uri);

} // namespace Iv
