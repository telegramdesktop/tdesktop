/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/flags.h"
#include "mtproto/sender.h"
#include "ui/chat/attach/attach_bot_webview.h"

namespace Api {
struct SendAction;
} // namespace Api

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
	bool requestWriteAccess = false;
};

class AttachWebView final
	: public base::has_weak_ptr
	, public Ui::BotWebView::Delegate {
public:
	explicit AttachWebView(not_null<Main::Session*> session);
	~AttachWebView();

	struct WebViewButton {
		QString text;
		QString startCommand;
		QByteArray url;
		bool fromMenu = false;
		bool fromSwitch = false;
	};
	void request(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action,
		const QString &botUsername,
		const QString &startCommand);
	void request(
		not_null<Window::SessionController*> controller,
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
	void requestApp(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action,
		not_null<UserData*> bot,
		const QString &appName,
		const QString &startParam,
		bool forceConfirmation);

	void cancel();

	void requestBots();
	[[nodiscard]] const std::vector<AttachWebViewBot> &attachBots() const {
		return _attachBots;
	}
	[[nodiscard]] rpl::producer<> attachBotsUpdates() const {
		return _attachBotsUpdates.events();
	}

	void requestAddToMenu(
		not_null<UserData*> bot,
		const QString &startCommand);
	void requestAddToMenu(
		not_null<UserData*> bot,
		const QString &startCommand,
		Window::SessionController *controller,
		std::optional<Api::SendAction> action,
		PeerTypes chooseTypes);
	void removeFromMenu(not_null<UserData*> bot);

	[[nodiscard]] std::optional<Api::SendAction> lookupLastAction(
		const QString &url) const;

	static void ClearAll();

private:
	struct Context;

	Webview::ThemeParams botThemeParams() override;
	bool botHandleLocalUri(QString uri) override;
	void botHandleInvoice(QString slug) override;
	void botHandleMenuButton(Ui::BotWebView::MenuButton button) override;
	void botSendData(QByteArray data) override;
	void botSwitchInlineQuery(
		std::vector<QString> chatTypes,
		QString query) override;
	void botCheckWriteAccess(Fn<void(bool allowed)> callback) override;
	void botAllowWriteAccess(Fn<void(bool allowed)> callback) override;
	void botSharePhone(Fn<void(bool shared)> callback) override;
	void botInvokeCustomMethod(
		Ui::BotWebView::CustomMethodRequest request) override;
	void botClose() override;

	[[nodiscard]] static Context LookupContext(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action);
	[[nodiscard]] static bool IsSame(
		const std::unique_ptr<Context> &a,
		const Context &b);

	void requestWithOptionalConfirm(
		not_null<UserData*> bot,
		const WebViewButton &button,
		const Context &context,
		Window::SessionController *controllerForConfirm = nullptr);

	void resolve();
	void request(const WebViewButton &button);
	void requestSimple(const WebViewButton &button);
	void resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done);

	void confirmOpen(
		not_null<Window::SessionController*> controller,
		Fn<void()> done);

	enum class ToggledState {
		Removed,
		Added,
		AllowedToWrite,
	};
	void toggleInMenu(
		not_null<UserData*> bot,
		ToggledState state,
		Fn<void()> callback = nullptr);

	void show(
		uint64 queryId,
		const QString &url,
		const QString &buttonText = QString(),
		bool allowClipboardRead = false);
	void confirmAddToMenu(
		AttachWebViewBot bot,
		Fn<void()> callback = nullptr);
	void confirmAppOpen(bool requestWriteAccess);
	void requestAppView(bool allowWrite);
	void started(uint64 queryId);

	void showToast(
		const QString &text,
		Window::SessionController *controller = nullptr);

	const not_null<Main::Session*> _session;

	std::unique_ptr<Context> _context;
	std::unique_ptr<Context> _lastShownContext;
	QString _lastShownUrl;
	uint64 _lastShownQueryId = 0;
	QString _lastShownButtonText;
	UserData *_bot = nullptr;
	QString _botUsername;
	QString _botAppName;
	QString _startCommand;
	BotAppData *_app = nullptr;
	QPointer<Ui::GenericBox> _confirmAddBox;

	mtpRequestId _requestId = 0;
	mtpRequestId _prolongId = 0;

	uint64 _botsHash = 0;
	mtpRequestId _botsRequestId = 0;

	std::unique_ptr<Context> _addToMenuContext;
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
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Fn<Api::SendAction()> actionFactory,
	Fn<void(bool)> attach);

} // namespace InlineBots
