/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/local_url_handlers.h"

#include "api/api_authorizations.h"
#include "api/api_text_entities.h"
#include "api/api_chat_invite.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "boxes/confirm_phone_box.h"
#include "boxes/background_preview_box.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "boxes/connection_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/sessions_box.h"
#include "boxes/language_box.h"
#include "passport/passport_form_controller.h"
#include "window/window_session_controller.h"
#include "ui/toast/toast.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_cloud_themes.h"
#include "data/data_channel.h"
#include "media/player/media_player_instance.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "settings/settings_common.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "app.h"

#include <QtGui/QGuiApplication>

namespace Core {
namespace {

using Match = qthelp::RegularExpressionMatch;

bool JoinGroupByHash(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	Api::CheckChatInvite(controller, match->captured(1));
	return true;
}

bool ShowStickerSet(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	Core::App().hideMediaView();
	controller->show(Box<StickerSetBox>(
		controller,
		StickerSetIdentifier{ .shortName = match->captured(1) }));
	return true;
}

bool ShowTheme(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto fromMessageId = context.value<ClickHandlerContext>().itemId;
	Core::App().hideMediaView();
	controller->session().data().cloudThemes().resolve(
		&controller->window(),
		match->captured(1),
		fromMessageId);
	return true;
}

void ShowLanguagesBox() {
	static auto Guard = base::binary_guard();
	Guard = LanguageBox::Show();
}

bool SetLanguage(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (match->capturedRef(1).isEmpty()) {
		ShowLanguagesBox();
	} else {
		const auto languageId = match->captured(2);
		Lang::CurrentCloudManager().switchWithWarning(languageId);
	}
	return true;
}

bool ShareUrl(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	auto url = params.value(qsl("url"));
	if (url.isEmpty()) {
		return false;
	} else {
		controller->content()->shareUrlLayer(url, params.value("text"));
		return true;
	}
	return false;
}

bool ConfirmPhone(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
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
	ConfirmPhoneBox::Start(&controller->session(), phone, hash);
	return true;
}

bool ShareGameScore(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ShareGameScoreByHash(&controller->session(), params.value(qsl("hash")));
	return true;
}

bool ApplySocksProxy(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ProxiesBoxController::ShowApplyConfirmation(
		MTP::ProxyData::Type::Socks5,
		params);
	return true;
}

bool ApplyMtprotoProxy(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ProxiesBoxController::ShowApplyConfirmation(
		MTP::ProxyData::Type::Mtproto,
		params);
	return true;
}

bool ShowPassportForm(
		Window::SessionController *controller,
		const QMap<QString, QString> &params) {
	if (!controller) {
		return false;
	}
	const auto botId = params.value("bot_id", QString()).toULongLong();
	const auto scope = params.value("scope", QString());
	const auto callback = params.value("callback_url", QString());
	const auto publicKey = params.value("public_key", QString());
	const auto nonce = params.value(
		Passport::NonceNameByScope(scope),
		QString());
	const auto errors = params.value("errors", QString());
	controller->showPassportForm(Passport::FormRequest(
		botId,
		scope,
		callback,
		publicKey,
		nonce,
		errors));
	return true;
}

bool ShowPassport(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	return ShowPassportForm(
		controller,
		url_parse_params(
			match->captured(1),
			qthelp::UrlParamNameTransform::ToLower));
}

bool ShowWallPaper(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	if (!params.value("gradient").isEmpty()) {
		Ui::show(Box<InformBox>(
			tr::lng_background_gradient_unsupported(tr::now)));
		return false;
	}
	const auto color = params.value("color");
	return BackgroundPreviewBox::Start(
		controller,
		(color.isEmpty() ? params.value(qsl("slug")) : color),
		params);
}

bool ResolveUsername(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
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
		return ShowPassportForm(controller, params);
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
	const auto postParam = params.value(qsl("post"));
	if (const auto postId = postParam.toInt()) {
		post = postId;
	}
	const auto commentParam = params.value(qsl("comment"));
	const auto commentId = commentParam.toInt();
	const auto threadParam = params.value(qsl("thread"));
	const auto threadId = threadParam.toInt();
	const auto gameParam = params.value(qsl("game"));
	if (!gameParam.isEmpty() && valid(gameParam)) {
		startToken = gameParam;
		post = ShowAtGameShareMsgId;
	}
	const auto fromMessageId = context.value<ClickHandlerContext>().itemId;
	using Navigation = Window::SessionNavigation;
	controller->showPeerByLink(Navigation::PeerByLinkInfo{
		.usernameOrId = domain,
		.messageId = post,
		.repliesInfo = commentId
			? Navigation::RepliesByLinkInfo{
				Navigation::CommentId{ commentId }
			}
			: threadId
			? Navigation::RepliesByLinkInfo{
				Navigation::ThreadId{ threadId }
			}
			: Navigation::RepliesByLinkInfo{ v::null },
		.startToken = startToken,
		.voicechatHash = (params.contains(u"voicechat"_q)
			? std::make_optional(params.value(u"voicechat"_q))
			: std::nullopt),
		.clickFromMessageId = fromMessageId,
	});
	return true;
}

bool ResolvePrivatePost(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto channelId = ChannelId(
		params.value(qsl("channel")).toULongLong());
	const auto msgId = params.value(qsl("post")).toInt();
	const auto commentParam = params.value(qsl("comment"));
	const auto commentId = commentParam.toInt();
	const auto threadParam = params.value(qsl("thread"));
	const auto threadId = threadParam.toInt();
	if (!channelId || !IsServerMsgId(msgId)) {
		return false;
	}
	const auto fromMessageId = context.value<ClickHandlerContext>().itemId;
	using Navigation = Window::SessionNavigation;
	controller->showPeerByLink(Navigation::PeerByLinkInfo{
		.usernameOrId = channelId,
		.messageId = msgId,
		.repliesInfo = commentId
			? Navigation::RepliesByLinkInfo{
				Navigation::CommentId{ commentId }
			}
			: threadId
			? Navigation::RepliesByLinkInfo{
				Navigation::ThreadId{ threadId }
			}
			: Navigation::RepliesByLinkInfo{ v::null },
		.clickFromMessageId = fromMessageId,
	});
	return true;
}

bool ResolveSettings(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	controller->window().activate();
	const auto section = match->captured(1).mid(1).toLower();
	if (section.isEmpty()) {
		controller->window().showSettings();
		return true;
	}
	if (section == qstr("devices")) {
		controller->session().api().authorizations().reload();
		controller->show(Box<SessionsBox>(&controller->session()));
		return true;
	} else if (section == qstr("language")) {
		ShowLanguagesBox();
		return true;
	}
	const auto type = (section == qstr("folders"))
		? ::Settings::Type::Folders
		: ::Settings::Type::Main;
	controller->showSettings(type);
	return true;
}

bool HandleUnknown(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto request = match->captured(1);
	const auto callback = crl::guard(controller, [=](const MTPDhelp_deepLinkInfo &result) {
		const auto text = TextWithEntities{
			qs(result.vmessage()),
			Api::EntitiesFromMTP(
				&controller->session(),
				result.ventities().value_or_empty())
		};
		if (result.is_update_app()) {
			const auto callback = [=](Fn<void()> &&close) {
				Core::UpdateApplication();
				close();
			};
			controller->show(Box<ConfirmBox>(
				text,
				tr::lng_menu_update(tr::now),
				callback));
		} else {
			controller->show(Box<InformBox>(text));
		}
	});
	controller->session().api().requestDeepLinkInfo(request, callback);
	return true;
}

bool OpenMediaTimestamp(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto time = match->captured(2).toInt();
	if (time < 0) {
		return false;
	}
	const auto base = match->captured(1);
	if (base.startsWith(qstr("doc"))) {
		const auto parts = base.mid(3).split('_');
		const auto documentId = parts.value(0).toULongLong();
		const auto itemId = FullMsgId(
			parts.value(1).toInt(),
			parts.value(2).toInt());
		const auto session = &controller->session();
		const auto document = session->data().document(documentId);
		session->settings().setMediaLastPlaybackPosition(
			documentId,
			time * crl::time(1000));
		if (document->isVideoFile()) {
			controller->openDocument(document, itemId, true);
		} else if (document->isSong() || document->isVoiceMessage()) {
			Media::Player::instance()->play({ document, itemId });
		}
		return true;
	}
	return false;
}

bool ShowInviteLink(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto base64link = match->captured(1).toLatin1();
	const auto link = QString::fromUtf8(QByteArray::fromBase64(base64link));
	if (link.isEmpty()) {
		return false;
	}
	QGuiApplication::clipboard()->setText(link);
	Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
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
			qsl("^setlanguage/?(\\?lang=([a-zA-Z0-9\\.\\_\\-]+))?(&|$)"),
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
			qsl("^settings(/folders|/devices|/language)?$"),
			ResolveSettings
		},
		{
			qsl("^([^\\?]+)(\\?|#|$)"),
			HandleUnknown
		},
	};
	return Result;
}

const std::vector<LocalUrlHandler> &InternalUrlHandlers() {
	static auto Result = std::vector<LocalUrlHandler>{
		{
			qsl("^media_timestamp/?\\?base=([a-zA-Z0-9\\.\\_\\-]+)&t=(\\d+)(&|$)"),
			OpenMediaTimestamp
		},
		{
			qsl("^show_invite_link/?\\?link=([a-zA-Z0-9_\\+\\/\\=\\-]+)(&|$)"),
			ShowInviteLink
		},
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
		if (auto joinChatMatch = regex_match(qsl("^(joinchat/|\\+|\\%20)([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"), query, matchOptions)) {
			return qsl("tg://join?invite=") + url_encode(joinChatMatch->captured(2));
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
		} else if (auto bgMatch = regex_match(qsl("^bg/([a-zA-Z0-9\\.\\_\\-\\~]+)(\\?(.+)?)?$"), query, matchOptions)) {
			const auto params = bgMatch->captured(3);
			const auto bg = bgMatch->captured(1);
			const auto type = regex_match(qsl("^[a-fA-F0-9]{6}^"), bg)
				? "color"
				: (regex_match(qsl("^[a-fA-F0-9]{6}\\-[a-fA-F0-9]{6}$"), bg)
					|| regex_match(qsl("^[a-fA-F0-9]{6}(\\~[a-fA-F0-9]{6}){1,3}$"), bg))
				? "gradient"
				: "slug";
			return qsl("tg://bg?") + type + '=' + bg + (params.isEmpty() ? QString() : '&' + params);
		} else if (auto postMatch = regex_match(qsl("^c/(\\-?\\d+)/(\\d+)(/?\\?|/?$)"), query, matchOptions)) {
			auto params = query.mid(postMatch->captured(0).size()).toString();
			return qsl("tg://privatepost?channel=%1&post=%2").arg(postMatch->captured(1), postMatch->captured(2)) + (params.isEmpty() ? QString() : '&' + params);
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
	return Core::App().passcodeLocked()
		? true
		: !InternalPassportLink(url);
}

} // namespace Core
