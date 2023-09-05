/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/bot_attach_web_view.h"

#include "api/api_blocked_peers.h"
#include "api/api_common.h"
#include "core/click_handler_types.h"
#include "data/data_bot_app.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "info/profile/info_profile_values.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "webview/webview_interface.h"
#include "core/application.h"
#include "core/local_url_handlers.h"
#include "ui/basic_click_handlers.h"
#include "history/history.h"
#include "history/history_item.h"
#include "payments/payments_checkout_process.h"
#include "storage/storage_account.h"
#include "boxes/peer_list_controllers.h"
#include "lang/lang_keys.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QSvgRenderer>

namespace InlineBots {
namespace {

constexpr auto kProlongTimeout = 60 * crl::time(1000);

struct ParsedBot {
	UserData *bot = nullptr;
	bool inactive = false;
};

[[nodiscard]] DocumentData *ResolveIcon(
		not_null<Main::Session*> session,
		const MTPDattachMenuBot &data) {
	for (const auto &icon : data.vicons().v) {
		const auto document = icon.match([&](
			const MTPDattachMenuBotIcon &data
		) -> DocumentData* {
			if (data.vname().v == "default_static") {
				return session->data().processDocument(data.vicon()).get();
			}
			return nullptr;
		});
		if (document) {
			return document;
		}
	}
	return nullptr;
}

[[nodiscard]] PeerTypes ResolvePeerTypes(
		const QVector<MTPAttachMenuPeerType> &types) {
	auto result = PeerTypes();
	for (const auto &type : types) {
		result |= type.match([&](const MTPDattachMenuPeerTypeSameBotPM &) {
			return PeerType::SameBot;
		}, [&](const MTPDattachMenuPeerTypeBotPM &) {
			return PeerType::Bot;
		}, [&](const MTPDattachMenuPeerTypePM &) {
			return PeerType::User;
		}, [&](const MTPDattachMenuPeerTypeChat &) {
			return PeerType::Group;
		}, [&](const MTPDattachMenuPeerTypeBroadcast &) {
			return PeerType::Broadcast;
		});
	}
	return result;
}

[[nodiscard]] std::optional<AttachWebViewBot> ParseAttachBot(
		not_null<Main::Session*> session,
		const MTPAttachMenuBot &bot) {
	auto result = bot.match([&](const MTPDattachMenuBot &data) {
		const auto user = session->data().userLoaded(UserId(data.vbot_id()));
		const auto good = user
			&& user->isBot()
			&& user->botInfo->supportsAttachMenu;
		return good
			? AttachWebViewBot{
				.user = user,
				.icon = ResolveIcon(session, data),
				.name = qs(data.vshort_name()),
				.types = ResolvePeerTypes(data.vpeer_types().v),
				.inactive = data.is_inactive(),
				.hasSettings = data.is_has_settings(),
				.requestWriteAccess = data.is_request_write_access(),
			} : std::optional<AttachWebViewBot>();
	});
	if (result && result->icon) {
		result->icon->forceToCache(true);
	}
	return result;
}

[[nodiscard]] PeerTypes PeerTypesFromNames(
		const std::vector<QString> &names) {
	auto result = PeerTypes();
	for (const auto &name : names) {
		//, bots, groups, channels
		result |= (name == u"users"_q)
			? PeerType::User
			: name == u"bots"_q
			? PeerType::Bot
			: name == u"groups"_q
			? PeerType::Group
			: name == u"channels"_q
			? PeerType::Broadcast
			: PeerType(0);
	}
	return result;
}

void ShowChooseBox(
		not_null<Window::SessionController*> controller,
		PeerTypes types,
		Fn<void(not_null<Data::Thread*>)> callback,
		rpl::producer<QString> titleOverride = nullptr) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto done = [=](not_null<Data::Thread*> thread) mutable {
		if (const auto strong = *weak) {
			strong->closeBox();
		}
		callback(thread);
	};
	auto filter = [=](not_null<Data::Thread*> thread) -> bool {
		const auto peer = thread->peer();
		if (!Data::CanSend(thread, ChatRestriction::SendInline, false)) {
			return false;
		} else if (const auto user = peer->asUser()) {
			if (user->isBot()) {
				return (types & PeerType::Bot);
			} else {
				return (types & PeerType::User);
			}
		} else if (peer->isBroadcast()) {
			return (types & PeerType::Broadcast);
		} else {
			return (types & PeerType::Group);
		}
	};
	auto initBox = [=](not_null<PeerListBox*> box) {
		if (titleOverride) {
			box->setTitle(std::move(titleOverride));
		}
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
	};
	*weak = controller->show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&controller->session(),
			std::move(done),
			std::move(filter)),
		std::move(initBox)));
}

[[nodiscard]] base::flat_set<not_null<AttachWebView*>> &ActiveWebViews() {
	static auto result = base::flat_set<not_null<AttachWebView*>>();
	return result;
}

class BotAction final : public Ui::Menu::ItemBase {
public:
	BotAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		const AttachWebViewBot &bot,
		Fn<void()> callback);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	[[nodiscard]] rpl::producer<bool> forceShown() const;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

private:
	void contextMenuEvent(QContextMenuEvent *e) override;

	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

	void prepare();
	void validateIcon();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const AttachWebViewBot _bot;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<bool> _forceShown;

	Ui::Text::String _text;
	QImage _mask;
	QImage _icon;
	int _textWidth = 0;
	const int _height;

};

BotAction::BotAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	const AttachWebViewBot &bot,
	Fn<void()> callback)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _bot(bot)
, _height(_st.itemPadding.top()
		+ _st.itemStyle.font->height
		+ _st.itemPadding.bottom()) {
	setAcceptBoth(false);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_icon = QImage();
		update();
	}, lifetime());

	enableMouseSelecting();
	prepare();
}

void BotAction::validateIcon() {
	if (_mask.isNull()) {
		if (!_bot.media || !_bot.media->loaded()) {
			return;
		}
		auto icon = QSvgRenderer(_bot.media->bytes());
		if (!icon.isValid()) {
			_mask = QImage(
				QSize(1, 1) * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			_mask.fill(Qt::transparent);
		} else {
			const auto size = style::ConvertScale(icon.defaultSize());
			_mask = QImage(
				size * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			_mask.setDevicePixelRatio(style::DevicePixelRatio());
			_mask.fill(Qt::transparent);
			{
				auto p = QPainter(&_mask);
				icon.render(&p, QRect(QPoint(), size));
			}
			_mask = Images::Colored(std::move(_mask), QColor(255, 255, 255));
		}
	}
	if (_icon.isNull()) {
		_icon = style::colorizeImage(_mask, st::menuIconColor);
	}
}

void BotAction::paint(Painter &p) {
	validateIcon();

	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}

	if (!_icon.isNull()) {
		p.drawImage(_st.itemIconPosition, _icon);
	}

	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	_text.drawLeftElided(
		p,
		_st.itemPadding.left(),
		_st.itemPadding.top(),
		_textWidth,
		width());
}

void BotAction::prepare() {
	_text.setMarkedText(_st.itemStyle, { _bot.name });
	const auto textWidth = _text.maxWidth();
	const auto &padding = _st.itemPadding;

	const auto goodWidth = padding.left()
		+ textWidth
		+ padding.right();

	const auto w = std::clamp(goodWidth, _st.widthMin, _st.widthMax);
	_textWidth = w - (goodWidth - textWidth);
	setMinWidth(w);
	update();
}

bool BotAction::isEnabled() const {
	return true;
}

not_null<QAction*> BotAction::action() const {
	return _dummyAction;
}

void BotAction::contextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->addAction(tr::lng_bot_remove_from_menu(tr::now), [=] {
		_bot.user->session().attachWebView().removeFromMenu(_bot.user);
	}, &st::menuIconDelete);

	QObject::connect(_menu, &QObject::destroyed, [=] {
		_forceShown.fire(false);
	});

	_forceShown.fire(true);
	_menu->popup(e->globalPos());
	e->accept();
}

QPoint BotAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage BotAction::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

int BotAction::contentHeight() const {
	return _height;
}

rpl::producer<bool> BotAction::forceShown() const {
	return _forceShown.events();
}

void BotAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

} // namespace

bool PeerMatchesTypes(
		not_null<PeerData*> peer,
		not_null<UserData*> bot,
		PeerTypes types) {
	if (const auto user = peer->asUser()) {
		return (user == bot)
			? (types & PeerType::SameBot)
			: user->isBot()
			? (types & PeerType::Bot)
			: (types & PeerType::User);
	} else if (peer->isBroadcast()) {
		return (types & PeerType::Broadcast);
	}
	return (types & PeerType::Group);
}

PeerTypes ParseChooseTypes(QStringView choose) {
	auto result = PeerTypes();
	for (const auto &entry : choose.split(QChar(' '))) {
		if (entry == u"users"_q) {
			result |= PeerType::User;
		} else if (entry == u"bots"_q) {
			result |= PeerType::Bot;
		} else if (entry == u"groups"_q) {
			result |= PeerType::Group;
		} else if (entry == u"channels"_q) {
			result |= PeerType::Broadcast;
		}
	}
	return result;
}

struct AttachWebView::Context {
	base::weak_ptr<Window::SessionController> controller;
	Dialogs::EntryState dialogsEntryState;
	Api::SendAction action;
	bool fromSwitch = false;
	bool fromBotApp = false;
};

AttachWebView::AttachWebView(not_null<Main::Session*> session)
: _session(session) {
}

AttachWebView::~AttachWebView() {
	ActiveWebViews().remove(this);
}

void AttachWebView::request(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action,
		const QString &botUsername,
		const QString &startCommand) {
	if (botUsername.isEmpty()) {
		return;
	}
	const auto username = _bot ? _bot->username() : _botUsername;
	const auto context = LookupContext(controller, action);
	if (IsSame(_context, context)
		&& username.toLower() == botUsername.toLower()
		&& _startCommand == startCommand) {
		if (_panel) {
			_panel->requestActivate();
		}
		return;
	}
	cancel();

	_context = std::make_unique<Context>(context);
	_botUsername = botUsername;
	_startCommand = startCommand;
	resolve();
}

Webview::ThemeParams AttachWebView::botThemeParams() {
	return Window::Theme::WebViewParams();
}

bool AttachWebView::botHandleLocalUri(QString uri) {
	const auto local = Core::TryConvertUrlToLocal(uri);
	if (uri == local || Core::InternalPassportLink(local)) {
		return local.startsWith(u"tg://"_q);
	} else if (!local.startsWith(u"tg://"_q, Qt::CaseInsensitive)) {
		return false;
	}
	botClose();
	crl::on_main([=, shownUrl = _lastShownUrl] {
		const auto variant = QVariant::fromValue(ClickHandlerContext{
			.attachBotWebviewUrl = shownUrl,
		});
		UrlClickHandler::Open(local, variant);
	});
	return true;
}

void AttachWebView::botHandleInvoice(QString slug) {
	Expects(_panel != nullptr);

	using Result = Payments::CheckoutResult;
	const auto weak = base::make_weak(_panel.get());
	const auto reactivate = [=](Result result) {
		if (const auto strong = weak.get()) {
			strong->invoiceClosed(slug, [&] {
				switch (result) {
				case Result::Paid: return "paid";
				case Result::Failed: return "failed";
				case Result::Pending: return "pending";
				case Result::Cancelled: return "cancelled";
				}
				Unexpected("Payments::CheckoutResult value.");
			}());
		}
	};
	_panel->hideForPayment();
	Payments::CheckoutProcess::Start(&_bot->session(), slug, reactivate);
}

void AttachWebView::botHandleMenuButton(Ui::BotWebView::MenuButton button) {
	Expects(_bot != nullptr);
	Expects(_panel != nullptr);

	using Button = Ui::BotWebView::MenuButton;
	const auto bot = _bot;
	switch (button) {
	case Button::OpenBot:
		botClose();
		if (bot->session().windows().empty()) {
			Core::App().domain().activate(&bot->session().account());
		}
		if (!bot->session().windows().empty()) {
			const auto window = bot->session().windows().front();
			window->showPeerHistory(bot);
			window->window().activate();
		}
		break;
	case Button::RemoveFromMenu:
		const auto attached = ranges::find(
			_attachBots,
			not_null{ _bot },
			&AttachWebViewBot::user);
		const auto name = (attached != end(_attachBots))
			? attached->name
			: _bot->name();
		const auto done = crl::guard(this, [=] {
			removeFromMenu(bot);
			botClose();
			if (const auto active = Core::App().activeWindow()) {
				active->activate();
			}
		});
		_panel->showBox(Ui::MakeConfirmBox({
			tr::lng_bot_remove_from_menu_sure(
				tr::now,
				lt_bot,
				Ui::Text::Bold(name),
				Ui::Text::WithEntities),
			done,
		}));
		break;
	}
}

void AttachWebView::botSendData(QByteArray data) {
	if (!_context
		|| _context->fromSwitch
		|| _context->fromBotApp
		|| _context->action.history->peer != _bot
		|| _lastShownQueryId) {
		return;
	}
	const auto randomId = base::RandomValue<uint64>();
	_session->api().request(MTPmessages_SendWebViewData(
		_bot->inputUser,
		MTP_long(randomId),
		MTP_string(_lastShownButtonText),
		MTP_bytes(data)
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
	}).send();
	crl::on_main(this, [=] { cancel(); });
}

void AttachWebView::botSwitchInlineQuery(
		std::vector<QString> chatTypes,
		QString query) {
	const auto controller = _context
		? _context->controller.get()
		: nullptr;
	const auto types = PeerTypesFromNames(chatTypes);
	if (!_bot
		|| !_bot->isBot()
		|| _bot->botInfo->inlinePlaceholder.isEmpty()
		|| !controller) {
		return;
	} else if (!types) {
		if (_context->dialogsEntryState.key.owningHistory()) {
			controller->switchInlineQuery(
				_context->dialogsEntryState,
				_bot,
				query);
		}
	} else {
		const auto bot = _bot;
		const auto done = [=](not_null<Data::Thread*> thread) {
			controller->switchInlineQuery(thread, bot, query);
		};
		ShowChooseBox(
			controller,
			types,
			done,
			tr::lng_inline_switch_choose());
	}
	crl::on_main(this, [=] { cancel(); });
}

void AttachWebView::botCheckWriteAccess(Fn<void(bool allowed)> callback) {
	_session->api().request(MTPbots_CanSendMessage(
		_bot->inputUser
	)).done([=](const MTPBool &result) {
		callback(mtpIsTrue(result));
	}).fail([=] {
		callback(false);
	}).send();
}

void AttachWebView::botAllowWriteAccess(Fn<void(bool allowed)> callback) {
	_session->api().request(MTPbots_AllowSendMessage(
		_bot->inputUser
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
		callback(true);
	}).fail([=] {
		callback(false);
	}).send();
}

void AttachWebView::botSharePhone(Fn<void(bool shared)> callback) {
	const auto bot = _bot;
	const auto history = _bot->owner().history(_bot);
	if (_bot->isBlocked()) {
		const auto done = [=](bool success) {
			if (success && _bot == bot) {
				Assert(!_bot->isBlocked());
				botSharePhone(callback);
			} else {
				callback(false);
			}
		};
		_bot->session().api().blockedPeers().unblock(
			_bot,
			crl::guard(this, done));
		return;
	}
	auto action = Api::SendAction(history);
	action.clearDraft = false;
	history->session().api().shareContact(
		_bot->session().user(),
		action,
		std::move(callback));
}

void AttachWebView::botInvokeCustomMethod(
		Ui::BotWebView::CustomMethodRequest request) {
	const auto callback = request.callback;
	_bot->session().api().request(MTPbots_InvokeWebViewCustomMethod(
		_bot->inputUser,
		MTP_string(request.method),
		MTP_dataJSON(MTP_bytes(request.params))
	)).done([=](const MTPDataJSON &result) {
		callback(result.data().vdata().v);
	}).fail([=](const MTP::Error &error) {
		callback(base::make_unexpected(error.type()));
	}).send();
}

void AttachWebView::botClose() {
	crl::on_main(this, [=] { cancel(); });
}

AttachWebView::Context AttachWebView::LookupContext(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action) {
	return {
		.controller = controller,
		.dialogsEntryState = controller->currentDialogsEntryState(),
		.action = action,
	};
}

bool AttachWebView::IsSame(
		const std::unique_ptr<Context> &a,
		const Context &b) {
	// Check fields that are sent to API in bot attach webview requests.
	return a
		&& (a->controller == b.controller)
		&& (a->dialogsEntryState == b.dialogsEntryState)
		&& (a->fromSwitch == b.fromSwitch)
		&& (a->action.history == b.action.history)
		&& (a->action.replyTo == b.action.replyTo)
		&& (a->action.options.sendAs == b.action.options.sendAs)
		&& (a->action.options.silent == b.action.options.silent);
}

void AttachWebView::request(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action,
		not_null<UserData*> bot,
		const WebViewButton &button) {
	requestWithOptionalConfirm(
		bot,
		button,
		LookupContext(controller, action),
		button.fromMenu ? nullptr : controller.get());
}

void AttachWebView::requestWithOptionalConfirm(
		not_null<UserData*> bot,
		const WebViewButton &button,
		const Context &context,
		Window::SessionController *controllerForConfirm) {
	if (IsSame(_context, context) && _bot == bot) {
		if (_panel) {
			_panel->requestActivate();
		} else if (_requestId) {
			return;
		}
	}
	cancel();

	_bot = bot;
	_context = std::make_unique<Context>(context);
	if (controllerForConfirm) {
		confirmOpen(controllerForConfirm, [=] {
			request(button);
		});
	} else {
		request(button);
	}
}

void AttachWebView::request(const WebViewButton &button) {
	Expects(_context != nullptr && _bot != nullptr);

	_startCommand = button.startCommand;
	const auto &action = _context->action;

	using Flag = MTPmessages_RequestWebView::Flag;
	const auto flags = Flag::f_theme_params
		| (button.url.isEmpty() ? Flag(0) : Flag::f_url)
		| (_startCommand.isEmpty() ? Flag(0) : Flag::f_start_param)
		| (action.replyTo ? Flag::f_reply_to : Flag(0))
		| (action.options.sendAs ? Flag::f_send_as : Flag(0))
		| (action.options.silent ? Flag::f_silent : Flag(0));
	_requestId = _session->api().request(MTPmessages_RequestWebView(
		MTP_flags(flags),
		action.history->peer->input,
		_bot->inputUser,
		MTP_bytes(button.url),
		MTP_string(_startCommand),
		MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams().json)),
		MTP_string("tdesktop"),
		action.mtpReplyTo(),
		(action.options.sendAs
			? action.options.sendAs->input
			: MTP_inputPeerEmpty())
	)).done([=](const MTPWebViewResult &result) {
		_requestId = 0;
		const auto &data = result.data();
		show(
			data.vquery_id().v,
			qs(data.vurl()),
			button.text,
			button.fromMenu || button.url.isEmpty());
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (error.type() == u"BOT_INVALID"_q) {
			requestBots();
		}
	}).send();
}

void AttachWebView::cancel() {
	ActiveWebViews().remove(this);
	_session->api().request(base::take(_requestId)).cancel();
	_session->api().request(base::take(_prolongId)).cancel();
	base::take(_panel);
	_lastShownContext = base::take(_context);
	_bot = nullptr;
	_app = nullptr;
	_botUsername = QString();
	_botAppName = QString();
	_startCommand = QString();
}

void AttachWebView::requestBots() {
	if (_botsRequestId) {
		return;
	}
	_botsRequestId = _session->api().request(MTPmessages_GetAttachMenuBots(
		MTP_long(_botsHash)
	)).done([=](const MTPAttachMenuBots &result) {
		_botsRequestId = 0;
		result.match([&](const MTPDattachMenuBotsNotModified &) {
		}, [&](const MTPDattachMenuBots &data) {
			_session->data().processUsers(data.vusers());
			_botsHash = data.vhash().v;
			_attachBots.clear();
			_attachBots.reserve(data.vbots().v.size());
			for (const auto &bot : data.vbots().v) {
				if (auto parsed = ParseAttachBot(_session, bot)) {
					if (!parsed->inactive) {
						if (const auto icon = parsed->icon) {
							parsed->media = icon->createMediaView();
							icon->save(Data::FileOrigin(), {});
						}
						_attachBots.push_back(std::move(*parsed));
					}
				}
			}
			_attachBotsUpdates.fire({});
		});
	}).fail([=] {
		_botsRequestId = 0;
	}).send();
}

void AttachWebView::requestAddToMenu(
		not_null<UserData*> bot,
		const QString &startCommand) {
	requestAddToMenu(bot, startCommand, nullptr, std::nullopt, PeerTypes());
}

void AttachWebView::requestAddToMenu(
		not_null<UserData*> bot,
		const QString &startCommand,
		Window::SessionController *controller,
		std::optional<Api::SendAction> action,
		PeerTypes chooseTypes) {
	Expects(controller != nullptr || _context != nullptr);

	if (!bot->isBot() || !bot->botInfo->supportsAttachMenu) {
		showToast(tr::lng_bot_menu_not_supported(tr::now), controller);
		return;
	}
	const auto wasController = (controller != nullptr);
	_addToMenuChooseController = base::make_weak(controller);
	_addToMenuStartCommand = startCommand;
	_addToMenuChooseTypes = chooseTypes;
	if (!controller) {
		_addToMenuContext = base::take(_context);
	} else if (action) {
		_addToMenuContext = std::make_unique<Context>(
			LookupContext(controller, *action));
	}
	if (_addToMenuId) {
		if (_addToMenuBot == bot) {
			return;
		}
		_session->api().request(base::take(_addToMenuId)).cancel();
	}
	_addToMenuBot = bot;
	_addToMenuId = _session->api().request(MTPmessages_GetAttachMenuBot(
		bot->inputUser
	)).done([=](const MTPAttachMenuBotsBot &result) {
		_addToMenuId = 0;
		const auto bot = base::take(_addToMenuBot);
		const auto context = std::shared_ptr(base::take(_addToMenuContext));
		const auto chooseTypes = base::take(_addToMenuChooseTypes);
		const auto startCommand = base::take(_addToMenuStartCommand);
		const auto chooseController = base::take(_addToMenuChooseController);
		const auto open = [=](PeerTypes types) {
			const auto strong = chooseController.get();
			if (!strong) {
				if (wasController) {
					// Just ignore the click if controller was destroyed.
					return true;
				}
			} else if (const auto useTypes = chooseTypes & types) {
				const auto done = [=](not_null<Data::Thread*> thread) {
					strong->showThread(thread);
					requestWithOptionalConfirm(
						bot,
						{ .startCommand = startCommand },
						LookupContext(strong, Api::SendAction(thread)));
				};
				ShowChooseBox(strong, useTypes, done);
				return true;
			}
			if (!context) {
				return false;
			}
			requestWithOptionalConfirm(
				bot,
				{ .startCommand = startCommand },
				*context);
			return true;
		};
		result.match([&](const MTPDattachMenuBotsBot &data) {
			_session->data().processUsers(data.vusers());
			if (const auto parsed = ParseAttachBot(_session, data.vbot())) {
				if (bot == parsed->user) {
					const auto types = parsed->types;
					if (parsed->inactive) {
						confirmAddToMenu(*parsed, [=] {
							open(types);
						});
					} else {
						requestBots();
						if (!open(types)) {
							showToast(
								tr::lng_bot_menu_already_added(tr::now));
						}
					}
				}
			}
		});
	}).fail([=] {
		_addToMenuId = 0;
		_addToMenuBot = nullptr;
		_addToMenuContext = nullptr;
		_addToMenuStartCommand = QString();
		showToast(tr::lng_bot_menu_not_supported(tr::now));
	}).send();
}

void AttachWebView::removeFromMenu(not_null<UserData*> bot) {
	toggleInMenu(bot, ToggledState::Removed, [=] {
		showToast(tr::lng_bot_remove_from_menu_done(tr::now));
	});
}

std::optional<Api::SendAction> AttachWebView::lookupLastAction(
		const QString &url) const {
	if (_lastShownUrl == url && _lastShownContext) {
		return _lastShownContext->action;
	}
	return std::nullopt;
}

void AttachWebView::resolve() {
	Expects(!_panel);

	resolveUsername(_botUsername, [=](not_null<PeerData*> bot) {
		if (!_context) {
			return;
		}
		_bot = bot->asUser();
		if (!_bot) {
			showToast(tr::lng_bot_menu_not_supported(tr::now));
			return;
		}
		requestAddToMenu(_bot, _startCommand);
	});
}

void AttachWebView::resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done) {
	if (const auto peer = _session->data().peerByUsername(username)) {
		done(peer);
		return;
	}
	_session->api().request(base::take(_requestId)).cancel();
	_requestId = _session->api().request(MTPcontacts_ResolveUsername(
		MTP_string(username)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		_requestId = 0;
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			if (const auto peerId = peerFromMTP(data.vpeer())) {
				done(_session->data().peer(peerId));
			}
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (error.code() == 400) {
			showToast(
				tr::lng_username_not_found(tr::now, lt_user, username));
		}
	}).send();
}

void AttachWebView::requestSimple(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> bot,
		const WebViewButton &button) {
	cancel();
	_bot = bot;
	_context = std::make_unique<Context>(LookupContext(
		controller,
		Api::SendAction(bot->owner().history(bot))));
	_context->fromSwitch = button.fromSwitch;
	confirmOpen(controller, [=] {
		requestSimple(button);
	});
}

void AttachWebView::requestSimple(const WebViewButton &button) {
	using Flag = MTPmessages_RequestSimpleWebView::Flag;
	_requestId = _session->api().request(MTPmessages_RequestSimpleWebView(
		MTP_flags(Flag::f_theme_params
			| (button.fromSwitch ? Flag::f_from_switch_webview : Flag())),
		_bot->inputUser,
		MTP_bytes(button.url),
		MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams().json)),
		MTP_string("tdesktop")
	)).done([=](const MTPSimpleWebViewResult &result) {
		_requestId = 0;
		result.match([&](const MTPDsimpleWebViewResultUrl &data) {
			const auto queryId = uint64();
			show(queryId, qs(data.vurl()), button.text);
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
	}).send();
}

void AttachWebView::requestMenu(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> bot) {
	cancel();
	_bot = bot;
	_context = std::make_unique<Context>(LookupContext(
		controller,
		Api::SendAction(bot->owner().history(bot))));
	const auto url = bot->botInfo->botMenuButtonUrl;
	const auto text = bot->botInfo->botMenuButtonText;
	confirmOpen(controller, [=] {
		const auto &action = _context->action;
		using Flag = MTPmessages_RequestWebView::Flag;
		_requestId = _session->api().request(MTPmessages_RequestWebView(
			MTP_flags(Flag::f_theme_params
				| Flag::f_url
				| Flag::f_from_bot_menu
				| (action.replyTo? Flag::f_reply_to : Flag(0))
				| (action.options.sendAs ? Flag::f_send_as : Flag(0))
				| (action.options.silent ? Flag::f_silent : Flag(0))),
			action.history->peer->input,
			_bot->inputUser,
			MTP_string(url),
			MTPstring(), // start_param
			MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams().json)),
			MTP_string("tdesktop"),
			action.mtpReplyTo(),
			(action.options.sendAs
				? action.options.sendAs->input
				: MTP_inputPeerEmpty())
		)).done([=](const MTPWebViewResult &result) {
			_requestId = 0;
			const auto &data = result.data();
			show(data.vquery_id().v, qs(data.vurl()), text);
		}).fail([=](const MTP::Error &error) {
			_requestId = 0;
			if (error.type() == u"BOT_INVALID"_q) {
				requestBots();
			}
		}).send();
	});
}

void AttachWebView::requestApp(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action,
		not_null<UserData*> bot,
		const QString &appName,
		const QString &startParam,
		bool forceConfirmation) {
	const auto context = LookupContext(controller, action);
	if (_requestId
		&& _bot == bot
		&& _startCommand == startParam
		&& _botAppName == appName
		&& IsSame(_context, context)) {
		return;
	}
	cancel();
	_bot = bot;
	_startCommand = startParam;
	_botAppName = appName;
	_context = std::make_unique<Context>(context);
	_context->fromBotApp = true;
	const auto already = _session->data().findBotApp(_bot->id, appName);
	_requestId = _session->api().request(MTPmessages_GetBotApp(
		MTP_inputBotAppShortName(
			bot->inputUser,
			MTP_string(appName)),
		MTP_long(already ? already->hash : 0)
	)).done([=](const MTPmessages_BotApp &result) {
		_requestId = 0;
		if (!_bot || !_context) {
			return;
		}
		const auto &data = result.data();
		const auto firstTime = data.is_inactive();
		const auto received = _session->data().processBotApp(
			_bot->id,
			data.vapp());
		_app = received ? received : already;
		if (!_app) {
			cancel();
			showToast(tr::lng_username_app_not_found(tr::now));
			return;
		}
		const auto confirm = firstTime || forceConfirmation;
		if (confirm) {
			confirmAppOpen(result.data().is_request_write_access());
		} else {
			requestAppView(false);
		}
	}).fail([=] {
		cancel();
		showToast(tr::lng_username_app_not_found(tr::now));
	}).send();
}

void AttachWebView::confirmAppOpen(bool requestWriteAccess) {
	const auto controller = _context ? _context->controller.get() : nullptr;
	if (!controller || !_bot) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto allowed = std::make_shared<Ui::Checkbox*>();
		const auto done = [=](Fn<void()> close) {
			requestAppView((*allowed) && (*allowed)->checked());
			close();
		};
		Ui::ConfirmBox(box, {
			tr::lng_allow_bot_webview(
				tr::now,
				lt_bot_name,
				Ui::Text::Bold(_bot->name()),
				Ui::Text::RichLangValue),
			done,
		});
		if (requestWriteAccess) {
			(*allowed) = box->addRow(
				object_ptr<Ui::Checkbox>(
					box,
					tr::lng_url_auth_allow_messages(
						tr::now,
						lt_bot,
						Ui::Text::Bold(_bot->name()),
						Ui::Text::WithEntities),
					true,
					st::urlAuthCheckbox),
				style::margins(
					st::boxRowPadding.left(),
					st::boxPhotoCaptionSkip,
					st::boxRowPadding.right(),
					st::boxPhotoCaptionSkip));
			(*allowed)->setAllowTextLines();
		}
	}));
}

void AttachWebView::requestAppView(bool allowWrite) {
	if (!_context || !_app) {
		return;
	}
	using Flag = MTPmessages_RequestAppWebView::Flag;
	const auto flags = Flag::f_theme_params
		| (_startCommand.isEmpty() ? Flag(0) : Flag::f_start_param)
		| (allowWrite ? Flag::f_write_allowed : Flag(0));
	_requestId = _session->api().request(MTPmessages_RequestAppWebView(
		MTP_flags(flags),
		_context->action.history->peer->input,
		MTP_inputBotAppID(MTP_long(_app->id), MTP_long(_app->accessHash)),
		MTP_string(_startCommand),
		MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams().json)),
		MTP_string("tdesktop")
	)).done([=](const MTPAppWebViewResult &result) {
		_requestId = 0;
		const auto &data = result.data();
		const auto queryId = uint64();
		show(queryId, qs(data.vurl()));
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (error.type() == u"BOT_INVALID"_q) {
			requestBots();
		}
	}).send();
}

void AttachWebView::confirmOpen(
		not_null<Window::SessionController*> controller,
		Fn<void()> done) {
	if (!_bot) {
		return;
	} else if (_bot->isVerified()
		|| _bot->session().local().isBotTrustedOpenWebView(_bot->id)) {
		done();
		return;
	}
	const auto callback = [=] {
		_bot->session().local().markBotTrustedOpenWebView(_bot->id);
		controller->hideLayer();
		done();
	};
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_allow_bot_webview(
			tr::now,
			lt_bot_name,
			Ui::Text::Bold(_bot->name()),
			Ui::Text::RichLangValue),
		.confirmed = callback,
		.confirmText = tr::lng_box_ok(),
	}));
}

void AttachWebView::ClearAll() {
	while (!ActiveWebViews().empty()) {
		ActiveWebViews().front()->cancel();
	}
}

void AttachWebView::show(
		uint64 queryId,
		const QString &url,
		const QString &buttonText,
		bool allowClipboardRead) {
	Expects(_bot != nullptr && _context != nullptr);

	auto title = Info::Profile::NameValue(_bot);
	ActiveWebViews().emplace(this);

	using Button = Ui::BotWebView::MenuButton;
	const auto attached = ranges::find(
		_attachBots,
		not_null{ _bot },
		&AttachWebViewBot::user);
	const auto hasSettings = (attached != end(_attachBots))
		&& !attached->inactive
		&& attached->hasSettings;
	const auto hasOpenBot = !_context
		|| (_bot != _context->action.history->peer);
	const auto hasRemoveFromMenu = (attached != end(_attachBots))
		&& !attached->inactive;
	const auto buttons = (hasSettings ? Button::Settings : Button::None)
		| (hasOpenBot ? Button::OpenBot : Button::None)
		| (hasRemoveFromMenu ? Button::RemoveFromMenu : Button::None);

	_lastShownUrl = url;
	_lastShownQueryId = queryId;
	_lastShownButtonText = buttonText;
	base::take(_panel);
	_panel = Ui::BotWebView::Show({
		.url = url,
		.userDataPath = _session->domain().local().webviewDataPath(),
		.title = std::move(title),
		.bottom = rpl::single('@' + _bot->username()),
		.delegate = static_cast<Ui::BotWebView::Delegate*>(this),
		.menuButtons = buttons,
		.allowClipboardRead = allowClipboardRead,
	});
	started(queryId);
}

void AttachWebView::started(uint64 queryId) {
	Expects(_bot != nullptr && _context != nullptr);

	if (_context->fromSwitch || !queryId) {
		return;
	}

	_session->data().webViewResultSent(
	) | rpl::filter([=](const Data::Session::WebViewResultSent &sent) {
		return (sent.queryId == queryId);
	}) | rpl::start_with_next([=] {
		cancel();
	}, _panel->lifetime());

	const auto action = _context->action;
	base::timer_each(
		kProlongTimeout
	) | rpl::start_with_next([=] {
		using Flag = MTPmessages_ProlongWebView::Flag;
		_session->api().request(base::take(_prolongId)).cancel();
		_prolongId = _session->api().request(MTPmessages_ProlongWebView(
			MTP_flags(Flag(0)
				| (action.replyTo ? Flag::f_reply_to : Flag(0))
				| (action.options.sendAs ? Flag::f_send_as : Flag(0))
				| (action.options.silent ? Flag::f_silent : Flag(0))),
			action.history->peer->input,
			_bot->inputUser,
			MTP_long(queryId),
			action.mtpReplyTo(),
			(action.options.sendAs
				? action.options.sendAs->input
				: MTP_inputPeerEmpty())
		)).done([=] {
			_prolongId = 0;
		}).send();
	}, _panel->lifetime());
}

void AttachWebView::showToast(
		const QString &text,
		Window::SessionController *controller) {
	const auto strong = controller
		? controller
		: _context
		? _context->controller.get()
		: _addToMenuContext
		? _addToMenuContext->controller.get()
		: nullptr;
	if (strong) {
		strong->showToast(text);
	}
}

void AttachWebView::confirmAddToMenu(
		AttachWebViewBot bot,
		Fn<void()> callback) {
	const auto active = Core::App().activeWindow();
	if (!active) {
		return;
	}
	_confirmAddBox = active->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto allowed = std::make_shared<Ui::Checkbox*>();
		const auto done = [=](Fn<void()> close) {
			const auto state = ((*allowed) && (*allowed)->checked())
				? ToggledState::AllowedToWrite
				: ToggledState::Added;
			toggleInMenu(bot.user, state, [=] {
				if (callback) {
					callback();
				}
				showToast(tr::lng_bot_add_to_menu_done(tr::now));
			});
			close();
		};
		Ui::ConfirmBox(box, {
			tr::lng_bot_add_to_menu(
				tr::now,
				lt_bot,
				Ui::Text::Bold(bot.name),
				Ui::Text::WithEntities),
			done,
		});
		if (bot.requestWriteAccess) {
			(*allowed) = box->addRow(
				object_ptr<Ui::Checkbox>(
					box,
					tr::lng_url_auth_allow_messages(
						tr::now,
						lt_bot,
						Ui::Text::Bold(bot.name),
						Ui::Text::WithEntities),
					true,
					st::urlAuthCheckbox),
				style::margins(
					st::boxRowPadding.left(),
					st::boxPhotoCaptionSkip,
					st::boxRowPadding.right(),
					st::boxPhotoCaptionSkip));
			(*allowed)->setAllowTextLines();
		}
	}));
}

void AttachWebView::toggleInMenu(
		not_null<UserData*> bot,
		ToggledState state,
		Fn<void()> callback) {
	using Flag = MTPmessages_ToggleBotInAttachMenu::Flag;
	_session->api().request(MTPmessages_ToggleBotInAttachMenu(
		MTP_flags((state == ToggledState::AllowedToWrite)
			? Flag::f_write_allowed
			: Flag()),
		bot->inputUser,
		MTP_bool(state != ToggledState::Removed)
	)).done([=] {
		_requestId = 0;
		requestBots();
		if (callback) {
			callback();
		}
	}).fail([=] {
		cancel();
	}).send();
}

std::unique_ptr<Ui::DropdownMenu> MakeAttachBotsMenu(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Fn<Api::SendAction()> actionFactory,
		Fn<void(bool)> attach) {
	if (!Data::CanSend(peer, ChatRestriction::SendInline)) {
		return nullptr;
	}
	auto result = std::make_unique<Ui::DropdownMenu>(
		parent,
		st::dropdownMenuWithIcons);
	const auto bots = &peer->session().attachWebView();
	const auto raw = result.get();
	auto minimal = 0;
	if (Data::CanSend(peer, ChatRestriction::SendPhotos, false)) {
		++minimal;
		raw->addAction(tr::lng_attach_photo_or_video(tr::now), [=] {
			attach(true);
		}, &st::menuIconPhoto);
	}
	const auto fileTypes = ChatRestriction::SendVideos
		| ChatRestriction::SendGifs
		| ChatRestriction::SendStickers
		| ChatRestriction::SendMusic
		| ChatRestriction::SendFiles;
	if (Data::CanSendAnyOf(peer, fileTypes)) {
		++minimal;
		raw->addAction(tr::lng_attach_document(tr::now), [=] {
			attach(false);
		}, &st::menuIconFile);
	}
	for (const auto &bot : bots->attachBots()) {
		if (!PeerMatchesTypes(peer, bot.user, bot.types)) {
			continue;
		}
		const auto callback = [=] {
			bots->request(
				controller,
				actionFactory(),
				bot.user,
				{ .fromMenu = true });
		};
		auto action = base::make_unique_q<BotAction>(
			raw,
			raw->menu()->st(),
			bot,
			callback);
		action->forceShown(
		) | rpl::start_with_next([=](bool shown) {
			if (shown) {
				raw->setAutoHiding(false);
			} else {
				raw->hideAnimated();
				raw->setAutoHiding(true);
			}
		}, action->lifetime());
		raw->addAction(std::move(action));
	}
	if (raw->actions().size() <= minimal) {
		return nullptr;
	}
	return result;
}

} // namespace InlineBots
