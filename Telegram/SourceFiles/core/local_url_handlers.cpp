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
#include "boxes/confirm_phone_box.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "boxes/connection_box.h"
#include "boxes/sticker_set_box.h"
#include "passport/passport_form_controller.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "messenger.h"
#include "auth_session.h"
#include "apiwrap.h"

namespace Core {
namespace {

using Match = qthelp::RegularExpressionMatch;

bool JoinGroupByHash(const Match &match, const QVariant &context) {
	if (!AuthSession::Exists()) {
		return false;
	}
	const auto hash = match->captured(1);
	Auth().api().checkChatInvite(hash, [=](const MTPChatInvite &result) {
		Messenger::Instance().hideMediaView();
		result.match([=](const MTPDchatInvite &data) {
			Ui::show(Box<ConfirmInviteBox>(data, [=] {
				Auth().api().importChatInvite(hash);
			}));
		}, [=](const MTPDchatInviteAlready &data) {
			if (const auto chat = App::feedChat(data.vchat)) {
				App::wnd()->controller()->showPeerHistory(
					chat,
					Window::SectionShow::Way::Forward);
			}
		});
	}, [=](const RPCError &error) {
		if (error.code() != 400) {
			return;
		}
		Messenger::Instance().hideMediaView();
		Ui::show(Box<InformBox>(lang(lng_group_invite_bad_link)));
	});
	return true;
}

bool ShowStickerSet(const Match &match, const QVariant &context) {
	if (!AuthSession::Exists()) {
		return false;
	}
	Messenger::Instance().hideMediaView();
	Ui::show(Box<StickerSetBox>(
		MTP_inputStickerSetShortName(MTP_string(match->captured(1)))));
	return true;
}

bool SetLanguage(const Match &match, const QVariant &context) {
	const auto languageId = match->captured(1);
	Lang::CurrentCloudManager().switchWithWarning(languageId);
	return true;
}

bool ShareUrl(const Match &match, const QVariant &context) {
	if (!AuthSession::Exists()) {
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

bool ConfirmPhone(const Match &match, const QVariant &context) {
	if (!AuthSession::Exists()) {
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

bool ShareGameScore(const Match &match, const QVariant &context) {
	if (!AuthSession::Exists()) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ShareGameScoreByHash(params.value(qsl("hash")));
	return true;
}

bool ApplySocksProxy(const Match &match, const QVariant &context) {
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ProxiesBoxController::ShowApplyConfirmation(ProxyData::Type::Socks5, params);
	return true;
}

bool ApplyMtprotoProxy(const Match &match, const QVariant &context) {
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
		if (const auto controller = window->controller()) {
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

bool ShowPassport(const Match &match, const QVariant &context) {
	return ShowPassportForm(url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower));
}

bool ResolveUsername(const Match &match, const QVariant &context) {
	if (!AuthSession::Exists()) {
		return false;
	}
	auto params = url_parse_params(
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
	auto post = (start == qsl("startgroup")) ? ShowAtProfileMsgId : ShowAtUnreadMsgId;
	auto postParam = params.value(qsl("post"));
	if (auto postId = postParam.toInt()) {
		post = postId;
	}
	auto gameParam = params.value(qsl("game"));
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

bool HandleUnknown(const Match &match, const QVariant &context) {
	if (!AuthSession::Exists()) {
		return false;
	}
	const auto request = match->captured(1);
	const auto callback = [=](const MTPDhelp_deepLinkInfo &result) {
		const auto text = TextWithEntities{
			qs(result.vmessage),
			(result.has_entities()
				? TextUtilities::EntitiesFromMTP(result.ventities.v)
				: EntitiesInText())
		};
		if (result.is_update_app()) {
			const auto box = std::make_shared<QPointer<BoxContent>>();
			const auto callback = [=] {
				Core::UpdateApplication();
				if (*box) (*box)->closeBox();
			};
			*box = Ui::show(Box<ConfirmBox>(
				text,
				lang(lng_menu_update),
				callback));
		} else {
			Ui::show(Box<InformBox>(text));
		}
	};
	Auth().api().requestDeepLinkInfo(request, callback);
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
			qsl("^resolve/?\\?(.+)(#|$)"),
			ResolveUsername
		},
		{
			qsl("^([^\\?]+)(\\?|#|$)"),
			HandleUnknown
		}
	};
	return Result;
}

} // namespace Core
