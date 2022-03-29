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
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "info/profile/info_profile_values.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toasts/common_toasts.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/effects/ripple_animation.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "apiwrap.h"
#include "styles/style_menu_icons.h"

namespace InlineBots {
namespace {

constexpr auto kProlongTimeout = 60 * crl::time(1000);

struct ParsedBot {
	UserData *bot = nullptr;
	bool inactive = false;
};

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
				.icon = session->data().processDocument(
					data.vattach_menu_icon()),
				.name = qs(data.vattach_menu_name()),
				.inactive = data.is_inactive(),
			} : std::optional<AttachWebViewBot>();
	});
	if (result) {
		result->icon->forceToCache(true);
	}
	return result;
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

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const AttachWebViewBot _bot;

	Ui::Text::String _text;
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
	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

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

	const auto normalHeight = _st.itemPadding.top()
		+ _st.itemStyle.font->height
		+ _st.itemPadding.bottom();
	const auto deltaHeight = _height - normalHeight;
	st::menuIconDelete.paint(
		p,
		_st.itemIconPosition + QPoint(0, deltaHeight / 2),
		width());

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

QPoint BotAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage BotAction::prepareRippleMask() const {
	return Ui::RippleAnimation::rectMask(size());
}

int BotAction::contentHeight() const {
	return _height;
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

AttachWebView::AttachWebView(not_null<Main::Session*> session)
: _session(session) {
}

AttachWebView::~AttachWebView() {
	ActiveWebViews().remove(this);
}

void AttachWebView::request(
		not_null<PeerData*> peer,
		const QString &botUsername) {
	const auto username = _bot ? _bot->username : _botUsername;
	if (_peer == peer && username.toLower() == botUsername.toLower()) {
		if (_panel) {
			_panel->requestActivate();
		}
		return;
	}
	cancel();

	_peer = peer;
	_botUsername = botUsername;
	resolve();
}

void AttachWebView::request(
		not_null<PeerData*> peer,
		not_null<UserData*> bot,
		const WebViewButton &button) {
	if (_peer == peer && _bot == bot) {
		if (_panel) {
			_panel->requestActivate();
		} else if (_requestId) {
			return;
		}
	}
	cancel();

	_bot = bot;
	_peer = peer;
	request(button);
}

void AttachWebView::request(const WebViewButton &button) {
	Expects(_peer != nullptr && _bot != nullptr);

	using Flag = MTPmessages_RequestWebView::Flag;
	const auto flags = Flag::f_theme_params
		| (button.url.isEmpty() ? Flag(0) : Flag::f_url);
	_requestId = _session->api().request(MTPmessages_RequestWebView(
		MTP_flags(flags),
		_peer->input,
		_bot->inputUser,
		MTP_bytes(button.url),
		MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams())),
		MTPint() // reply_to_msg_id
	)).done([=](const MTPWebViewResult &result) {
		_requestId = 0;
		result.match([&](const MTPDwebViewResultUrl &data) {
			show(data.vquery_id().v, qs(data.vurl()), button.text);
		}, [&](const MTPDwebViewResultConfirmationRequired &data) {
			_session->data().processUsers(data.vusers());
			const auto &received = data.vbot();
			if (const auto bot = ParseAttachBot(_session, received)) {
				if (_bot != bot->user) {
					cancel();
					return;
				}
				confirmAddToMenu(*bot, [=] {
					request(button);
				});
			} else {
				cancel();
			}
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		int a = error.code();
	}).send();
}

void AttachWebView::cancel() {
	ActiveWebViews().remove(this);
	_session->api().request(base::take(_requestId)).cancel();
	_session->api().request(base::take(_prolongId)).cancel();
	_panel = nullptr;
	_peer = _bot = nullptr;
	_botUsername = QString();
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
						parsed->media = parsed->icon->createMediaView();
						parsed->icon->save(Data::FileOrigin(), {});
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

void AttachWebView::requestAddToMenu(not_null<UserData*> bot) {
	if (!bot->isBot() || !bot->botInfo->supportsAttachMenu) {
		return;
	} else if (_addToMenuId) {
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
		const auto requested = base::take(_addToMenuBot);
		result.match([&](const MTPDattachMenuBotsBot &data) {
			_session->data().processUsers(data.vusers());
			if (const auto bot = ParseAttachBot(_session, data.vbot())) {
				if (requested == bot->user) {
					if (bot->inactive) {
						confirmAddToMenu(*bot);
					} else {
						requestBots();
						Ui::ShowMultilineToast({
							.text = { u"Bot is already added."_q },
						});
					}
				}
			}
		});
	}).fail([=] {
		_addToMenuId = 0;
		_addToMenuBot = nullptr;
		Ui::ShowMultilineToast({
			.text = { u"Bot cannot be added to the menu."_q },
			});
	}).send();
}

void AttachWebView::removeFromMenu(not_null<UserData*> bot) {
	toggleInMenu(bot, false, nullptr);
}

void AttachWebView::resolve() {
	if (!_bot) {
		requestByUsername();
	}
}

void AttachWebView::requestByUsername() {
	resolveUsername(_botUsername, [=](not_null<PeerData*> bot) {
		_bot = bot->asUser();
		if (!_bot || !_bot->isBot() || !_bot->botInfo->supportsAttachMenu) {
			Ui::ShowMultilineToast({
				// #TODO webview lang
				.text = { u"This bot isn't supported in the attach menu."_q }
			});
			return;
		}
		request();
	});
}

void AttachWebView::resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done) {
	if (const auto peer = _peer->owner().peerByUsername(username)) {
		done(peer);
		return;
	}
	_session->api().request(base::take(_requestId)).cancel();
	_requestId = _session->api().request(MTPcontacts_ResolveUsername(
		MTP_string(username)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		_requestId = 0;
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_peer->owner().processUsers(data.vusers());
			_peer->owner().processChats(data.vchats());
			if (const auto peerId = peerFromMTP(data.vpeer())) {
				done(_peer->owner().peer(peerId));
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
		not_null<UserData*> bot,
		const WebViewButton &button) {
	cancel();
	_bot = bot;
	_peer = bot;
	using Flag = MTPmessages_RequestSimpleWebView::Flag;
	_requestId = _session->api().request(MTPmessages_RequestSimpleWebView(
		MTP_flags(Flag::f_theme_params),
		bot->inputUser,
		MTP_bytes(button.url),
		MTP_dataJSON(MTP_bytes(Window::Theme::WebViewParams()))
	)).done([=](const MTPSimpleWebViewResult &result) {
		_requestId = 0;
		result.match([&](const MTPDsimpleWebViewResultUrl &data) {
			const auto queryId = uint64();
			show(queryId, qs(data.vurl()), button.text);
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		int a = error.code();
	}).send();
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
	Expects(_bot != nullptr && _peer != nullptr);

	const auto close = crl::guard(this, [=] {
		cancel();
	});
	const auto sendData = crl::guard(this, [=](QByteArray data) {
		if (_peer != _bot) {
			cancel();
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
		cancel();
	});
	auto title = Info::Profile::NameValue(
		_bot
	) | rpl::map([](const TextWithEntities &value) {
		return value.text;
	});
	ActiveWebViews().emplace(this);
	_panel = Ui::BotWebView::Show({
		.url = url,
		.userDataPath = _session->domain().local().webviewDataPath(),
		.title = std::move(title),
		.bottom = rpl::single('@' + _bot->username),
		.sendData = sendData,
		.close = close,
		.themeParams = [] { return Window::Theme::WebViewParams(); },
	});
	started(queryId);
}

void AttachWebView::started(uint64 queryId) {
	Expects(_peer != nullptr && _bot != nullptr);

	_session->data().webViewResultSent(
	) | rpl::filter([=](const Data::Session::WebViewResultSent &sent) {
		return (sent.peerId == _peer->id)
			&& (sent.botId == _bot->id)
			&& (sent.queryId == queryId);
	}) | rpl::start_with_next([=] {
		cancel();
	}, _panel->lifetime());

	base::timer_each(
		kProlongTimeout
	) | rpl::start_with_next([=] {
		using Flag = MTPmessages_ProlongWebView::Flag;
		auto flags = Flag::f_reply_to_msg_id | Flag::f_silent;
		_session->api().request(base::take(_prolongId)).cancel();
		_prolongId = _session->api().request(MTPmessages_ProlongWebView(
			MTP_flags(flags),
			_peer->input,
			_bot->inputUser,
			MTP_long(queryId),
			MTP_int(_replyToMsgId.bare)
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
			close();
		});
	};
	const auto active = Core::App().activeWindow();
	if (!active) {
		return;
	}
	_confirmAddBox = active->show(Ui::MakeConfirmBox({
		u"Do you want to? "_q + bot.name,
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
		not_null<Window::SessionController*> controller) {
	auto result = std::make_unique<Ui::DropdownMenu>(
		parent,
		st::dropdownMenuWithIcons);
	const auto bots = &controller->session().attachWebView();
	const auto raw = result.get();
	const auto refresh = [=] {
		raw->clearActions();
		for (const auto &bot : bots->attachBots()) {
			raw->addAction(base::make_unique_q<BotAction>(raw, bot, raw->menu()->st(), [=] {
				const auto active = controller->activeChatCurrent();
				if (const auto history = active.history()) {
					bots->request(history->peer, bot.user);
				}
			}));
		}
	};
	refresh();
	bots->attachBotsUpdates(
	) | rpl::start_with_next(refresh, raw->lifetime());

	return result;
}

} // namespace InlineBots
