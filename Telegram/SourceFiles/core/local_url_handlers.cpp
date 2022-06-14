/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/local_url_handlers.h"

#include "api/api_authorizations.h"
#include "api/api_confirm_phone.h"
#include "api/api_text_entities.h"
#include "api/api_chat_invite.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "boxes/background_preview_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "boxes/connection_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/sessions_box.h"
#include "boxes/language_box.h"
#include "boxes/change_phone_box.h"
#include "passport/passport_form_controller.h"
#include "window/window_session_controller.h"
#include "ui/toast/toast.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_cloud_themes.h"
#include "data/data_channel.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_open_common.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/themes/window_theme_editor_box.h" // GenerateSlug.
#include "payments/payments_checkout_process.h"
#include "settings/settings_common.h"
#include "settings/settings_folders.h"
#include "settings/settings_main.h"
#include "settings/settings_privacy_security.h"
#include "settings/settings_chat.h"
#include "settings/settings_premium.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "inline_bots/bot_attach_web_view.h"
#include "history/history.h"
#include "base/qt/qt_common_adapters.h"
#include "apiwrap.h"

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
	controller->window().activate();
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
	controller->window().activate();
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
	controller->window().activate();
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
	if (match->capturedView(1).isEmpty()) {
		ShowLanguagesBox();
	} else {
		const auto languageId = match->captured(2);
		Lang::CurrentCloudManager().switchWithWarning(languageId);
	}
	if (controller) {
		controller->window().activate();
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
		controller->window().activate();
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
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto phone = params.value(qsl("phone"));
	const auto hash = params.value(qsl("hash"));
	if (phone.isEmpty() || hash.isEmpty()) {
		return false;
	}
	controller->session().api().confirmPhone().resolve(
		controller,
		phone,
		hash);
	controller->window().activate();
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
	ShareGameScoreByHash(controller, params.value(qsl("hash")));
	controller->window().activate();
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
	if (controller) {
		controller->window().activate();
	}
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
	if (controller) {
		controller->window().activate();
	}
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
	const auto bg = params.value("bg_color");
	const auto color = params.value("color");
	const auto gradient = params.value("gradient");
	const auto result = BackgroundPreviewBox::Start(
		controller,
		(!color.isEmpty()
			? color
			: !gradient.isEmpty()
			? gradient
			: params.value(qsl("slug"))),
		params);
	controller->window().activate();
	return result;
}

[[nodiscard]] ChatAdminRights ParseRequestedAdminRights(
		const QString &value) {
	auto result = ChatAdminRights();
	for (const auto &element : value.split(QRegularExpression("[+ ]"))) {
		if (element == u"change_info"_q) {
			result |= ChatAdminRight::ChangeInfo;
		} else if (element == u"post_messages"_q) {
			result |= ChatAdminRight::PostMessages;
		} else if (element == u"edit_messages"_q) {
			result |= ChatAdminRight::EditMessages;
		} else if (element == u"delete_messages"_q) {
			result |= ChatAdminRight::DeleteMessages;
		} else if (element == u"restrict_members"_q) {
			result |= ChatAdminRight::BanUsers;
		} else if (element == u"invite_users"_q) {
			result |= ChatAdminRight::InviteUsers;
		} else if (element == u"pin_messages"_q) {
			result |= ChatAdminRight::PinMessages;
		} else if (element == u"promote_members"_q) {
			result |= ChatAdminRight::AddAdmins;
		} else if (element == u"manage_video_chats"_q) {
			result |= ChatAdminRight::ManageCall;
		} else if (element == u"anonymous"_q) {
			result |= ChatAdminRight::Anonymous;
		} else if (element == u"manage_chat"_q) {
			result |= ChatAdminRight::Other;
		} else {
			return {};
		}
	}
	return result;
}

bool ResolveUsernameOrPhone(
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
	const auto phone = params.value(qsl("phone"));
	const auto validDomain = [](const QString &domain) {
		return qthelp::regex_match(
			qsl("^[a-zA-Z0-9\\.\\_]+$"),
			domain,
			{}
		).valid();
	};
	const auto validPhone = [](const QString &phone) {
		return qthelp::regex_match(qsl("^[0-9]+$"), phone, {}).valid();
	};
	if (domain == qsl("telegrampassport")) {
		return ShowPassportForm(controller, params);
	} else if (!validDomain(domain) && !validPhone(phone)) {
		return false;
	}
	using ResolveType = Window::ResolveType;
	auto resolveType = ResolveType::Default;
	auto startToken = params.value(u"start"_q);
	if (!startToken.isEmpty()) {
		resolveType = ResolveType::BotStart;
	} else if (params.contains(u"startgroup"_q)) {
		resolveType = ResolveType::AddToGroup;
		startToken = params.value(u"startgroup"_q);
	} else if (params.contains(u"startchannel"_q)) {
		resolveType = ResolveType::AddToChannel;
	}
	auto post = ShowAtUnreadMsgId;
	auto adminRights = ChatAdminRights();
	if (resolveType == ResolveType::AddToGroup
		|| resolveType == ResolveType::AddToChannel) {
		adminRights = ParseRequestedAdminRights(params.value(u"admin"_q));
	}
	const auto postParam = params.value(qsl("post"));
	if (const auto postId = postParam.toInt()) {
		post = postId;
	}
	const auto commentParam = params.value(qsl("comment"));
	const auto commentId = commentParam.toInt();
	const auto threadParam = params.value(qsl("thread"));
	const auto threadId = threadParam.toInt();
	const auto gameParam = params.value(qsl("game"));
	if (!gameParam.isEmpty() && validDomain(gameParam)) {
		startToken = gameParam;
		resolveType = ResolveType::ShareGame;
	}
	const auto myContext = context.value<ClickHandlerContext>();
	using Navigation = Window::SessionNavigation;
	controller->showPeerByLink(Navigation::PeerByLinkInfo{
		.usernameOrId = domain,
		.phone = phone,
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
		.resolveType = resolveType,
		.startToken = startToken,
		.startAdminRights = adminRights,
		.startAutoSubmit = myContext.botStartAutoSubmit,
		.attachBotUsername = params.value(u"attach"_q),
		.attachBotToggleCommand = (params.contains(u"startattach"_q)
			? params.value(u"startattach"_q)
			: std::optional<QString>()),
		.attachBotChooseTypes = InlineBots::ParseChooseTypes(
			params.value(u"choose"_q)),
		.voicechatHash = (params.contains(u"livestream"_q)
			? std::make_optional(params.value(u"livestream"_q))
			: params.contains(u"videochat"_q)
			? std::make_optional(params.value(u"videochat"_q))
			: params.contains(u"voicechat"_q)
			? std::make_optional(params.value(u"voicechat"_q))
			: std::nullopt),
		.clickFromMessageId = myContext.itemId,
	});
	controller->window().activate();
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
	controller->window().activate();
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

	const auto type = [&]() -> std::optional<::Settings::Type> {
		if (section == qstr("language")) {
			ShowLanguagesBox();
			return {};
		} else if (section == qstr("devices")) {
			controller->session().api().authorizations().reload();
			return ::Settings::Sessions::Id();
		} else if (section == qstr("folders")) {
			return ::Settings::Folders::Id();
		} else if (section == qstr("privacy")) {
			return ::Settings::PrivacySecurity::Id();
		} else if (section == qstr("themes")) {
			return ::Settings::Chat::Id();
		} else if (section == qstr("change_number")) {
			return ::Settings::ChangePhone::Id();
		}
		return ::Settings::Main::Id();
	}();

	if (type.has_value()) {
		controller->showSettings(*type);
		controller->window().activate();
	}
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
			controller->show(Ui::MakeConfirmBox({
				.text = text,
				.confirmed = callback,
				.confirmText = tr::lng_menu_update(),
			}));
		} else {
			controller->show(Ui::MakeInformBox(text));
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
			PeerId(parts.value(1).toULongLong()),
			MsgId(parts.value(2).toLongLong()));
		const auto session = &controller->session();
		const auto document = session->data().document(documentId);
		const auto timeMs = time * crl::time(1000);
		if (document->isVideoFile()) {
			controller->window().openInMediaView(Media::View::OpenRequest(
				controller,
				document,
				session->data().message(itemId),
				false,
				timeMs));
		} else if (document->isSong() || document->isVoiceMessage()) {
			session->settings().setMediaLastPlaybackPosition(
				documentId,
				timeMs);
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
	Ui::Toast::Show(
		Window::Show(controller).toastParent(),
		tr::lng_group_invite_copied(tr::now));
	return true;
}

bool OpenExternalLink(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	return Ui::Integration::Instance().handleUrlClick(
		match->captured(1),
		context);
}

void ExportTestChatTheme(
		not_null<Window::SessionController*> controller,
		not_null<const Data::CloudTheme*> theme) {
	const auto session = &controller->session();
	const auto show = std::make_shared<Window::Show>(controller);
	const auto inputSettings = [&](Data::CloudThemeType type)
	-> std::optional<MTPInputThemeSettings> {
		const auto i = theme->settings.find(type);
		if (i == end(theme->settings)) {
			Ui::Toast::Show(show->toastParent(), "Something went wrong :(");
			return std::nullopt;
		}
		const auto &fields = i->second;
		if (!fields.paper
			|| !fields.paper->isPattern()
			|| fields.paper->backgroundColors().empty()
			|| !fields.paper->hasShareUrl()) {
			Ui::Toast::Show(show->toastParent(), "Something went wrong :(");
			return std::nullopt;
		}
		const auto &bg = fields.paper->backgroundColors();
		const auto url = fields.paper->shareUrl(session);
		const auto from = url.indexOf("bg/");
		const auto till = url.indexOf("?");
		if (from < 0 || till <= from) {
			Ui::Toast::Show(
				show->toastParent(),
				"Bad WallPaper link: " + url);
			return std::nullopt;
		}

		using Setting = MTPDinputThemeSettings::Flag;
		using Paper = MTPDwallPaperSettings::Flag;
		const auto color = [](const QColor &color) {
			const auto red = color.red();
			const auto green = color.green();
			const auto blue = color.blue();
			return int(((uint32(red) & 0xFFU) << 16)
				| ((uint32(green) & 0xFFU) << 8)
				| (uint32(blue) & 0xFFU));
		};
		const auto colors = [&](const std::vector<QColor> &colors) {
			auto result = QVector<MTPint>();
			result.reserve(colors.size());
			for (const auto &single : colors) {
				result.push_back(MTP_int(color(single)));
			}
			return result;
		};
		const auto slug = url.mid(from + 3, till - from - 3);
		const auto settings = Setting::f_wallpaper
			| Setting::f_wallpaper_settings
			| (fields.outgoingAccentColor
				? Setting::f_outbox_accent_color
				: Setting(0))
			| (!fields.outgoingMessagesColors.empty()
				? Setting::f_message_colors
				: Setting(0));
		const auto papers = Paper::f_background_color
			| Paper::f_intensity
			| (bg.size() > 1
				? Paper::f_second_background_color
				: Paper(0))
			| (bg.size() > 2
				? Paper::f_third_background_color
				: Paper(0))
			| (bg.size() > 3
				? Paper::f_fourth_background_color
				: Paper(0));
		return MTP_inputThemeSettings(
			MTP_flags(settings),
			((type == Data::CloudThemeType::Dark)
				? MTP_baseThemeTinted()
				: MTP_baseThemeClassic()),
			MTP_int(color(fields.accentColor)),
			MTP_int(color(fields.outgoingAccentColor.value_or(
				Qt::black))),
			MTP_vector<MTPint>(colors(fields.outgoingMessagesColors)),
			MTP_inputWallPaperSlug(MTP_string(slug)),
			MTP_wallPaperSettings(
				MTP_flags(papers),
				MTP_int(color(bg[0])),
				MTP_int(color(bg.size() > 1 ? bg[1] : Qt::black)),
				MTP_int(color(bg.size() > 2 ? bg[2] : Qt::black)),
				MTP_int(color(bg.size() > 3 ? bg[3] : Qt::black)),
				MTP_int(fields.paper->patternIntensity()),
				MTP_int(0)));
	};
	const auto light = inputSettings(Data::CloudThemeType::Light);
	if (!light) {
		return;
	}
	const auto dark = inputSettings(Data::CloudThemeType::Dark);
	if (!dark) {
		return;
	}
	session->api().request(MTPaccount_CreateTheme(
		MTP_flags(MTPaccount_CreateTheme::Flag::f_settings),
		MTP_string(Window::Theme::GenerateSlug()),
		MTP_string(theme->title + " Desktop"),
		MTPInputDocument(),
		MTP_vector<MTPInputThemeSettings>(QVector<MTPInputThemeSettings>{
			*light,
			*dark,
		})
	)).done([=](const MTPTheme &result) {
		const auto slug = Data::CloudTheme::Parse(session, result, true).slug;
		QGuiApplication::clipboard()->setText(
			session->createInternalLinkFull("addtheme/" + slug));
		if (show->valid()) {
			Ui::Toast::Show(
				show->toastParent(),
				tr::lng_background_link_copied(tr::now));
		}
	}).fail([=](const MTP::Error &error) {
		if (show->valid()) {
			Ui::Toast::Show(show->toastParent(), "Error: " + error.type());
		}
	}).send();
}

bool ResolveTestChatTheme(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	if (const auto history = controller->activeChatCurrent().history()) {
		controller->clearCachedChatThemes();
		const auto theme = history->owner().cloudThemes().updateThemeFromLink(
			history->peer->themeEmoji(),
			params);
		if (theme) {
			if (!params["export"].isEmpty()) {
				ExportTestChatTheme(controller, &*theme);
			}
			const auto recache = [&](Data::CloudThemeType type) {
				[[maybe_unused]] auto value = theme->settings.contains(type)
					? controller->cachedChatThemeValue(*theme, type)
					: nullptr;
			};
			recache(Data::CloudThemeType::Dark);
			recache(Data::CloudThemeType::Light);
		}
	}
	return true;
}

bool ResolveInvoice(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto slug = params.value(qsl("slug"));
	if (slug.isEmpty()) {
		return false;
	}
	const auto window = &controller->window();
	Payments::CheckoutProcess::Start(
		&controller->session(),
		slug,
		crl::guard(window, [=](auto) { window->activate(); }));
	return true;
}

bool ResolvePremiumOffer(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1).mid(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto refAddition = params.value(qsl("ref"));
	const auto ref = "deeplink"
		+ (refAddition.isEmpty() ? QString() : '_' + refAddition);
	::Settings::ShowPremium(controller, ref);
	controller->window().activate();
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
			ResolveUsernameOrPhone
		},
		{
			qsl("^privatepost/?\\?(.+)(#|$)"),
			ResolvePrivatePost
		},
		{
			qsl("^settings(/language|/devices|/folders|/privacy|/themes|/change_number)?$"),
			ResolveSettings
		},
		{
			qsl("^test_chat_theme/?\\?(.+)(#|$)"),
			ResolveTestChatTheme,
		},
		{
			qsl("invoice/?\\?(.+)(#|$)"),
			ResolveInvoice,
		},
		{
			qsl("premium_offer/?(\\?.+)?(#|$)"),
			ResolvePremiumOffer,
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
		{
			qsl("^url:(.+)$"),
			OpenExternalLink
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
		auto query = telegramMeMatch->capturedView(5);
		if (auto phoneMatch = regex_match(qsl("^\\+([0-9]+)(\\?|$)"), query, matchOptions)) {
			auto params = query.mid(phoneMatch->captured(0).size()).toString();
			return qsl("tg://resolve?phone=") + phoneMatch->captured(1) + (params.isEmpty() ? QString() : '&' + params);
		} else if (auto joinChatMatch = regex_match(qsl("^(joinchat/|\\+|\\%20)([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"), query, matchOptions)) {
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
		} else if (auto invoiceMatch = regex_match(qsl("^(invoice/|\\$)([a-zA-Z0-9_\\-]+)(\\?|#|$)"), query, matchOptions)) {
			return qsl("tg://invoice?slug=") + invoiceMatch->captured(2);
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
			return qsl("tg://resolve?domain=") + url_encode(usernameMatch->captured(1)) + postParam + (params.isEmpty() ? QString() : '&' + params);
		}
	}
	return url;
}

bool InternalPassportLink(const QString &url) {
	const auto urlTrimmed = url.trimmed();
	if (!urlTrimmed.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		return false;
	}
	const auto command = base::StringViewMid(urlTrimmed, qstr("tg://").size());

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
