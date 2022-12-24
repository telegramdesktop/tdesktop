/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "mtproto/sender.h"
#include "base/weak_ptr.h"
#include "base/flags.h"

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

enum class PeerType : uint8 {
	SameBot   = 0x01,
	Bot       = 0x02,
	User      = 0x04,
	Group     = 0x08,
	Broadcast = 0x10,
};
using PeerTypes = base::flags<PeerType>;

[[nodiscard]] bool PeerMatchesTypes(
	not_null<PeerData*> peer,
	not_null<UserData*> bot,
	PeerTypes types);
[[nodiscard]] PeerTypes ParseChooseTypes(QStringView choose);

struct AttachWebViewBot {
	not_null<UserData*> user;
	DocumentData *icon = nullptr;
	std::shared_ptr<Data::DocumentMedia> media;
	QString name;
	PeerTypes types = 0;
	bool inactive = false;
	bool hasSettings = false;
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
		const Api::SendAction &action,
		const QString &botUsername,
		const QString &startCommand);
	void request(
		Window::SessionController *controller,
		const Api::SendAction &action,
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
		const std::optional<Api::SendAction> &action,
		not_null<UserData*> bot,
		const QString &startCommand,
		Window::SessionController *controller = nullptr,
		PeerTypes chooseTypes = {});
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

	std::optional<Api::SendAction> _action;
	UserData *_bot = nullptr;
	QString _botUsername;
	QString _startCommand;
	QPointer<Ui::GenericBox> _confirmAddBox;

	mtpRequestId _requestId = 0;
	mtpRequestId _prolongId = 0;

	uint64 _botsHash = 0;
	mtpRequestId _botsRequestId = 0;

	std::optional<Api::SendAction> _addToMenuAction;
	UserData *_addToMenuBot = nullptr;
	mtpRequestId _addToMenuId = 0;
	QString _addToMenuStartCommand;
	base::weak_ptr<Window::SessionController> _addToMenuChooseController;
	PeerTypes _addToMenuChooseTypes;

	std::vector<AttachWebViewBot> _attachBots;
	rpl::event_stream<> _attachBotsUpdates;

	std::unique_ptr<Ui::BotWebView::Panel> _panel;

};

[[nodiscard]] std::unique_ptr<Ui::DropdownMenu> MakeAttachBotsMenu(
	not_null<QWidget*> parent,
	not_null<PeerData*> peer,
	Fn<Api::SendAction()> actionFactory,
	Fn<void(bool)> attach);

} // namespace InlineBots
