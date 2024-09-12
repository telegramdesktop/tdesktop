/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "dialogs/dialogs_key.h"
#include "api/api_common.h"
#include "mtproto/sender.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/rp_widget.h"

namespace Data {
class Thread;
} // namespace Data

namespace Ui {
class Show;
class GenericBox;
class DropdownMenu;
} // namespace Ui

namespace Ui::BotWebView {
class Panel;
} // namespace Ui::BotWebView

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Payments {
struct NonPanelPaymentForm;
enum class CheckoutResult;
} // namespace Payments

namespace InlineBots {

class WebViewInstance;

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
	bool inactive : 1 = false;
	bool inMainMenu : 1 = false;
	bool inAttachMenu : 1 = false;
	bool disclaimerRequired : 1 = false;
	bool requestWriteAccess : 1 = false;
};

struct WebViewSourceButton {
	bool simple = false;

	friend inline bool operator==(
		WebViewSourceButton,
		WebViewSourceButton) = default;
};

struct WebViewSourceSwitch {
	friend inline bool operator==(
		const WebViewSourceSwitch &,
		const WebViewSourceSwitch &) = default;
};

struct WebViewSourceLinkApp { // t.me/botusername/appname
	base::weak_ptr<WebViewInstance> from;
	QString appname;
	QString token;

	friend inline bool operator==(
		const WebViewSourceLinkApp &,
		const WebViewSourceLinkApp &) = default;
};

struct WebViewSourceLinkAttachMenu { // ?startattach
	base::weak_ptr<WebViewInstance> from;
	base::weak_ptr<Data::Thread> thread;
	PeerTypes choose;
	QString token;

	friend inline bool operator==(
		const WebViewSourceLinkAttachMenu &,
		const WebViewSourceLinkAttachMenu &) = default;
};

struct WebViewSourceLinkBotProfile { // t.me/botusername?startapp
	base::weak_ptr<WebViewInstance> from;
	QString token;
	bool compact = false;

	friend inline bool operator==(
		const WebViewSourceLinkBotProfile &,
		const WebViewSourceLinkBotProfile &) = default;
};

struct WebViewSourceMainMenu {
	friend inline bool operator==(
		WebViewSourceMainMenu,
		WebViewSourceMainMenu) = default;
};

struct WebViewSourceAttachMenu {
	base::weak_ptr<Data::Thread> thread;

	friend inline bool operator==(
		const WebViewSourceAttachMenu &,
		const WebViewSourceAttachMenu &) = default;
};

struct WebViewSourceBotMenu {
	friend inline bool operator==(
		WebViewSourceBotMenu,
		WebViewSourceBotMenu) = default;
};

struct WebViewSourceGame {
	FullMsgId messageId;
	QString title;

	friend inline bool operator==(
		WebViewSourceGame,
		WebViewSourceGame) = default;
};

struct WebViewSourceBotProfile {
	friend inline bool operator==(
		WebViewSourceBotProfile,
		WebViewSourceBotProfile) = default;
};

struct WebViewSource : std::variant<
	WebViewSourceButton,
	WebViewSourceSwitch,
	WebViewSourceLinkApp,
	WebViewSourceLinkAttachMenu,
	WebViewSourceLinkBotProfile,
	WebViewSourceMainMenu,
	WebViewSourceAttachMenu,
	WebViewSourceBotMenu,
	WebViewSourceGame,
	WebViewSourceBotProfile> {
	using variant::variant;
};

struct WebViewButton {
	QString text;
	QString startCommand;
	QByteArray url;
	bool fromAttachMenu = false;
	bool fromMainMenu = false;
	bool fromSwitch = false;
};

struct WebViewContext {
	base::weak_ptr<Window::SessionController> controller;
	Dialogs::EntryState dialogsEntryState;
	std::optional<Api::SendAction> action;
	bool maySkipConfirmation = false;
};

struct WebViewDescriptor {
	not_null<UserData*> bot;
	std::shared_ptr<Ui::Show> parentShow;
	WebViewContext context;
	WebViewButton button;
	WebViewSource source;
};

class WebViewInstance final
	: public base::has_weak_ptr
	, public Ui::BotWebView::Delegate {
public:
	explicit WebViewInstance(WebViewDescriptor &&descriptor);
	~WebViewInstance();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<UserData*> bot() const;
	[[nodiscard]] WebViewSource source() const;

	void activate();
	void close();

	[[nodiscard]] std::shared_ptr<Main::SessionShow> uiShow();

private:
	void resolve();

	bool openAppFromBotMenuLink();

	void requestButton();
	void requestSimple();
	void requestMain();
	void requestApp(bool allowWrite);
	void requestWithMainMenuDisclaimer();
	void requestWithMenuAdd();
	void maybeChooseAndRequestButton(PeerTypes supported);

	void resolveApp(
		const QString &appname,
		const QString &startparam,
		bool forceConfirmation);
	void confirmOpen(Fn<void()> done);
	void confirmAppOpen(bool writeAccess, Fn<void(bool allowWrite)> done);

	void show(const QString &url, uint64 queryId = 0);
	void showGame();
	void started(uint64 queryId);

	auto nonPanelPaymentFormFactory(
		Fn<void(Payments::CheckoutResult)> reactivate)
	-> Fn<void(Payments::NonPanelPaymentForm)>;

	Webview::ThemeParams botThemeParams() override;
	bool botHandleLocalUri(QString uri, bool keepOpen) override;
	void botHandleInvoice(QString slug) override;
	void botHandleMenuButton(Ui::BotWebView::MenuButton button) override;
	bool botValidateExternalLink(QString uri) override;
	void botOpenIvLink(QString uri) override;
	void botSendData(QByteArray data) override;
	void botSwitchInlineQuery(
		std::vector<QString> chatTypes,
		QString query) override;
	void botCheckWriteAccess(Fn<void(bool allowed)> callback) override;
	void botAllowWriteAccess(Fn<void(bool allowed)> callback) override;
	void botSharePhone(Fn<void(bool shared)> callback) override;
	void botInvokeCustomMethod(
		Ui::BotWebView::CustomMethodRequest request) override;
	void botOpenPrivacyPolicy() override;
	void botClose() override;

	const std::shared_ptr<Ui::Show> _parentShow;
	const not_null<Main::Session*> _session;
	const not_null<UserData*> _bot;
	const WebViewContext _context;
	const WebViewButton _button;
	const WebViewSource _source;

	BotAppData *_app = nullptr;
	QString _appStartParam;
	bool _dataSent = false;

	mtpRequestId _requestId = 0;
	mtpRequestId _prolongId = 0;

	QString _panelUrl;
	std::unique_ptr<Ui::BotWebView::Panel> _panel;

	static base::weak_ptr<WebViewInstance> PendingActivation;

};

class AttachWebView final : public base::has_weak_ptr {
public:
	explicit AttachWebView(not_null<Main::Session*> session);
	~AttachWebView();

	void open(WebViewDescriptor &&descriptor);
	void openByUsername(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action,
		const QString &botUsername,
		const QString &startCommand);

	void cancel();

	void requestBots(Fn<void()> callback = nullptr);
	[[nodiscard]] const std::vector<AttachWebViewBot> &attachBots() const {
		return _attachBots;
	}
	[[nodiscard]] rpl::producer<> attachBotsUpdates() const {
		return _attachBotsUpdates.events();
	}
	void notifyBotIconLoaded() {
		_attachBotsUpdates.fire({});
	}
	[[nodiscard]] bool disclaimerAccepted(
		const AttachWebViewBot &bot) const;
	[[nodiscard]] bool showMainMenuNewBadge(
		const AttachWebViewBot &bot) const;

	void removeFromMenu(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot);

	enum class AddToMenuResult {
		AlreadyInMenu,
		Added,
		Unsupported,
		Cancelled,
	};
	void requestAddToMenu(
		not_null<UserData*> bot,
		Fn<void(AddToMenuResult, PeerTypes supported)> done);
	void acceptMainMenuDisclaimer(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		Fn<void(AddToMenuResult, PeerTypes supported)> done);

	void close(not_null<WebViewInstance*> instance);
	void closeAll();

	void loadPopularAppBots();
	[[nodiscard]] auto popularAppBots() const
		-> const std::vector<not_null<UserData*>> &;
	[[nodiscard]] rpl::producer<> popularAppBotsLoaded() const;

private:
	void resolveUsername(
		std::shared_ptr<Ui::Show> show,
		Fn<void(not_null<PeerData*>)> done);

	enum class ToggledState {
		Removed,
		Added,
		AllowedToWrite,
	};
	void toggleInMenu(
		not_null<UserData*> bot,
		ToggledState state,
		Fn<void(bool success)> callback = nullptr);
	void confirmAddToMenu(
		AttachWebViewBot bot,
		Fn<void(bool added)> callback = nullptr);

	const not_null<Main::Session*> _session;

	base::Timer _refreshTimer;

	QString _botUsername;
	QString _startCommand;

	mtpRequestId _requestId = 0;

	uint64 _botsHash = 0;
	mtpRequestId _botsRequestId = 0;
	std::vector<Fn<void()>> _botsRequestCallbacks;

	struct AddToMenuProcess {
		mtpRequestId requestId = 0;
		std::vector<Fn<void(AddToMenuResult, PeerTypes supported)>> done;
	};
	base::flat_map<not_null<UserData*>, AddToMenuProcess> _addToMenu;

	std::vector<AttachWebViewBot> _attachBots;
	rpl::event_stream<> _attachBotsUpdates;
	base::flat_set<not_null<UserData*>> _disclaimerAccepted;

	std::vector<std::unique_ptr<WebViewInstance>> _instances;

	std::vector<not_null<UserData*>> _popularAppBots;
	mtpRequestId _popularAppBotsRequestId = 0;
	rpl::variable<bool> _popularAppBotsLoaded = false;

};

[[nodiscard]] std::unique_ptr<Ui::DropdownMenu> MakeAttachBotsMenu(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Fn<Api::SendAction()> actionFactory,
	Fn<void(bool)> attach);

class MenuBotIcon final : public Ui::RpWidget {
public:
	MenuBotIcon(
		QWidget *parent,
		std::shared_ptr<Data::DocumentMedia> media);

private:
	void paintEvent(QPaintEvent *e) override;

	void validate();

	std::shared_ptr<Data::DocumentMedia> _media;
	QImage _image;
	QImage _mask;

};

} // namespace InlineBots
