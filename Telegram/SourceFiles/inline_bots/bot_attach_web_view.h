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
class DropdownMenu;
} // namespace Ui

namespace Ui::BotWebView {
class Panel;
} // namespace Ui::BotWebView

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class DocumentMedia;
} // namespace Data

namespace InlineBots {

struct AttachWebViewBot {
	not_null<UserData*> user;
	DocumentData *icon = nullptr;
	std::shared_ptr<Data::DocumentMedia> media;
	QString name;
	bool inactive = false;
};

class AttachWebView final : public base::has_weak_ptr {
public:
	explicit AttachWebView(not_null<Main::Session*> session);
	~AttachWebView();

	struct WebViewButton {
		QString text;
		QString startCommand;
		QByteArray url;
	};
	void request(
		not_null<PeerData*> peer,
		const QString &botUsername,
		const QString &startCommand);
	void request(
		Window::SessionController *controller,
		not_null<PeerData*> peer,
		not_null<UserData*> bot,
		const WebViewButton &button);
	void requestSimple(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> bot,
		const WebViewButton &button);
	void requestMenu(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> bot);

	void cancel();

	void requestBots();
	[[nodiscard]] const std::vector<AttachWebViewBot> &attachBots() const {
		return _attachBots;
	}
	[[nodiscard]] rpl::producer<> attachBotsUpdates() const {
		return _attachBotsUpdates.events();
	}

	void requestAddToMenu(
		PeerData *peer,
		not_null<UserData*> bot,
		const QString &startCommand);
	void removeFromMenu(not_null<UserData*> bot);

	static void ClearAll();

private:
	void resolve();
	void request(const WebViewButton &button);
	void requestSimple(const WebViewButton &button);
	void resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done);

	void confirmOpen(
		not_null<Window::SessionController*> controller,
		Fn<void()> done);

	void toggleInMenu(
		not_null<UserData*> bot,
		bool enabled,
		Fn<void()> callback = nullptr);

	void show(
		uint64 queryId,
		const QString &url,
		const QString &buttonText = QString());
	void confirmAddToMenu(
		AttachWebViewBot bot,
		Fn<void()> callback = nullptr);
	void started(uint64 queryId);

	const not_null<Main::Session*> _session;

	PeerData *_peer = nullptr;
	UserData *_bot = nullptr;
	QString _botUsername;
	QString _startCommand;
	QPointer<Ui::GenericBox> _confirmAddBox;
	MsgId _replyToMsgId;

	mtpRequestId _requestId = 0;
	mtpRequestId _prolongId = 0;

	uint64 _botsHash = 0;
	mtpRequestId _botsRequestId = 0;

	PeerData *_addToMenuPeer = nullptr;
	UserData *_addToMenuBot = nullptr;
	mtpRequestId _addToMenuId = 0;
	QString _addToMenuStartCommand;

	std::vector<AttachWebViewBot> _attachBots;
	rpl::event_stream<> _attachBotsUpdates;

	std::unique_ptr<Ui::BotWebView::Panel> _panel;

};

[[nodiscard]] std::unique_ptr<Ui::DropdownMenu> MakeAttachBotsMenu(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	Fn<void(bool)> forceShown,
	Fn<void(bool)> attach);

} // namespace InlineBots
