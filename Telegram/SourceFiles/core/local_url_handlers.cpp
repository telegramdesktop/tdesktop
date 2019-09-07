/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/local_url_handlers.h"

#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "boxes/confirm_phone_box.h"
#include "boxes/background_preview_box.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "boxes/connection_box.h"
#include "boxes/sticker_set_box.h"
#include "passport/passport_form_controller.h"
#include "window/window_session_controller.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Core {
namespace {

using Match = qthelp::RegularExpressionMatch;

bool JoinGroupByHash(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	const auto hash = match->captured(1);
	session->api().checkChatInvite(hash, [=](const MTPChatInvite &result) {
		Core::App().hideMediaView();
		result.match([=](const MTPDchatInvite &data) {
			Ui::show(Box<ConfirmInviteBox>(session, data, [=] {
				session->api().importChatInvite(hash);
			}));
		}, [=](const MTPDchatInviteAlready &data) {
			if (const auto chat = session->data().processChat(data.vchat())) {
				App::wnd()->sessionController()->showPeerHistory(
					chat,
					Window::SectionShow::Way::Forward);
			}
		});
	}, [=](const RPCError &error) {
		if (error.code() != 400) {
			return;
		}
		Core::App().hideMediaView();
		Ui::show(Box<InformBox>(tr::lng_group_invite_bad_link(tr::now)));
	});
	return true;
}

bool ShowStickerSet(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	Core::App().hideMediaView();
	Ui::show(Box<StickerSetBox>(
		App::wnd()->sessionController(),
		MTP_inputStickerSetShortName(MTP_string(match->captured(1)))));
	return true;
}

bool ShowTheme(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	Core::App().hideMediaView();
	Ui::show(Box<InformBox>(tr::lng_theme_no_desktop_version(tr::now)));
	return true;
}

bool SetLanguage(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	const auto languageId = match->captured(1);
	Lang::CurrentCloudManager().switchWithWarning(languageId);
	return true;
}

bool ShareUrl(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	auto url = params.value(qsl("url"));
	if (url.isEmpty()) {
		return false;
	}
	App::main()->shareUrlLayer(url, params.value("text"));
	return true;
}

bool ConfirmPhone(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	auto phone = params.value(qsl("phone"));
	auto hash = params.value(qsl("hash"));
	if (phone.isEmpty() || hash.isEmpty()) {
		return false;
	}
	ConfirmPhoneBox::start(phone, hash);
	return true;
}

bool ShareGameScore(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ShareGameScoreByHash(session, params.value(qsl("hash")));
	return true;
}

bool ApplySocksProxy(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ProxiesBoxController::ShowApplyConfirmation(ProxyData::Type::Socks5, params);
	return true;
}

bool ApplyMtprotoProxy(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ProxiesBoxController::ShowApplyConfirmation(ProxyData::Type::Mtproto, params);
	return true;
}

bool ShowPassportForm(const QMap<QString, QString> &params) {
	const auto botId = params.value("bot_id", QString()).toInt();
	const auto scope = params.value("scope", QString());
	const auto callback = params.value("callback_url", QString());
	const auto publicKey = params.value("public_key", QString());
	const auto nonce = params.value(
		Passport::NonceNameByScope(scope),
		QString());
	const auto errors = params.value("errors", QString());
	if (const auto window = App::wnd()) {
		if (const auto controller = window->sessionController()) {
			controller->showPassportForm(Passport::FormRequest(
				botId,
				scope,
				callback,
				publicKey,
				nonce,
				errors));
			return true;
		}
	}
	return false;
}

bool ShowPassport(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	return ShowPassportForm(url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower));
}

bool ShowWallPaper(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	return BackgroundPreviewBox::Start(
		session,
		params.value(qsl("slug")),
		params);
}

bool ResolveUsername(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto domain = params.value(qsl("domain"));
	const auto valid = [](const QString &domain) {
		return qthelp::regex_match(
			qsl("^[a-zA-Z0-9\\.\\_]+$"),
			domain,
			{}
		).valid();
	};
	if (domain == qsl("telegrampassport")) {
		return ShowPassportForm(params);
	} else if (!valid(domain)) {
		return false;
	}
	auto start = qsl("start");
	auto startToken = params.value(start);
	if (startToken.isEmpty()) {
		start = qsl("startgroup");
		startToken = params.value(start);
		if (startToken.isEmpty()) {
			start = QString();
		}
	}
	auto post = (start == qsl("startgroup"))
		? ShowAtProfileMsgId
		: ShowAtUnreadMsgId;
	auto postParam = params.value(qsl("post"));
	if (auto postId = postParam.toInt()) {
		post = postId;
	}
	const auto gameParam = params.value(qsl("game"));
	if (!gameParam.isEmpty() && valid(gameParam)) {
		startToken = gameParam;
		post = ShowAtGameShareMsgId;
	}
	const auto clickFromMessageId = context.value<FullMsgId>();
	App::main()->openPeerByName(
		domain,
		post,
		startToken,
		clickFromMessageId);
	return true;
}

bool ResolvePrivatePost(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto channelId = params.value(qsl("channel")).toInt();
	const auto msgId = params.value(qsl("post")).toInt();
	if (!channelId || !IsServerMsgId(msgId)) {
		return false;
	}
	const auto done = [=](not_null<PeerData*> peer) {
		App::wnd()->sessionController()->showPeerHistory(
			peer->id,
			Window::SectionShow::Way::Forward,
			msgId);
	};
	const auto fail = [=] {
		Ui::show(Box<InformBox>(tr::lng_error_post_link_invalid(tr::now)));
	};
	if (const auto channel = session->data().channelLoaded(channelId)) {
		done(channel);
		return true;
	}
	session->api().request(MTPchannels_GetChannels(
		MTP_vector<MTPInputChannel>(
			1,
			MTP_inputChannel(MTP_int(channelId), MTP_long(0)))
	)).done([=](const MTPmessages_Chats &result) {
		result.match([&](const auto &data) {
			const auto peer = session->data().processChats(data.vchats());
			if (peer && peer->id == peerFromChannel(channelId)) {
				done(peer);
			} else {
				fail();
			}
		});
	}).fail([=](const RPCError &error) {
		fail();
	}).send();
	return true;
}

bool HandleUnknown(
		Main::Session *session,
		const Match &match,
		const QVariant &context) {
	if (!session) {
		return false;
	}
	const auto request = match->captured(1);
	const auto callback = [=](const MTPDhelp_deepLinkInfo &result) {
		const auto text = TextWithEntities{
			qs(result.vmessage()),
			TextUtilities::EntitiesFromMTP(
				result.ventities().value_or_empty())
		};
		if (result.is_update_app()) {
			const auto box = std::make_shared<QPointer<BoxContent>>();
			const auto callback = [=] {
				Core::UpdateApplication();
				if (*box) (*box)->closeBox();
			};
			*box = Ui::show(Box<ConfirmBox>(
				text,
				tr::lng_menu_update(tr::now),
				callback));
		} else {
			Ui::show(Box<InformBox>(text));
		}
	};
	session->api().requestDeepLinkInfo(request, callback);
	return true;
}

} // namespace

const std::vector<LocalUrlHandler> &LocalUrlHandlers() {
	static auto Result = std::vector<LocalUrlHandler>{
		{
			qsl("^join/?\\?invite=([a-zA-Z0-9\\.\\_\\-]+)(&|$)"),
			JoinGroupByHash
		},
		{
			qsl("^addstickers/?\\?set=([a-zA-Z0-9\\.\\_]+)(&|$)"),
			ShowStickerSet
		},
		{
			qsl("^addtheme/?\\?slug=([a-zA-Z0-9\\.\\_]+)(&|$)"),
			ShowTheme
		},
		{
			qsl("^setlanguage/?\\?lang=([a-zA-Z0-9\\.\\_\\-]+)(&|$)"),
			SetLanguage
		},
		{
			qsl("^msg_url/?\\?(.+)(#|$)"),
			ShareUrl
		},
		{
			qsl("^confirmphone/?\\?(.+)(#|$)"),
			ConfirmPhone
		},
		{
			qsl("^share_game_score/?\\?(.+)(#|$)"),
			ShareGameScore
		},
		{
			qsl("^socks/?\\?(.+)(#|$)"),
			ApplySocksProxy
		},
		{
			qsl("^proxy/?\\?(.+)(#|$)"),
			ApplyMtprotoProxy
		},
		{
			qsl("^passport/?\\?(.+)(#|$)"),
			ShowPassport
		},
		{
			qsl("^bg/?\\?(.+)(#|$)"),
			ShowWallPaper
		},
		{
			qsl("^resolve/?\\?(.+)(#|$)"),
			ResolveUsername
		},
		{
			qsl("^privatepost/?\\?(.+)(#|$)"),
			ResolvePrivatePost
		},
		{
			qsl("^([^\\?]+)(\\?|#|$)"),
			HandleUnknown
		}
	};
	return Result;
}

QString TryConvertUrlToLocal(QString url) {
	if (url.size() > 8192) {
		url = url.mid(0, 8192);
	}

	using namespace qthelp;
	auto matchOptions = RegExOption::CaseInsensitive;
	auto telegramMeMatch = regex_match(qsl("^(https?://)?(www\\.)?(telegram\\.(me|dog)|t\\.me)/(.+)$"), url, matchOptions);
	if (telegramMeMatch) {
		auto query = telegramMeMatch->capturedRef(5);
		if (auto joinChatMatch = regex_match(qsl("^joinchat/([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"), query, matchOptions)) {
			return qsl("tg://join?invite=") + url_encode(joinChatMatch->captured(1));
		} else if (auto stickerSetMatch = regex_match(qsl("^addstickers/([a-zA-Z0-9\\.\\_]+)(\\?|$)"), query, matchOptions)) {
			return qsl("tg://addstickers?set=") + url_encode(stickerSetMatch->captured(1));
		} else if (auto themeMatch = regex_match(qsl("^addtheme/([a-zA-Z0-9\\.\\_]+)(\\?|$)"), query, matchOptions)) {
			return qsl("tg://addtheme?slug=") + url_encode(themeMatch->captured(1));
		} else if (auto languageMatch = regex_match(qsl("^setlanguage/([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"), query, matchOptions)) {
			return qsl("tg://setlanguage?lang=") + url_encode(languageMatch->captured(1));
		} else if (auto shareUrlMatch = regex_match(qsl("^share/url/?\\?(.+)$"), query, matchOptions)) {
			return qsl("tg://msg_url?") + shareUrlMatch->captured(1);
		} else if (auto confirmPhoneMatch = regex_match(qsl("^confirmphone/?\\?(.+)"), query, matchOptions)) {
			return qsl("tg://confirmphone?") + confirmPhoneMatch->captured(1);
		} else if (auto ivMatch = regex_match(qsl("^iv/?\\?(.+)(#|$)"), query, matchOptions)) {
			//
			// We need to show our t.me page, not the url directly.
			//
			//auto params = url_parse_params(ivMatch->captured(1), UrlParamNameTransform::ToLower);
			//auto previewedUrl = params.value(qsl("url"));
			//if (previewedUrl.startsWith(qstr("http://"), Qt::CaseInsensitive)
			//	|| previewedUrl.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
			//	return previewedUrl;
			//}
			return url;
		} else if (auto socksMatch = regex_match(qsl("^socks/?\\?(.+)(#|$)"), query, matchOptions)) {
			return qsl("tg://socks?") + socksMatch->captured(1);
		} else if (auto proxyMatch = regex_match(qsl("^proxy/?\\?(.+)(#|$)"), query, matchOptions)) {
			return qsl("tg://proxy?") + proxyMatch->captured(1);
		} else if (auto bgMatch = regex_match(qsl("^bg/([a-zA-Z0-9\\.\\_\\-]+)(\\?(.+)?)?$"), query, matchOptions)) {
			const auto params = bgMatch->captured(3);
			return qsl("tg://bg?slug=") + bgMatch->captured(1) + (params.isEmpty() ? QString() : '&' + params);
		} else if (auto postMatch = regex_match(qsl("^c/(\\-?\\d+)/(\\d+)(#|$)"), query, matchOptions)) {
			return qsl("tg://privatepost?channel=%1&post=%2").arg(postMatch->captured(1)).arg(postMatch->captured(2));
		} else if (auto usernameMatch = regex_match(qsl("^([a-zA-Z0-9\\.\\_]+)(/?\\?|/?$|/(\\d+)/?(?:\\?|$))"), query, matchOptions)) {
			auto params = query.mid(usernameMatch->captured(0).size()).toString();
			auto postParam = QString();
			if (auto postMatch = regex_match(qsl("^/\\d+/?(?:\\?|$)"), usernameMatch->captured(2))) {
				postParam = qsl("&post=") + usernameMatch->captured(3);
			}
			return qsl("tg://resolve/?domain=") + url_encode(usernameMatch->captured(1)) + postParam + (params.isEmpty() ? QString() : '&' + params);
		}
	}
	return url;
}

bool InternalPassportLink(const QString &url) {
	const auto urlTrimmed = url.trimmed();
	if (!urlTrimmed.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		return false;
	}
	const auto command = urlTrimmed.midRef(qstr("tg://").size());

	using namespace qthelp;
	const auto matchOptions = RegExOption::CaseInsensitive;
	const auto authMatch = regex_match(
		qsl("^passport/?\\?(.+)(#|$)"),
		command,
		matchOptions);
	const auto usernameMatch = regex_match(
		qsl("^resolve/?\\?(.+)(#|$)"),
		command,
		matchOptions);
	const auto usernameValue = usernameMatch->hasMatch()
		? url_parse_params(
			usernameMatch->captured(1),
			UrlParamNameTransform::ToLower).value(qsl("domain"))
		: QString();
	const auto authLegacy = (usernameValue == qstr("telegrampassport"));
	return authMatch->hasMatch() || authLegacy;
}

bool StartUrlRequiresActivate(const QString &url) {
	return Core::App().locked()
		? true
		: !InternalPassportLink(url);
}

} // namespace Core
