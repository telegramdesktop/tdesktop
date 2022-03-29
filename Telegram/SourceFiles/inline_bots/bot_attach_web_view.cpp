/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/bot_attach_web_view.h"

#include "data/data_user.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toasts/common_toasts.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "base/random.h"
#include "apiwrap.h"

namespace InlineBots {
namespace {

[[nodiscard]] UserData *ParseAttachBot(
		not_null<Main::Session*> session,
		const MTPAttachMenuBot &bot) {
	return bot.match([&](const MTPDattachMenuBot &data) {
		const auto user = session->data().userLoaded(UserId(data.vbot_id()));
		return (user && user->isBot() && user->botInfo->supportsAttachMenu)
			? user
			: nullptr;
	});
}

} // namespace

AttachWebView::AttachWebView(not_null<Main::Session*> session)
: _session(session) {
}

AttachWebView::~AttachWebView() = default;

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
				if (_bot != bot) {
					cancel();
					return;
				}
				requestAddToMenu([=] {
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
	_session->api().request(base::take(_requestId)).cancel();
	_panel = nullptr;
	_peer = _bot = nullptr;
	_botUsername = QString();
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
	_panel = Ui::BotWebView::Show({
		.url = url,
		.userDataPath = _session->domain().local().webviewDataPath(),
		.sendData = sendData,
		.close = close,
		.themeParams = [] { return Window::Theme::WebViewParams(); },
	});

	_session->data().webViewResultSent(
	) | rpl::filter([=](const Data::Session::WebViewResultSent &sent) {
		return (sent.peerId == _peer->id)
			&& (sent.botId == _bot->id)
			&& (sent.queryId == queryId);
	}) | rpl::start_with_next([=] {
		cancel();
	}, _panel->lifetime());
}

void AttachWebView::requestAddToMenu(Fn<void()> callback) {
	Expects(_bot != nullptr);

	const auto done = [=](Fn<void()> close) {
		toggleInMenu( true, [=] {
			callback();
			close();
		});
	};
	const auto active = Core::App().activeWindow();
	if (!active) {
		return;
	}
	_confirmAddBox = active->show(Ui::MakeConfirmBox({
		u"Do you want to? "_q + _bot->name,
		done,
	}));
}

void AttachWebView::toggleInMenu(bool enabled, Fn<void()> callback) {
	Expects(_bot != nullptr);

	_requestId = _session->api().request(MTPmessages_ToggleBotInAttachMenu(
		_bot->inputUser,
		MTP_bool(enabled)
	)).done([=](const MTPBool &result) {
		_requestId = 0;
		callback();
	}).fail([=] {
		cancel();
	}).send();
}

} // namespace InlineBots
