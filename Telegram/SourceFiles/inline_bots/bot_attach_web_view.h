/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/weak_ptr.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Ui::BotWebView {
class Panel;
} // namespace Ui::BotWebView

namespace Main {
class Session;
} // namespace Main

namespace InlineBots {

class AttachWebView final : public base::has_weak_ptr {
public:
	explicit AttachWebView(not_null<Main::Session*> session);
	~AttachWebView();

	struct WebViewButton {
		QString text;
		QByteArray url;
	};
	void request(not_null<PeerData*> peer, const QString &botUsername);
	void request(
		not_null<PeerData*> peer,
		not_null<UserData*> bot,
		const WebViewButton &button = WebViewButton());
	void requestSimple(
		not_null<UserData*> bot,
		const WebViewButton &button);

private:
	void cancel();
	void resolve();
	void request(const WebViewButton &button = WebViewButton());
	void requestByUsername();
	void resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done);

	void toggleInMenu(bool enabled, Fn<void()> callback);

	void show(
		uint64 queryId,
		const QString &url,
		const QString &buttonText = QString());
	void requestAddToMenu(Fn<void()> callback);

	const not_null<Main::Session*> _session;

	PeerData *_peer = nullptr;
	UserData *_bot = nullptr;
	QString _botUsername;
	QPointer<Ui::GenericBox> _confirmAddBox;

	mtpRequestId _requestId = 0;

	std::unique_ptr<Ui::BotWebView::Panel> _panel;

};

} // namespace InlineBots
