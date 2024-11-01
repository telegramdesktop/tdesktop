/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/bot_attach_web_view.h"

#include "api/api_blocked_peers.h"
#include "api/api_common.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/qthelp_url.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/share_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/local_url_handlers.h"
#include "core/shortcuts.h"
#include "data/components/location_pickers.h"
#include "data/data_bot_app.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer_bot_command.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/profile/info_profile_values.h"
#include "iv/iv_instance.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_non_panel_process.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/controls/location_picker.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "webview/webview_interface.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_info.h" // infoVerifiedCheck.
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QSvgRenderer>

namespace InlineBots {
namespace {

constexpr auto kProlongTimeout = 60 * crl::time(1000);
constexpr auto kRefreshBotsTimeout = 60 * 60 * crl::time(1000);
constexpr auto kPopularAppBotsLimit = 100;

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
				.types = (data.vpeer_types()
					? ResolvePeerTypes(data.vpeer_types()->v)
					: PeerTypes()),
				.inactive = data.is_inactive(),
				.inMainMenu = data.is_show_in_side_menu(),
				.inAttachMenu = data.is_show_in_attach_menu(),
				.disclaimerRequired = data.is_side_menu_disclaimer_needed(),
				.requestWriteAccess = data.is_request_write_access(),
			} : std::optional<AttachWebViewBot>();
	});
	if (result && result->icon) {
		result->icon->forceToCache(true);
	}
	if (const auto icon = result->icon) {
		result->media = icon->createMediaView();
		icon->save(Data::FileOrigin(), {});
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

[[nodiscard]] Ui::LocationPickerConfig ResolveMapsConfig(
		not_null<Main::Session*> session) {
	const auto &appConfig = session->appConfig();
	auto map = appConfig.get<base::flat_map<QString, QString>>(
		u"tdesktop_config_map"_q,
		base::flat_map<QString, QString>());
	return {
		.mapsToken = map[u"maps"_q],
		.geoToken = map[u"geo"_q],
	};
}

[[nodiscard]] Window::SessionController *WindowForThread(
		base::weak_ptr<Window::SessionController> weak,
		not_null<Data::Thread*> thread) {
	if (const auto separate = Core::App().separateWindowFor(thread)) {
		return separate->sessionController();
	}
	const auto strong = weak.get();
	if (strong && strong->windowId().hasChatsList()) {
		strong->showThread(thread);
		return strong;
	}
	const auto window = Core::App().ensureSeparateWindowFor(thread);
	return window ? window->sessionController() : nullptr;
}

void ShowChooseBox(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
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
	*weak = show->show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			session,
			std::move(done),
			std::move(filter)),
		std::move(initBox)));
}

void ShowChooseBox(
		not_null<Window::SessionController*> controller,
		PeerTypes types,
		Fn<void(not_null<Data::Thread*>)> callback,
		rpl::producer<QString> titleOverride = nullptr) {
	ShowChooseBox(
		controller->uiShow(),
		&controller->session(),
		types,
		std::move(callback),
		std::move(titleOverride));
}

void FillDisclaimerBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(bool accepted)> done) {
	const auto updateCheck = std::make_shared<Fn<void()>>();
	const auto validateCheck = std::make_shared<Fn<bool()>>();

	const auto callback = [=](Fn<void()> close) {
		if (validateCheck && (*validateCheck)()) {
			done(true);
			close();
		}
	};

	const auto padding = st::boxRowPadding;
	Ui::ConfirmBox(box, {
		.text = tr::lng_mini_apps_disclaimer_text(
			tr::now,
			Ui::Text::RichLangValue),
		.confirmed = callback,
		.cancelled = [=](Fn<void()> close) { done(false); close(); },
		.confirmText = tr::lng_box_ok(),
		.labelPadding = QMargins(padding.left(), 0, padding.right(), 0),
		.title = tr::lng_mini_apps_disclaimer_title(),
	});

	auto checkView = std::make_unique<Ui::CheckView>(
		st::defaultCheck,
		false,
		[=] { if (*updateCheck) { (*updateCheck)(); } });
	const auto check = checkView.get();
	const auto row = box->addRow(
		object_ptr<Ui::Checkbox>(
			box.get(),
			tr::lng_mini_apps_disclaimer_button(
				lt_link,
				rpl::single(Ui::Text::Link(
					tr::lng_mini_apps_disclaimer_link(tr::now),
					tr::lng_mini_apps_tos_url(tr::now))),
				Ui::Text::WithEntities),
			st::urlAuthCheckbox,
			std::move(checkView)),
		{
			st::boxRowPadding.left(),
			st::boxRowPadding.left(),
			st::boxRowPadding.right(),
			0,
		});
	row->setAllowTextLines();
	row->setClickHandlerFilter([=](
			const ClickHandlerPtr &link,
			Qt::MouseButton button) {
		ActivateClickHandler(row, link, ClickContext{
			.button = button,
			.other = QVariant::fromValue(ClickHandlerContext{
				.show = box->uiShow(),
			})
		});
		return false;
	});

	(*updateCheck) = [=] { row->update(); };

	const auto showError = Ui::CheckView::PrepareNonToggledError(
		check,
		box->lifetime());

	(*validateCheck) = [=] {
		if (check->checked()) {
			return true;
		}
		showError();
		return false;
	};
}

WebViewContext ResolveContext(
		not_null<UserData*> bot,
		WebViewContext context) {
	if (!context.dialogsEntryState.key) {
		if (const auto strong = context.controller.get()) {
			context.dialogsEntryState = strong->currentDialogsEntryState();
		}
	}
	if (!context.action) {
		const auto &state = context.dialogsEntryState;
		if (const auto thread = state.key.thread()) {
			context.action = Api::SendAction(thread);
			context.action->replyTo = state.currentReplyTo;
		} else {
			context.action = Api::SendAction(bot->owner().history(bot));
		}
	}
	if (!context.dialogsEntryState.key) {
		using namespace Dialogs;
		using Section = EntryState::Section;
		const auto history = context.action->history;
		const auto topicId = context.action->replyTo.topicRootId;
		const auto topic = history->peer->forumTopicFor(topicId);
		context.dialogsEntryState = EntryState{
			.key = (topic ? Key{ topic } : Key{ history }),
			.section = (topic ? Section::Replies : Section::History),
			.currentReplyTo = context.action->replyTo,
		};
	}
	return context;
}

void FillBotUsepic(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> bot,
		base::weak_ptr<Window::SessionController> weak) {
	auto arrow = Ui::Text::SingleCustomEmoji(
		bot->owner().customEmojiManager().registerInternalEmoji(
			st::topicButtonArrow,
			st::channelEarnLearnArrowMargins,
			false));
	auto aboutLabel = Ui::CreateLabelWithCustomEmoji(
		box->verticalLayout(),
		tr::lng_allow_bot_webview_details(
			lt_emoji,
			rpl::single(std::move(arrow)),
			Ui::Text::RichLangValue
		) | rpl::map([](TextWithEntities text) {
			return Ui::Text::Link(std::move(text), u"internal:"_q);
		}),
		{ .session = &bot->session() },
		st::defaultFlatLabel);
	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		box->verticalLayout(),
		bot,
		st::infoPersonalChannelUserpic);
	Ui::AddSkip(box->verticalLayout());
	aboutLabel->setClickHandlerFilter([=](auto &&...) {
		if (const auto strong = weak.get()) {
			strong->showPeerHistory(
				bot->id,
				Window::SectionShow::Way::Forward);
			return true;
		}
		return false;
	});
	const auto title = Ui::CreateChild<Ui::RpWidget>(box->verticalLayout());
	const auto titleLabel = Ui::CreateChild<Ui::FlatLabel>(
		title,
		rpl::single(bot->name()),
		box->getDelegate()->style().title);
	const auto icon = bot->isVerified() ? &st::infoVerifiedCheck : nullptr;
	title->resize(
		titleLabel->width() + (icon ? icon->width() : 0),
		titleLabel->height());
	title->widthValue(
	) | rpl::distinct_until_changed() | rpl::start_with_next([=](int w) {
		titleLabel->resizeToWidth(w
			- (icon ? icon->width() + st::lineWidth : 0));
	}, title->lifetime());
	if (icon) {
		title->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(title);
			p.fillRect(title->rect(), Qt::transparent);
			icon->paint(
				p,
				std::min(
					titleLabel->textMaxWidth() + st::lineWidth,
					title->width() - st::lineWidth - icon->width()),
				(title->height() - icon->height()) / 2,
				title->width());
		}, title->lifetime());
	}

	Ui::IconWithTitle(box->verticalLayout(), userpic, title, aboutLabel);
}

class BotAction final : public Ui::Menu::ItemBase {
public:
	BotAction(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Ui::Show> show,
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
	void paint(Painter &p);

	const std::shared_ptr<Ui::Show> _show;
	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const AttachWebViewBot _bot;

	MenuBotIcon _icon;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<bool> _forceShown;

	Ui::Text::String _text;
	int _textWidth = 0;
	const int _height;

};

BotAction::BotAction(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Ui::Show> show,
	const style::Menu &st,
	const AttachWebViewBot &bot,
	Fn<void()> callback)
: ItemBase(parent, st)
, _show(std::move(show))
, _dummyAction(new QAction(parent))
, _st(st)
, _bot(bot)
, _icon(this, _bot.media)
, _height(_st.itemPadding.top()
		+ _st.itemStyle.font->height
		+ _st.itemPadding.bottom()) {
	setAcceptBoth(false);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	_icon.move(_st.itemIconPosition);

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare();
}

void BotAction::paint(Painter &p) {
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
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
		const auto bot = _bot.user;
		bot->session().attachWebView().removeFromMenu(_show, bot);
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

base::weak_ptr<WebViewInstance> WebViewInstance::PendingActivation;

MenuBotIcon::MenuBotIcon(
	QWidget *parent,
	std::shared_ptr<Data::DocumentMedia> media)
: RpWidget(parent)
, _media(std::move(media)) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_image = QImage();
		update();
	}, lifetime());

	setAttribute(Qt::WA_TransparentForMouseEvents);
	resize(st::menuIconAdmin.size());
	show();
}

void MenuBotIcon::paintEvent(QPaintEvent *e) {
	validate();
	if (!_image.isNull()) {
		QPainter(this).drawImage(0, 0, _image);
	}
}

void MenuBotIcon::validate() {
	const auto ratio = style::DevicePixelRatio();
	const auto wanted = size() * ratio;
	if (_mask.size() != wanted) {
		if (!_media || !_media->loaded()) {
			return;
		}
		auto icon = QSvgRenderer(_media->bytes());
		_mask = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
		_mask.setDevicePixelRatio(style::DevicePixelRatio());
		_mask.fill(Qt::transparent);
		if (icon.isValid()) {
			auto p = QPainter(&_mask);
			icon.render(&p, rect());
			p.end();

			_mask = Images::Colored(std::move(_mask), Qt::white);
		}
	}
	if (_image.isNull()) {
		_image = style::colorizeImage(_mask, st::menuIconColor);
	}
}

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

WebViewInstance::WebViewInstance(WebViewDescriptor &&descriptor)
: _parentShow(descriptor.parentShow
	? std::move(descriptor.parentShow)
	: descriptor.context.controller
	? descriptor.context.controller.get()->uiShow()
	: nullptr)
, _session(&descriptor.bot->session())
, _bot(descriptor.bot)
, _context(ResolveContext(_bot, std::move(descriptor.context)))
, _button(std::move(descriptor.button))
, _source(std::move(descriptor.source)) {
	resolve();
}

WebViewInstance::~WebViewInstance() {
	_session->api().request(base::take(_requestId)).cancel();
	_session->api().request(base::take(_prolongId)).cancel();
	base::take(_panel);
}

Main::Session &WebViewInstance::session() const {
	return *_session;
}

not_null<UserData*> WebViewInstance::bot() const {
	return _bot;
}

WebViewSource WebViewInstance::source() const {
	return _source;
}

void WebViewInstance::activate() {
	if (_panel) {
		_panel->requestActivate();
	} else {
		PendingActivation = this;
	}
}

void WebViewInstance::resolve() {
	v::match(_source, [&](WebViewSourceButton data) {
		confirmOpen([=] {
			if (data.simple) {
				requestSimple();
			} else {
				requestButton();
			}
		});
	}, [&](WebViewSourceSwitch) {
		confirmOpen([=] {
			requestSimple();
		});
	}, [&](WebViewSourceLinkApp data) {
		resolveApp(data.appname, data.token, !_context.maySkipConfirmation);
	}, [&](WebViewSourceLinkBotProfile) {
		confirmOpen([=] {
			requestMain();
		});
	}, [&](WebViewSourceLinkAttachMenu data) {
		requestWithMenuAdd();
	}, [&](WebViewSourceMainMenu) {
		requestWithMainMenuDisclaimer();
	}, [&](WebViewSourceAttachMenu) {
		requestWithMenuAdd();
	}, [&](WebViewSourceBotMenu) {
		if (!openAppFromBotMenuLink()) {
			confirmOpen([=] {
				requestButton();
			});
		}
	}, [&](WebViewSourceGame game) {
		showGame();
	}, [&](WebViewSourceBotProfile) {
		if (_context.maySkipConfirmation) {
			requestMain();
		} else {
			confirmOpen([=] {
				requestMain();
			});
		}
	});
}

bool WebViewInstance::openAppFromBotMenuLink() {
	const auto url = QString::fromUtf8(_button.url);
	const auto local = Core::TryConvertUrlToLocal(url);
	const auto prefix = u"tg://resolve?"_q;
	if (!local.startsWith(prefix)) {
		return false;
	}
	const auto params = qthelp::url_parse_params(
		local.mid(prefix.size()),
		qthelp::UrlParamNameTransform::ToLower);
	const auto domainParam = params.value(u"domain"_q);
	const auto appnameParam = params.value(u"appname"_q);
	const auto webChannelPreviewLink = (domainParam == u"s"_q)
		&& !appnameParam.isEmpty();
	const auto appname = webChannelPreviewLink ? QString() : appnameParam;
	if (appname.isEmpty()) {
		return false;
	}
	resolveApp(appname, params.value(u"startapp"_q), true);
	return true;
}

void WebViewInstance::resolveApp(
		const QString &appname,
		const QString &startparam,
		bool forceConfirmation) {
	const auto already = _session->data().findBotApp(_bot->id, appname);
	_requestId = _session->api().request(MTPmessages_GetBotApp(
		MTP_inputBotAppShortName(
			_bot->inputUser,
			MTP_string(appname)),
		MTP_long(already ? already->hash : 0)
	)).done([=](const MTPmessages_BotApp &result) {
		_requestId = 0;
		const auto &data = result.data();
		const auto received = _session->data().processBotApp(
			_bot->id,
			data.vapp());
		_app = received ? received : already;
		_appStartParam = startparam;
		if (!_app) {
			_parentShow->showToast(tr::lng_username_app_not_found(tr::now));
			close();
			return;
		}
		const auto confirm = data.is_inactive() || forceConfirmation;
		const auto writeAccess = result.data().is_request_write_access();

		// Check if this app can be added to main menu.
		// On fail it'll still be opened.
		using Result = AttachWebView::AddToMenuResult;
		const auto done = crl::guard(this, [=](Result value, auto) {
			if (value == Result::Cancelled) {
				close();
			} else if (value != Result::Unsupported) {
				requestApp(true);
			} else if (confirm) {
				confirmAppOpen(writeAccess, [=](bool allowWrite) {
					requestApp(allowWrite);
				});
			} else {
				requestApp(false);
			}
		});
		_session->attachWebView().requestAddToMenu(_bot, done);
	}).fail([=] {
		_parentShow->showToast(tr::lng_username_app_not_found(tr::now));
		close();
	}).send();
}

void WebViewInstance::confirmOpen(Fn<void()> done) {
	if (_bot->isVerified()
		|| _session->local().isBotTrustedOpenWebView(_bot->id)) {
		done();
		return;
	}
	const auto callback = [=](Fn<void()> close) {
		_session->local().markBotTrustedOpenWebView(_bot->id);
		close();
		done();
	};
	const auto cancel = [=](Fn<void()> close) {
		botClose();
		close();
	};

	_parentShow->show(Box([=](not_null<Ui::GenericBox*> box) {
		FillBotUsepic(box, _bot, _context.controller);
		Ui::ConfirmBox(box, {
			.text = tr::lng_profile_open_app_about(
				tr::now,
				lt_terms,
				Ui::Text::Link(
					tr::lng_profile_open_app_terms(tr::now),
					tr::lng_mini_apps_tos_url(tr::now)),
				Ui::Text::RichLangValue),
			.confirmed = crl::guard(this, callback),
			.cancelled = crl::guard(this, cancel),
			.confirmText = tr::lng_view_button_bot_app(),
		});
	}));
}

void WebViewInstance::confirmAppOpen(
		bool writeAccess,
		Fn<void(bool allowWrite)> done) {
	_parentShow->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto allowed = std::make_shared<Ui::Checkbox*>();
		const auto callback = [=](Fn<void()> close) {
			done((*allowed) && (*allowed)->checked());
			close();
		};
		const auto cancelled = [=](Fn<void()> close) {
			botClose();
			close();
		};
		FillBotUsepic(box, _bot, _context.controller);
		Ui::ConfirmBox(box, {
			tr::lng_profile_open_app_about(
				tr::now,
				lt_terms,
				Ui::Text::Link(
					tr::lng_profile_open_app_terms(tr::now),
					tr::lng_mini_apps_tos_url(tr::now)),
				Ui::Text::RichLangValue),
			crl::guard(this, callback),
			crl::guard(this, cancelled),
			tr::lng_view_button_bot_app(),
		});
		if (writeAccess) {
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

void WebViewInstance::requestButton() {
	Expects(_context.action.has_value());

	const auto &action = *_context.action;
	using Flag = MTPmessages_RequestWebView::Flag;
	_requestId = _session->api().request(MTPmessages_RequestWebView(
		MTP_flags(Flag::f_theme_params
			| (_button.url.isEmpty() ? Flag(0) : Flag::f_url)
			| (_button.startCommand.isEmpty()
				? Flag(0)
				: Flag::f_start_param)
			| (v::is<WebViewSourceBotMenu>(_source)
				? Flag::f_from_bot_menu
				: Flag(0))
			| (action.replyTo ? Flag::f_reply_to : Flag(0))
			| (action.options.sendAs ? Flag::f_send_as : Flag(0))
			| (action.options.silent ? Flag::f_silent : Flag(0))),
		action.history->peer->input,
		_bot->inputUser,
		MTP_bytes(_button.url),
		MTP_string(_button.startCommand),
		MTP_dataJSON(MTP_bytes(botThemeParams().json)),
		MTP_string("tdesktop"),
		action.mtpReplyTo(),
		(action.options.sendAs
			? action.options.sendAs->input
			: MTP_inputPeerEmpty())
	)).done([=](const MTPWebViewResult &result) {
		const auto &data = result.data();
		show(qs(data.vurl()), data.vquery_id().value_or_empty());
	}).fail([=](const MTP::Error &error) {
		_parentShow->showToast(error.type());
		if (error.type() == u"BOT_INVALID"_q) {
			_session->attachWebView().requestBots();
		}
		close();
	}).send();
}

void WebViewInstance::requestSimple() {
	using Flag = MTPmessages_RequestSimpleWebView::Flag;
	_requestId = _session->api().request(MTPmessages_RequestSimpleWebView(
		MTP_flags(Flag::f_theme_params
			| (v::is<WebViewSourceSwitch>(_source)
				? (Flag::f_url | Flag::f_from_switch_webview)
				: v::is<WebViewSourceMainMenu>(_source)
				? (Flag::f_from_side_menu
					| (_button.startCommand.isEmpty() // from LinkMainMenu
						? Flag()
						: Flag::f_start_param))
				: Flag::f_url)),
		_bot->inputUser,
		MTP_bytes(_button.url),
		MTP_string(_button.startCommand),
		MTP_dataJSON(MTP_bytes(botThemeParams().json)),
		MTP_string("tdesktop")
	)).done([=](const MTPWebViewResult &result) {
		show(qs(result.data().vurl()));
	}).fail([=](const MTP::Error &error) {
		_parentShow->showToast(error.type());
		close();
	}).send();
}

void WebViewInstance::requestMain() {
	using Flag = MTPmessages_RequestMainWebView::Flag;
	_requestId = _session->api().request(MTPmessages_RequestMainWebView(
		MTP_flags(Flag::f_theme_params
			| (_button.startCommand.isEmpty()
						? Flag()
						: Flag::f_start_param)
			| (v::is<WebViewSourceLinkBotProfile>(_source)
				? (v::get<WebViewSourceLinkBotProfile>(_source).compact
					? Flag::f_compact
					: Flag(0))
				: Flag(0))),
		_context.action->history->peer->input,
		_bot->inputUser,
		MTP_string(_button.startCommand),
		MTP_dataJSON(MTP_bytes(botThemeParams().json)),
		MTP_string("tdesktop")
	)).done([=](const MTPWebViewResult &result) {
		show(qs(result.data().vurl()));
	}).fail([=](const MTP::Error &error) {
		_parentShow->showToast(error.type());
		close();
	}).send();
}

void WebViewInstance::requestApp(bool allowWrite) {
	Expects(_app != nullptr);
	Expects(_context.action.has_value());

	using Flag = MTPmessages_RequestAppWebView::Flag;
	const auto app = _app;
	const auto flags = Flag::f_theme_params
		| (_appStartParam.isEmpty() ? Flag(0) : Flag::f_start_param)
		| (allowWrite ? Flag::f_write_allowed : Flag(0));
	_requestId = _session->api().request(MTPmessages_RequestAppWebView(
		MTP_flags(flags),
		_context.action->history->peer->input,
		MTP_inputBotAppID(MTP_long(app->id), MTP_long(app->accessHash)),
		MTP_string(_appStartParam),
		MTP_dataJSON(MTP_bytes(botThemeParams().json)),
		MTP_string("tdesktop")
	)).done([=](const MTPWebViewResult &result) {
		_requestId = 0;
		show(qs(result.data().vurl()));
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (error.type() == u"BOT_INVALID"_q) {
			_session->attachWebView().requestBots();
		}
		close();
	}).send();
}

void WebViewInstance::requestWithMainMenuDisclaimer() {
	using Result = AttachWebView::AddToMenuResult;
	const auto done = crl::guard(this, [=](Result value, auto) {
		if (value == Result::Cancelled) {
			close();
		} else if (value == Result::Unsupported) {
			_parentShow->showToast(tr::lng_bot_menu_not_supported(tr::now));
			close();
		} else {
			requestSimple();
		}
	});
	_session->attachWebView().acceptMainMenuDisclaimer(
		_parentShow,
		_bot,
		done);
}

void WebViewInstance::requestWithMenuAdd() {
	using Result = AttachWebView::AddToMenuResult;
	const auto done = crl::guard(this, [=](Result value, PeerTypes types) {
		if (value == Result::Cancelled) {
			close();
		} else if (value == Result::Unsupported) {
			_parentShow->showToast(tr::lng_bot_menu_not_supported(tr::now));
			close();
		} else if (v::is<WebViewSourceLinkAttachMenu>(_source)) {
			maybeChooseAndRequestButton(types);
		} else if (v::is<WebViewSourceAttachMenu>(_source)) {
			requestButton();
		} else {
			requestSimple();
		}
	});
	_session->attachWebView().requestAddToMenu(_bot, done);
}

void WebViewInstance::maybeChooseAndRequestButton(PeerTypes supported) {
	Expects(v::is<WebViewSourceLinkAttachMenu>(_source));

	const auto link = v::get<WebViewSourceLinkAttachMenu>(_source);
	const auto chooseFrom = (link.choose & supported);
	if (!chooseFrom) {
		requestButton();
		return;
	}
	const auto bot = _bot;
	const auto button = _button;
	const auto weak = _context.controller;
	const auto done = [=](not_null<Data::Thread*> thread) {
		if (const auto controller = WindowForThread(weak, thread)) {
			thread->session().attachWebView().open({
				.bot = bot,
				.context = {
					.controller = controller,
					.action = Api::SendAction(thread),
				},
				.button = button,
				.source = InlineBots::WebViewSourceLinkAttachMenu{
					.thread = thread,
					.token = button.startCommand,
				},
			});
		}
	};
	ShowChooseBox(_parentShow, _session, chooseFrom, done);
	close();
}

void WebViewInstance::show(const QString &url, uint64 queryId) {
	auto title = Info::Profile::NameValue(_bot);
	auto titleBadge = _bot->isVerified()
		? object_ptr<Ui::RpWidget>(_parentShow->toastParent())
		: nullptr;
	if (titleBadge) {
		const auto raw = titleBadge.data();
		raw->paintRequest() | rpl::start_with_next([=] {
			auto p = Painter(raw);
			st::infoVerifiedCheck.paint(p, st::lineWidth, 0, raw->width());
		}, raw->lifetime());
		raw->resize(st::infoVerifiedCheck.size() + QSize(0, st::lineWidth));
	}

	const auto &bots = _session->attachWebView().attachBots();

	using Button = Ui::BotWebView::MenuButton;
	const auto attached = ranges::find(
		bots,
		not_null{ _bot },
		&AttachWebViewBot::user);
	const auto hasOpenBot = v::is<WebViewSourceMainMenu>(_source)
		|| (_context.action->history->peer != _bot);
	const auto hasRemoveFromMenu = (attached != end(bots))
		&& (!attached->inactive || attached->inMainMenu)
		&& (v::is<WebViewSourceMainMenu>(_source)
			|| v::is<WebViewSourceAttachMenu>(_source)
			|| v::is<WebViewSourceLinkAttachMenu>(_source));
	const auto buttons = (hasOpenBot ? Button::OpenBot : Button::None)
		| (!hasRemoveFromMenu
			? Button::None
			: attached->inMainMenu
			? Button::RemoveFromMainMenu
			: Button::RemoveFromMenu);
	const auto allowClipboardRead = v::is<WebViewSourceAttachMenu>(_source)
		|| v::is<WebViewSourceAttachMenu>(_source)
		|| (attached != end(bots)
			&& (attached->inAttachMenu || attached->inMainMenu));
	_panelUrl = url;
	_panel = Ui::BotWebView::Show({
		.url = url,
		.storageId = _session->local().resolveStorageIdBots(),
		.title = std::move(title),
		.titleBadge = std::move(titleBadge),
		.bottom = rpl::single('@' + _bot->username()),
		.delegate = static_cast<Ui::BotWebView::Delegate*>(this),
		.menuButtons = buttons,
		.allowClipboardRead = allowClipboardRead,
	});
	started(queryId);

	if (const auto strong = PendingActivation.get()) {
		if (strong == this) {
			PendingActivation = nullptr;
			_panel->requestActivate();
		}
	}
}

void WebViewInstance::showGame() {
	Expects(v::is<WebViewSourceGame>(_source));

	const auto game = v::get<WebViewSourceGame>(_source);
	_panelUrl = QString::fromUtf8(_button.url);
	_panel = Ui::BotWebView::Show({
		.url = _panelUrl,
		.storageId = _session->local().resolveStorageIdBots(),
		.title = rpl::single(game.title),
		.bottom = rpl::single('@' + _bot->username()),
		.delegate = static_cast<Ui::BotWebView::Delegate*>(this),
		.menuButtons = Ui::BotWebView::MenuButton::ShareGame,
	});
}

void WebViewInstance::close() {
	_session->attachWebView().close(this);
}

void WebViewInstance::started(uint64 queryId) {
	Expects(_context.action.has_value());

	if (!queryId) {
		return;
	}

	_session->data().webViewResultSent(
	) | rpl::filter([=](const Data::Session::WebViewResultSent &sent) {
		return (sent.queryId == queryId);
	}) | rpl::start_with_next([=] {
		close();
	}, _panel->lifetime());

	const auto action = *_context.action;
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

Webview::ThemeParams WebViewInstance::botThemeParams() {
	return Window::Theme::WebViewParams();
}

bool WebViewInstance::botHandleLocalUri(QString uri, bool keepOpen) {
	const auto local = Core::TryConvertUrlToLocal(uri);
	if (Core::InternalPassportLink(local)) {
		return true;
	} else if (!local.startsWith(u"tg://"_q, Qt::CaseInsensitive)
		&& !local.startsWith(u"tonsite://"_q, Qt::CaseInsensitive)) {
		return false;
	}
	const auto bot = _bot;
	const auto context = std::make_shared<WebViewContext>(_context);
	if (!keepOpen) {
		botClose();
	}
	crl::on_main([=] {
		if (bot->session().windows().empty()) {
			Core::App().domain().activate(&bot->session().account());
		}
		const auto window = !bot->session().windows().empty()
			? bot->session().windows().front().get()
			: nullptr;
		context->controller = window;
		const auto variant = QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = window,
			.botWebviewContext = context,
		});
		UrlClickHandler::Open(local, variant);
	});
	return true;
}

void WebViewInstance::botHandleInvoice(QString slug) {
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
	Payments::CheckoutProcess::Start(
		&_bot->session(),
		slug,
		reactivate,
		nonPanelPaymentFormFactory(reactivate));
}

auto WebViewInstance::nonPanelPaymentFormFactory(
	Fn<void(Payments::CheckoutResult)> reactivate)
-> Fn<void(Payments::NonPanelPaymentForm)> {
	using namespace Payments;
	const auto panel = base::make_weak(_panel.get());
	const auto weak = _context.controller;
	return [=](Payments::NonPanelPaymentForm form) {
		using CreditsFormDataPtr = std::shared_ptr<CreditsFormData>;
		using CreditsReceiptPtr = std::shared_ptr<CreditsReceiptData>;
		v::match(form, [&](const CreditsFormDataPtr &form) {
			if (const auto strong = panel.get()) {
				ProcessCreditsPayment(
					uiShow(),
					strong->toastParent().get(),
					form,
					reactivate);
			}
		}, [&](const CreditsReceiptPtr &receipt) {
			if (const auto controller = weak.get()) {
				ProcessCreditsReceipt(controller, receipt, reactivate);
			}
		}, [&](RealFormPresentedNotification) {
			_panel->hideForPayment();
		});
	};
}

void WebViewInstance::botHandleMenuButton(
		Ui::BotWebView::MenuButton button) {
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
	case Button::RemoveFromMainMenu: {
		const auto &bots = _session->attachWebView().attachBots();
		const auto attached = ranges::find(
			bots,
			_bot,
			&AttachWebViewBot::user);
		const auto name = (attached != end(bots))
			? attached->name
			: _bot->name();
		const auto done = crl::guard(this, [=] {
			const auto session = _session;
			const auto was = _parentShow;
			botClose();

			const auto active = Core::App().activeWindow();
			const auto show = active ? active->uiShow() : was;
			session->attachWebView().removeFromMenu(show, bot);
			if (active) {
				active->activate();
			}
		});
		const auto main = (button == Button::RemoveFromMainMenu);
		_panel->showBox(Ui::MakeConfirmBox({
			(main
				? tr::lng_bot_remove_from_side_menu_sure
				: tr::lng_bot_remove_from_menu_sure)(
					tr::now,
					lt_bot,
					Ui::Text::Bold(name),
					Ui::Text::WithEntities),
			done,
		}));
	} break;
	case Button::ShareGame: {
		const auto itemId = v::is<WebViewSourceGame>(_source)
			? v::get<WebViewSourceGame>(_source).messageId
			: FullMsgId();
		if (!_panel || !itemId) {
			return;
		} else if (const auto item = _session->data().message(itemId)) {
			FastShareMessage(uiShow(), item);
		} else {
			_panel->showToast({ tr::lng_message_not_found(tr::now) });
		}
	} break;
	}
}

bool WebViewInstance::botValidateExternalLink(QString uri) {
	const auto lower = uri.toLower();
	const auto allowed = _session->appConfig().get<std::vector<QString>>(
		"web_app_allowed_protocols",
		std::vector{ u"http"_q, u"https"_q });
	for (const auto &protocol : allowed) {
		if (lower.startsWith(protocol + u"://"_q)) {
			return true;
		}
	}
	return false;
}

void WebViewInstance::botOpenIvLink(QString uri) {
	const auto window = _context.controller.get();
	if (window) {
		Core::App().iv().openWithIvPreferred(window, uri);
	} else {
		Core::App().iv().openWithIvPreferred(_session, uri);
	}
}

void WebViewInstance::botSendData(QByteArray data) {
	Expects(_context.action.has_value());

	const auto button = std::get_if<WebViewSourceButton>(&_source);
	if (!button
		|| !button->simple
		|| _context.action->history->peer != _bot
		|| _dataSent) {
		return;
	}
	_dataSent = true;
	_session->api().request(MTPmessages_SendWebViewData(
		_bot->inputUser,
		MTP_long(base::RandomValue<uint64>()),
		MTP_string(_button.text),
		MTP_bytes(data)
	)).done([session = _session](const MTPUpdates &result) {
		session->api().applyUpdates(result);
	}).send();
	botClose();
}

void WebViewInstance::botSwitchInlineQuery(
		std::vector<QString> chatTypes,
		QString query) {
	const auto controller = _context.controller.get();
	const auto types = PeerTypesFromNames(chatTypes);
	if (!_bot
		|| !_bot->isBot()
		|| _bot->botInfo->inlinePlaceholder.isEmpty()
		|| !controller) {
		return;
	} else if (!types) {
		if (_context.dialogsEntryState.key.owningHistory()) {
			controller->switchInlineQuery(
				_context.dialogsEntryState,
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
	botClose();
}

void WebViewInstance::botCheckWriteAccess(Fn<void(bool allowed)> callback) {
	_session->api().request(MTPbots_CanSendMessage(
		_bot->inputUser
	)).done([=](const MTPBool &result) {
		callback(mtpIsTrue(result));
	}).fail([=] {
		callback(false);
	}).send();
}

void WebViewInstance::botAllowWriteAccess(Fn<void(bool allowed)> callback) {
	_session->api().request(MTPbots_AllowSendMessage(
		_bot->inputUser
	)).done([session = _session, callback](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		callback(true);
	}).fail([=] {
		callback(false);
	}).send();
}

void WebViewInstance::botSharePhone(Fn<void(bool shared)> callback) {
	const auto history = _bot->owner().history(_bot);
	if (_bot->isBlocked()) {
		const auto done = crl::guard(this, [=](bool success) {
			if (success) {
				botSharePhone(callback);
			} else {
				callback(false);
			}
		});
		_session->api().blockedPeers().unblock(_bot, done);
		return;
	}
	auto action = Api::SendAction(history);
	action.clearDraft = false;
	_session->api().shareContact(
		_session->user(),
		action,
		std::move(callback));
}

void WebViewInstance::botInvokeCustomMethod(
		Ui::BotWebView::CustomMethodRequest request) {
	const auto callback = request.callback;
	_session->api().request(MTPbots_InvokeWebViewCustomMethod(
		_bot->inputUser,
		MTP_string(request.method),
		MTP_dataJSON(MTP_bytes(request.params))
	)).done([=](const MTPDataJSON &result) {
		callback(result.data().vdata().v);
	}).fail([=](const MTP::Error &error) {
		callback(base::make_unexpected(error.type()));
	}).send();
}

void WebViewInstance::botOpenPrivacyPolicy() {
	const auto bot = _bot;
	const auto weak = _context.controller;
	const auto command = u"privacy"_q;
	const auto findCommand = [=] {
		if (!bot->isBot()) {
			return QString();
		}
		for (const auto &data : bot->botInfo->commands) {
			const auto isSame = !data.command.compare(
				command,
				Qt::CaseInsensitive);
			if (isSame) {
				return data.command;
			}
		}
		return QString();
	};
	const auto makeOtherContext = [=](bool forceWindow) {
		return QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = (forceWindow
				? WindowForThread(weak, bot->owner().history(bot))
				: weak),
			.peer = bot,
		});
	};
	const auto sendCommand = [=] {
		const auto original = findCommand();
		if (original.isEmpty()) {
			return false;
		}
		BotCommandClickHandler('/' + original).onClick(ClickContext{
			Qt::LeftButton,
			makeOtherContext(true)
		});
		return true;
	};
	const auto openUrl = [=](const QString &url) {
		Core::App().iv().openWithIvPreferred(
			&_bot->session(),
			url,
			makeOtherContext(false));
	};
	if (const auto info = _bot->botInfo.get()) {
		if (!info->privacyPolicyUrl.isEmpty()) {
			openUrl(info->privacyPolicyUrl);
			return;
		}
	}
	if (!sendCommand()) {
		openUrl(tr::lng_profile_bot_privacy_url(tr::now));
	}
}

void WebViewInstance::botClose() {
	crl::on_main(this, [=] { close(); });
}

std::shared_ptr<Main::SessionShow> WebViewInstance::uiShow() {
	class Show final : public Main::SessionShow {
	public:
		explicit Show(not_null<WebViewInstance*> that) : _that(that) {
		}

		void showOrHideBoxOrLayer(
				std::variant<
				v::null_t,
				object_ptr<Ui::BoxContent>,
				std::unique_ptr<Ui::LayerWidget>> &&layer,
				Ui::LayerOptions options,
				anim::type animated) const override {
			using UniqueLayer = std::unique_ptr<Ui::LayerWidget>;
			using ObjectBox = object_ptr<Ui::BoxContent>;
			const auto panel = _that ? _that->_panel.get() : nullptr;
			if (v::is<UniqueLayer>(layer)) {
				Unexpected("Layers in WebView are not implemented.");
			} else if (auto box = std::get_if<ObjectBox>(&layer)) {
				if (panel) {
					panel->showBox(std::move(*box), options, animated);
				}
			} else if (panel) {
				panel->hideLayer(animated);
			}
		}
		[[nodiscard]] not_null<QWidget*> toastParent() const override {
			const auto panel = _that ? _that->_panel.get() : nullptr;

			Ensures(panel != nullptr);
			return panel->toastParent();
		}
		[[nodiscard]] bool valid() const override {
			return _that && (_that->_panel != nullptr);
		}
		operator bool() const override {
			return valid();
		}

		[[nodiscard]] Main::Session &session() const override {
			Expects(_that.get() != nullptr);

			return *_that->_session;
		}

	private:
		const base::weak_ptr<WebViewInstance> _that;

	};
	return std::make_shared<Show>(this);
}

AttachWebView::AttachWebView(not_null<Main::Session*> session)
: _session(session)
, _refreshTimer([=] { requestBots(); }) {
	_refreshTimer.callEach(kRefreshBotsTimeout);
}

AttachWebView::~AttachWebView() {
	closeAll();
	_session->api().request(_popularAppBotsRequestId).cancel();
}

void AttachWebView::openByUsername(
		not_null<Window::SessionController*> controller,
		const Api::SendAction &action,
		const QString &botUsername,
		const QString &startCommand) {
	if (botUsername.isEmpty()
		|| (_botUsername == botUsername && _startCommand == startCommand)) {
		return;
	}
	cancel();

	_botUsername = botUsername;
	_startCommand = startCommand;
	const auto weak = base::make_weak(controller);
	const auto show = controller->uiShow();
	resolveUsername(show, crl::guard(weak, [=](not_null<PeerData*> peer) {
		_botUsername = QString();
		const auto token = base::take(_startCommand);

		const auto bot = peer->asUser();
		if (!bot || !bot->isBot()) {
			if (const auto strong = weak.get()) {
				strong->showToast(tr::lng_bot_menu_not_supported(tr::now));
			}
			return;
		}

		open({
			.bot = bot,
			.context = {
				.controller = controller,
				.action = action,
			},
			.button = { .startCommand = token },
			.source = InlineBots::WebViewSourceLinkAttachMenu{},
		});
	}));
}

void AttachWebView::close(not_null<WebViewInstance*> instance) {
	const auto i = ranges::find(
		_instances,
		instance.get(),
		&std::unique_ptr<WebViewInstance>::get);
	if (i != end(_instances)) {
		const auto taken = base::take(*i);
		_instances.erase(i);
	}
}

void AttachWebView::closeAll() {
	cancel();
	base::take(_instances);
}

void AttachWebView::loadPopularAppBots() {
	if (_popularAppBotsLoaded.current() || _popularAppBotsRequestId) {
		return;
	}
	_popularAppBotsRequestId = _session->api().request(
		MTPbots_GetPopularAppBots(
			MTP_string(),
			MTP_int(kPopularAppBotsLimit))
	).done([=](const MTPbots_PopularAppBots &result) {
		_popularAppBotsRequestId = 0;

		const auto &list = result.data().vusers().v;
		auto parsed = std::vector<not_null<UserData*>>();
		parsed.reserve(list.size());
		for (const auto &user : list) {
			const auto bot = _session->data().processUser(user);
			if (bot->isBot()) {
				parsed.push_back(bot);
			}
		}
		_popularAppBots = std::move(parsed);
		_popularAppBotsLoaded = true;
	}).send();
}

auto AttachWebView::popularAppBots() const
-> const std::vector<not_null<UserData*>> & {
	return _popularAppBots;
}

rpl::producer<> AttachWebView::popularAppBotsLoaded() const {
	return _popularAppBotsLoaded.changes() | rpl::to_empty;
}

void AttachWebView::cancel() {
	_session->api().request(base::take(_requestId)).cancel();
	_botUsername = QString();
	_startCommand = QString();
}

void AttachWebView::requestBots(Fn<void()> callback) {
	if (callback) {
		_botsRequestCallbacks.push_back(std::move(callback));
	}
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
					_attachBots.push_back(std::move(*parsed));
				}
			}
			_attachBotsUpdates.fire({});
		});
		for (const auto &callback : base::take(_botsRequestCallbacks)) {
			callback();
		}
	}).fail([=] {
		_botsRequestId = 0;
		for (const auto &callback : base::take(_botsRequestCallbacks)) {
			callback();
		}
	}).send();
}

bool AttachWebView::disclaimerAccepted(const AttachWebViewBot &bot) const {
	return _disclaimerAccepted.contains(bot.user);
}

bool AttachWebView::showMainMenuNewBadge(
		const AttachWebViewBot &bot) const {
	return bot.inMainMenu
		&& bot.disclaimerRequired
		&& !disclaimerAccepted(bot);
}

void AttachWebView::requestAddToMenu(
		not_null<UserData*> bot,
		Fn<void(AddToMenuResult, PeerTypes supported)> done) {
	auto &process = _addToMenu[bot];
	if (done) {
		process.done.push_back(std::move(done));
	}
	if (process.requestId) {
		return;
	}

	const auto finish = [=](AddToMenuResult result, PeerTypes supported) {
		if (auto process = _addToMenu.take(bot)) {
			for (const auto &done : process->done) {
				done(result, supported);
			}
		}
	};
	if (!bot->isBot() || !bot->botInfo->supportsAttachMenu) {
		finish(AddToMenuResult::Unsupported, {});
		return;
	}

	process.requestId = _session->api().request(
		MTPmessages_GetAttachMenuBot(bot->inputUser)
	).done([=](const MTPAttachMenuBotsBot &result) {
		_addToMenu[bot].requestId = 0;
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		const auto parsed = ParseAttachBot(_session, data.vbot());
		if (!parsed || bot != parsed->user) {
			finish(AddToMenuResult::Unsupported, {});
			return;
		}
		const auto i = ranges::find(
			_attachBots,
			not_null(bot),
			&AttachWebViewBot::user);
		if (i != end(_attachBots)) {
			// Save flags in our list, like 'inactive'.
			*i = *parsed;
		}
		const auto types = parsed->types;
		if (parsed->inactive) {
			confirmAddToMenu(*parsed, [=](bool added) {
				const auto result = added
					? AddToMenuResult::Added
					: AddToMenuResult::Cancelled;
				finish(result, types);
			});
		} else {
			requestBots();
			finish(AddToMenuResult::AlreadyInMenu, types);
		}
	}).fail([=] {
		finish(AddToMenuResult::Unsupported, {});
	}).send();
}

void AttachWebView::removeFromMenu(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot) {
	toggleInMenu(bot, ToggledState::Removed, [=](bool success) {
		if (success) {
			show->showToast(tr::lng_bot_remove_from_menu_done(tr::now));
		}
	});
}

void AttachWebView::resolveUsername(
		std::shared_ptr<Ui::Show> show,
		Fn<void(not_null<PeerData*>)> done) {
	if (const auto peer = _session->data().peerByUsername(_botUsername)) {
		done(peer);
		return;
	}
	_session->api().request(base::take(_requestId)).cancel();
	_requestId = _session->api().request(MTPcontacts_ResolveUsername(
		MTP_string(_botUsername)
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
			show->showToast(
				tr::lng_username_not_found(tr::now, lt_user, _botUsername));
		}
	}).send();
}

void AttachWebView::open(WebViewDescriptor &&descriptor) {
	for (const auto &instance : _instances) {
		if (instance->bot() == descriptor.bot
			&& instance->source() == descriptor.source) {
			instance->activate();
			return;
		}
	}
	_instances.push_back(
		std::make_unique<WebViewInstance>(std::move(descriptor)));
	_instances.back()->activate();
}

void AttachWebView::acceptMainMenuDisclaimer(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		Fn<void(AddToMenuResult, PeerTypes supported)> done) {
	const auto i = ranges::find(_attachBots, bot, &AttachWebViewBot::user);
	if (i == end(_attachBots)) {
		_attachBotsUpdates.fire({});
		return;
	} else if (i->inactive) {
		requestAddToMenu(bot, std::move(done));
		return;
	} else if (!i->disclaimerRequired || disclaimerAccepted(*i)) {
		done(AddToMenuResult::AlreadyInMenu, i->types);
		return;
	}
	const auto types = i->types;
	show->show(Box(FillDisclaimerBox, crl::guard(this, [=](bool accepted) {
		if (accepted) {
			_disclaimerAccepted.emplace(bot);
			_attachBotsUpdates.fire({});
			done(AddToMenuResult::AlreadyInMenu, types);
		} else {
			done(AddToMenuResult::Cancelled, {});
		}
	})));
}

void AttachWebView::confirmAddToMenu(
		AttachWebViewBot bot,
		Fn<void(bool added)> callback) {
	const auto active = Core::App().activeWindow();
	if (!active) {
		if (callback) {
			callback(false);
		}
		return;
	}
	const auto weak = base::make_weak(active);
	active->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto allowed = std::make_shared<Ui::Checkbox*>();
		const auto disclaimer = !disclaimerAccepted(bot);
		const auto done = [=](Fn<void()> close) {
			const auto state = (disclaimer
				|| ((*allowed) && (*allowed)->checked()))
				? ToggledState::AllowedToWrite
				: ToggledState::Added;
			toggleInMenu(bot.user, state, [=](bool success) {
				if (callback) {
					callback(success);
				}
				if (const auto strong = weak.get()) {
					strong->showToast((bot.inMainMenu
						? tr::lng_bot_add_to_side_menu_done
						: tr::lng_bot_add_to_menu_done)(tr::now));
				}
			});
			close();
		};
		if (disclaimer) {
			FillDisclaimerBox(box, [=](bool accepted) {
				if (accepted) {
					_disclaimerAccepted.emplace(bot.user);
					_attachBotsUpdates.fire({});
					done([] {});
				} else if (callback) {
					callback(false);
				}
			});
			box->addRow(object_ptr<Ui::FixedHeightWidget>(
				box,
				st::boxRowPadding.left()));
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_bot_will_be_added(
					lt_bot,
					rpl::single(Ui::Text::Bold(bot.name)),
					Ui::Text::WithEntities),
				st::boxLabel));
		} else {
			Ui::ConfirmBox(box, {
				(bot.inMainMenu
					? tr::lng_bot_add_to_side_menu
					: tr::lng_bot_add_to_menu)(
						tr::now,
						lt_bot,
						Ui::Text::Bold(bot.name),
						Ui::Text::WithEntities),
				done,
				(callback
					? [=](Fn<void()> close) { callback(false); close(); }
					: Fn<void(Fn<void()>)>()),
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
						(disclaimer
							? st::boxPhotoCaptionSkip
							: st::boxRowPadding.left()),
						st::boxRowPadding.right(),
						st::boxRowPadding.left()));
				(*allowed)->setAllowTextLines();
			}
		}
	}));
}

void AttachWebView::toggleInMenu(
		not_null<UserData*> bot,
		ToggledState state,
		Fn<void(bool success)> callback) {
	using Flag = MTPmessages_ToggleBotInAttachMenu::Flag;
	_session->api().request(MTPmessages_ToggleBotInAttachMenu(
		MTP_flags((state == ToggledState::AllowedToWrite)
			? Flag::f_write_allowed
			: Flag()),
		bot->inputUser,
		MTP_bool(state != ToggledState::Removed)
	)).done([=] {
		_requestId = 0;
		_session->api().request(base::take(_botsRequestId)).cancel();
		requestBots(callback ? [=] { callback(true); } : Fn<void()>());
	}).fail([=] {
		cancel();
		if (callback) {
			callback(false);
		}
	}).send();
}

void ChooseAndSendLocation(
		not_null<Window::SessionController*> controller,
		const Ui::LocationPickerConfig &config,
		Api::SendAction action) {
	const auto session = &controller->session();
	if (const auto picker = session->locationPickers().lookup(action)) {
		picker->activate();
		return;
	}
	const auto callback = [=](Data::InputVenue venue) {
		if (venue.justLocation()) {
			Api::SendLocation(action, venue.lat, venue.lon);
		} else {
			Api::SendVenue(action, venue);
		}
	};
	const auto picker = Ui::LocationPicker::Show({
		.parent = controller->widget(),
		.config = config,
		.chooseLabel = tr::lng_maps_point_send(),
		.recipient = action.history->peer,
		.session = session,
		.callback = crl::guard(session, callback),
		.quit = [] { Shortcuts::Launch(Shortcuts::Command::Quit); },
		.storageId = session->local().resolveStorageIdBots(),
		.closeRequests = controller->content()->death(),
	});
	session->locationPickers().emplace(action, picker);
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
	if (peer->canCreatePolls()) {
		++minimal;
		raw->addAction(tr::lng_polls_create(tr::now), [=] {
			const auto action = actionFactory();
			const auto source = action.options.scheduled
				? Api::SendType::Scheduled
				: Api::SendType::Normal;
			const auto sendMenuType = action.replyTo.topicRootId
				? SendMenu::Type::SilentOnly
				: SendMenu::Type::Scheduled;
			const auto flag = PollData::Flags();
			const auto replyTo = action.replyTo;
			Window::PeerMenuCreatePoll(
				controller,
				peer,
				replyTo,
				flag,
				flag,
				source,
				{ sendMenuType });
		}, &st::menuIconCreatePoll);
	}
	const auto session = &controller->session();
	const auto locationType = ChatRestriction::SendOther;
	const auto config = ResolveMapsConfig(session);
	if (Data::CanSendAnyOf(peer, locationType)
		&& Ui::LocationPicker::Available(config)) {
		raw->addAction(tr::lng_maps_point(tr::now), [=] {
			ChooseAndSendLocation(controller, config, actionFactory());
		}, &st::menuIconAddress);
	}
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inAttachMenu
			|| !PeerMatchesTypes(peer, bot.user, bot.types)) {
			continue;
		}
		const auto callback = [=] {
			bots->open({
				.bot = bot.user,
				.context = {
					.controller = controller,
					.action = actionFactory(),
				},
				.source = InlineBots::WebViewSourceAttachMenu(),
			});
		};
		auto action = base::make_unique_q<BotAction>(
			raw,
			controller->uiShow(),
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
