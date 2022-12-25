/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/bot_attach_web_view.h"

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
#include "ui/toasts/common_toasts.h"
#include "ui/chat/attach/attach_bot_webview.h"
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
#include "styles/style_menu_icons.h"

#include <QSvgRenderer>

namespace InlineBots {
namespace {

constexpr auto kProlongTimeout = 60 * crl::time(1000);

struct ParsedBot {
	UserData *bot = nullptr;
	bool inactive = false;
};

[[nodiscard]] bool IsSame(
		const std::optional<Api::SendAction> &a,
		const Api::SendAction &b) {
	// Check fields that are sent to API in bot attach webview requests.
	return a.has_value()
		&& (a->history == b.history)
		&& (a->replyTo == b.replyTo)
		&& (a->topicRootId == b.topicRootId)
		&& (a->options.sendAs == b.options.sendAs)
		&& (a->options.silent == b.options.silent);
}

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
			} : std::optional<AttachWebViewBot>();
	});
	if (result && result->icon) {
		result->icon->forceToCache(true);
	}
	return result;
}

void ShowChooseBox(
		not_null<Window::SessionController*> controller,
		PeerTypes types,
		Fn<void(not_null<Data::Thread*>)> callback) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto done = [=](not_null<Data::Thread*> thread) mutable {
		if (const auto strong = *weak) {
			strong->closeBox();
		}
		callback(thread);
	};
	auto filter = [=](not_null<Data::Thread*> thread) -> bool {
		const auto peer = thread->peer();
		if (!thread->canWrite()) {
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
	auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
	};
	*weak = controller->show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&controller->session(),
			std::move(done),
			std::move(filter)),
		std::move(initBox)), Ui::LayerOption::KeepOther);
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

AttachWebView::AttachWebView(not_null<Main::Session*> session)
: _session(session) {
}

AttachWebView::~AttachWebView() {
	ActiveWebViews().remove(this);
}

void AttachWebView::request(
		const Api::SendAction &action,
		const QString &botUsername,
		const QString &startCommand) {
	if (botUsername.isEmpty()) {
		return;
	}
	const auto username = _bot ? _bot->username() : _botUsername;
	if (IsSame(_action, action)
		&& username.toLower() == botUsername.toLower()
		&& _startCommand == startCommand) {
		if (_panel) {
			_panel->requestActivate();
		}
		return;
	}
	cancel();

	_action = action;
	_botUsername = botUsername;
	_startCommand = startCommand;
	resolve();
}

void AttachWebView::request(
		Window::SessionController *controller,
		const Api::SendAction &action,
		not_null<UserData*> bot,
		const WebViewButton &button) {
	if (IsSame(_action, action) && _bot == bot) {
		if (_panel) {
			_panel->requestActivate();
		} else if (_requestId) {
			return;
		}
	}
	cancel();

	_bot = bot;
	_action = action;
	if (controller) {
		confirmOpen(controller, [=] {
			request(button);
		});
	} else {
		request(button);
	}
}

void AttachWebView::request(const WebViewButton &button) {
	Expects(_action.has_value() && _bot != nullptr);

	_startCommand = button.startCommand;

	using Flag = MTPmessages_RequestWebView::Flag;
	const auto flags = Flag::f_theme_params
		| (button.url.isEmpty() ? Flag(0) : Flag::f_url)
		| (_startCommand.isEmpty() ? Flag(0) : Flag::f_start_param)
		| (_action->replyTo ? Flag::f_reply_to_msg_id : Flag(0))
		| (_action->topicRootId ? Flag::f_top_msg_id : Flag(0))
		| (_action->options.sendAs ? Flag::f_send_as : Flag(0))
		| (_action->options.silent ? Flag::f_silent : Flag(0));
	_requestId = _session->api().request(MTPmessages_RequestWebView(
		MTP_flags(flags),
		_action->history->peer->input,
		_bot->inputUser,
		MTP_bytes(button.url),
		MTP_string(_startCommand),
		MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams().json)),
		MTP_string("tdesktop"),
		MTP_int(_action->replyTo.bare),
		MTP_int(_action->topicRootId.bare),
		(_action->options.sendAs
			? _action->options.sendAs->input
			: MTP_inputPeerEmpty())
	)).done([=](const MTPWebViewResult &result) {
		_requestId = 0;
		result.match([&](const MTPDwebViewResultUrl &data) {
			show(data.vquery_id().v, qs(data.vurl()), button.text);
		});
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
	_panel = nullptr;
	_action = std::nullopt;
	_bot = nullptr;
	_botUsername = QString();
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
		const std::optional<Api::SendAction> &action,
		not_null<UserData*> bot,
		const QString &startCommand,
		Window::SessionController *controller,
		PeerTypes chooseTypes) {
	if (!bot->isBot() || !bot->botInfo->supportsAttachMenu) {
		Ui::ShowMultilineToast({
			.text = { tr::lng_bot_menu_not_supported(tr::now) },
		});
		return;
	}
	_addToMenuChooseController = base::make_weak(controller);
	_addToMenuStartCommand = startCommand;
	_addToMenuChooseTypes = chooseTypes;
	_addToMenuAction = action;
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
		const auto contextAction = base::take(_addToMenuAction);
		const auto chooseTypes = base::take(_addToMenuChooseTypes);
		const auto startCommand = base::take(_addToMenuStartCommand);
		const auto chooseController = base::take(_addToMenuChooseController);
		const auto open = [=](PeerTypes types) {
			if (const auto useTypes = chooseTypes & types) {
				if (const auto strong = chooseController.get()) {
					const auto done = [=](not_null<Data::Thread*> thread) {
						strong->showThread(thread);
						request(
							nullptr,
							Api::SendAction(thread),
							bot,
							{ .startCommand = startCommand });
					};
					ShowChooseBox(strong, useTypes, done);
				}
				return true;
			} else if (!contextAction) {
				return false;
			}
			request(
				nullptr,
				*contextAction,
				bot,
				{ .startCommand = startCommand });
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
							Ui::ShowMultilineToast({
								.text = {
									tr::lng_bot_menu_already_added(tr::now) },
							});
						}
					}
				}
			}
		});
	}).fail([=] {
		_addToMenuId = 0;
		_addToMenuBot = nullptr;
		_addToMenuAction = std::nullopt;
		_addToMenuStartCommand = QString();
		Ui::ShowMultilineToast({
			.text = { tr::lng_bot_menu_not_supported(tr::now) },
		});
	}).send();
}

void AttachWebView::removeFromMenu(not_null<UserData*> bot) {
	toggleInMenu(bot, false, [=] {
		Ui::ShowMultilineToast({
			.text = { tr::lng_bot_remove_from_menu_done(tr::now) },
		});
	});
}

void AttachWebView::resolve() {
	resolveUsername(_botUsername, [=](not_null<PeerData*> bot) {
		_bot = bot->asUser();
		if (!_bot) {
			Ui::ShowMultilineToast({
				.text = { tr::lng_bot_menu_not_supported(tr::now) }
			});
			return;
		}
		requestAddToMenu(_action, _bot, _startCommand);
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
			Ui::ShowMultilineToast({
				.text = {
					tr::lng_username_not_found(tr::now, lt_user, username),
				},
			});
		}
	}).send();
}

void AttachWebView::requestSimple(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> bot,
		const WebViewButton &button) {
	cancel();
	_bot = bot;
	_action = Api::SendAction(bot->owner().history(bot));
	confirmOpen(controller, [=] {
		requestSimple(button);
	});
}

void AttachWebView::requestSimple(const WebViewButton &button) {
	using Flag = MTPmessages_RequestSimpleWebView::Flag;
	_requestId = _session->api().request(MTPmessages_RequestSimpleWebView(
		MTP_flags(Flag::f_theme_params),
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
	_action = Api::SendAction(bot->owner().history(bot));
	const auto url = bot->botInfo->botMenuButtonUrl;
	const auto text = bot->botInfo->botMenuButtonText;
	confirmOpen(controller, [=] {
		using Flag = MTPmessages_RequestWebView::Flag;
		_requestId = _session->api().request(MTPmessages_RequestWebView(
			MTP_flags(Flag::f_theme_params
				| Flag::f_url
				| Flag::f_from_bot_menu
				| (_action->replyTo? Flag::f_reply_to_msg_id : Flag(0))
				| (_action->topicRootId ? Flag::f_top_msg_id : Flag(0))
				| (_action->options.sendAs ? Flag::f_send_as : Flag(0))
				| (_action->options.silent ? Flag::f_silent : Flag(0))),
			_action->history->peer->input,
			_bot->inputUser,
			MTP_string(url),
			MTPstring(), // url
			MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams().json)),
			MTP_string("tdesktop"),
			MTP_int(_action->replyTo.bare),
			MTP_int(_action->topicRootId.bare),
			(_action->options.sendAs
				? _action->options.sendAs->input
				: MTP_inputPeerEmpty())
		)).done([=](const MTPWebViewResult &result) {
			_requestId = 0;
			result.match([&](const MTPDwebViewResultUrl &data) {
				show(data.vquery_id().v, qs(data.vurl()), text);
			});
		}).fail([=](const MTP::Error &error) {
			_requestId = 0;
			if (error.type() == u"BOT_INVALID"_q) {
				requestBots();
			}
		}).send();
	});
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
		const QString &buttonText) {
	Expects(_bot != nullptr && _action.has_value());

	const auto close = crl::guard(this, [=] {
		crl::on_main(this, [=] { cancel(); });
	});
	const auto sendData = crl::guard(this, [=](QByteArray data) {
		if (!_action || _action->history->peer != _bot || queryId) {
			return;
		}
		const auto randomId = base::RandomValue<uint64>();
		_session->api().request(MTPmessages_SendWebViewData(
			_bot->inputUser,
			MTP_long(randomId),
			MTP_string(buttonText),
			MTP_bytes(data)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
		}).send();
		crl::on_main(this, [=] { cancel(); });
	});
	const auto handleLocalUri = [close](QString uri) {
		const auto local = Core::TryConvertUrlToLocal(uri);
		if (uri == local || Core::InternalPassportLink(local)) {
			return local.startsWith(u"tg://"_q);
		} else if (!local.startsWith(u"tg://"_q, Qt::CaseInsensitive)) {
			return false;
		}
		UrlClickHandler::Open(local, {});
		close();
		return true;
	};
	const auto panel = std::make_shared<
		base::weak_ptr<Ui::BotWebView::Panel>>(nullptr);
	const auto handleInvoice = [=, session = _session](QString slug) {
		using Result = Payments::CheckoutResult;
		const auto reactivate = [=](Result result) {
			if (const auto strong = panel->get()) {
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
		if (const auto strong = panel->get()) {
			strong->hideForPayment();
		}
		Payments::CheckoutProcess::Start(session, slug, reactivate);
	};
	auto title = Info::Profile::NameValue(_bot);
	ActiveWebViews().emplace(this);

	using Button = Ui::BotWebView::MenuButton;
	const auto attached = ranges::find(
		_attachBots,
		not_null{ _bot },
		&AttachWebViewBot::user);
	const auto name = (attached != end(_attachBots))
		? attached->name
		: _bot->name();
	const auto hasSettings = (attached != end(_attachBots))
		&& !attached->inactive
		&& attached->hasSettings;
	const auto hasOpenBot = !_action || (_bot != _action->history->peer);
	const auto hasRemoveFromMenu = (attached != end(_attachBots))
		&& !attached->inactive;
	const auto buttons = (hasSettings ? Button::Settings : Button::None)
		| (hasOpenBot ? Button::OpenBot : Button::None)
		| (hasRemoveFromMenu ? Button::RemoveFromMenu : Button::None);
	const auto bot = _bot;

	const auto handleMenuButton = crl::guard(this, [=](Button button) {
		switch (button) {
		case Button::OpenBot:
			close();
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
			if (const auto strong = panel->get()) {
				const auto done = crl::guard(this, [=] {
					removeFromMenu(bot);
					close();
					if (const auto active = Core::App().activeWindow()) {
						active->activate();
					}
				});
				strong->showBox(Ui::MakeConfirmBox({
					tr::lng_bot_remove_from_menu_sure(
						tr::now,
						lt_bot,
						Ui::Text::Bold(name),
						Ui::Text::WithEntities),
					done,
				}));
			}
			break;
		}
	});

	_panel = Ui::BotWebView::Show({
		.url = url,
		.userDataPath = _session->domain().local().webviewDataPath(),
		.title = std::move(title),
		.bottom = rpl::single('@' + _bot->username()),
		.handleLocalUri = handleLocalUri,
		.handleInvoice = handleInvoice,
		.sendData = sendData,
		.close = close,
		.phone = _session->user()->phone(),
		.menuButtons = buttons,
		.handleMenuButton = handleMenuButton,
		.themeParams = [] { return Window::Theme::WebViewParams(); },
	});
	*panel = _panel.get();
	started(queryId);
}

void AttachWebView::started(uint64 queryId) {
	Expects(_action.has_value() && _bot != nullptr);

	_session->data().webViewResultSent(
	) | rpl::filter([=](const Data::Session::WebViewResultSent &sent) {
		return (sent.queryId == queryId);
	}) | rpl::start_with_next([=] {
		cancel();
	}, _panel->lifetime());

	base::timer_each(
		kProlongTimeout
	) | rpl::start_with_next([=] {
		using Flag = MTPmessages_ProlongWebView::Flag;
		_session->api().request(base::take(_prolongId)).cancel();
		_prolongId = _session->api().request(MTPmessages_ProlongWebView(
			MTP_flags(Flag(0)
				| (_action->replyTo ? Flag::f_reply_to_msg_id : Flag(0))
				| (_action->topicRootId ? Flag::f_top_msg_id : Flag(0))
				| (_action->options.sendAs ? Flag::f_send_as : Flag(0))
				| (_action->options.silent ? Flag::f_silent : Flag(0))),
			_action->history->peer->input,
			_bot->inputUser,
			MTP_long(queryId),
			MTP_int(_action->replyTo.bare),
			MTP_int(_action->topicRootId.bare),
			(_action->options.sendAs
				? _action->options.sendAs->input
				: MTP_inputPeerEmpty())
		)).done([=] {
			_prolongId = 0;
		}).send();
	}, _panel->lifetime());
}

void AttachWebView::confirmAddToMenu(
		AttachWebViewBot bot,
		Fn<void()> callback) {
	const auto done = [=](Fn<void()> close) {
		toggleInMenu(bot.user, true, [=] {
			if (callback) {
				callback();
			}
			Ui::ShowMultilineToast({
				.text = { tr::lng_bot_add_to_menu_done(tr::now) },
			});
		});
		close();
	};
	const auto active = Core::App().activeWindow();
	if (!active) {
		return;
	}
	_confirmAddBox = active->show(Ui::MakeConfirmBox({
		tr::lng_bot_add_to_menu(
			tr::now,
			lt_bot,
			Ui::Text::Bold(bot.name),
			Ui::Text::WithEntities),
		done,
	}));
}

void AttachWebView::toggleInMenu(
		not_null<UserData*> bot,
		bool enabled,
		Fn<void()> callback) {
	_session->api().request(MTPmessages_ToggleBotInAttachMenu(
		bot->inputUser,
		MTP_bool(enabled)
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
		not_null<PeerData*> peer,
		Fn<Api::SendAction()> actionFactory,
		Fn<void(bool)> attach) {
	auto result = std::make_unique<Ui::DropdownMenu>(
		parent,
		st::dropdownMenuWithIcons);
	const auto bots = &peer->session().attachWebView();
	const auto raw = result.get();
	raw->addAction(tr::lng_attach_photo_or_video(tr::now), [=] {
		attach(true);
	}, &st::menuIconPhoto);
	raw->addAction(tr::lng_attach_document(tr::now), [=] {
		attach(false);
	}, &st::menuIconFile);
	for (const auto &bot : bots->attachBots()) {
		if (!PeerMatchesTypes(peer, bot.user, bot.types)) {
			continue;
		}
		auto action = base::make_unique_q<BotAction>(
			raw,
			raw->menu()->st(),
			bot,
			[=] { bots->request(nullptr, actionFactory(), bot.user, {}); });
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
	if (raw->actions().size() < 3) {
		return nullptr;
	}
	return result;
}

} // namespace InlineBots
