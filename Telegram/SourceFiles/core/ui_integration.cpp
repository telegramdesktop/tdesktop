/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/ui_integration.h"

#include "api/api_text_entities.h"
#include "core/local_url_handlers.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/click_handler_types.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_session.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/basic_click_handlers.h"
#include "ui/emoji_config.h"
#include "lang/lang_keys.h"
#include "platform/platform_specific.h"
#include "boxes/url_auth_box.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_app_config.h"
#include "mainwindow.h"

namespace Core {
namespace {

const auto kGoodPrefix = u"https://"_q;
const auto kBadPrefix = u"http://"_q;

[[nodiscard]] QUrl UrlForAutoLogin(const QString &url) {
	return (url.startsWith(kGoodPrefix, Qt::CaseInsensitive)
		|| url.startsWith(kBadPrefix, Qt::CaseInsensitive))
		? QUrl(url)
		: QUrl();
}

[[nodiscard]] QString DomainForAutoLogin(const QUrl &url) {
	return url.isValid() ? url.host().toLower() : QString();
}

[[nodiscard]] QString UrlWithAutoLoginToken(
		const QString &url,
		QUrl parsed,
		const QString &domain) {
	const auto &config = Core::App().activeAccount().appConfig();
	const auto token = config.get<QString>("autologin_token", {});
	const auto domains = config.get<std::vector<QString>>(
		"autologin_domains",
		{});
	if (token.isEmpty()
		|| domain.isEmpty()
		|| !ranges::contains(domains, domain)) {
		return url;
	}
	const auto added = "autologin_token=" + token;
	parsed.setQuery(parsed.hasQuery()
		? (parsed.query() + '&' + added)
		: added);
	if (url.startsWith(kBadPrefix, Qt::CaseInsensitive)) {
		parsed.setScheme("https");
	}
	return QString::fromUtf8(parsed.toEncoded());
}

[[nodiscard]] bool BotAutoLogin(
		const QString &url,
		const QString &domain,
		QVariant context) {
	auto &account = Core::App().activeAccount();
	const auto &config = account.appConfig();
	const auto domains = config.get<std::vector<QString>>(
		"url_auth_domains",
		{});
	if (!account.sessionExists()
		|| domain.isEmpty()
		|| !ranges::contains(domains, domain)) {
		return false;
	}
	const auto good = url.startsWith(kBadPrefix, Qt::CaseInsensitive)
		? (kGoodPrefix + url.mid(kBadPrefix.size()))
		: url;
	UrlAuthBox::Activate(&account.session(), good, context);
	return true;
}

[[nodiscard]] QString OpenGLCheckFilePath() {
	return cWorkingDir() + "tdata/opengl_crash_check";
}

[[nodiscard]] QString ANGLEBackendFilePath() {
	return cWorkingDir() + "tdata/angle_backend";
}

} // namespace

void UiIntegration::postponeCall(FnMut<void()> &&callable) {
	Sandbox::Instance().postponeCall(std::move(callable));
}

void UiIntegration::registerLeaveSubscription(not_null<QWidget*> widget) {
	Core::App().registerLeaveSubscription(widget);
}

void UiIntegration::unregisterLeaveSubscription(not_null<QWidget*> widget) {
	Core::App().unregisterLeaveSubscription(widget);
}

QString UiIntegration::emojiCacheFolder() {
	return cWorkingDir() + "tdata/emoji";
}

QString UiIntegration::openglCheckFilePath() {
	return OpenGLCheckFilePath();
}

QString UiIntegration::angleBackendFilePath() {
	return ANGLEBackendFilePath();
}

void UiIntegration::textActionsUpdated() {
	if (const auto window = App::wnd()) {
		window->updateGlobalMenu();
	}
}

void UiIntegration::activationFromTopPanel() {
	Platform::IgnoreApplicationActivationRightNow();
}

bool UiIntegration::screenIsLocked() {
	return Core::App().screenIsLocked();
}

QString UiIntegration::timeFormat() {
	return cTimeFormat();
}

std::shared_ptr<ClickHandler> UiIntegration::createLinkHandler(
		const EntityLinkData &data,
		const std::any &context) {
	const auto my = std::any_cast<MarkedTextContext>(&context);
	switch (data.type) {
	case EntityType::Url:
		return (!data.data.isEmpty()
			&& UrlClickHandler::IsSuspicious(data.data))
			? std::make_shared<HiddenUrlClickHandler>(data.data)
			: Integration::createLinkHandler(data, context);

	case EntityType::CustomUrl:
		return !data.data.isEmpty()
			? std::make_shared<HiddenUrlClickHandler>(data.data)
			: Integration::createLinkHandler(data, context);

	case EntityType::BotCommand:
		return std::make_shared<BotCommandClickHandler>(data.data);

	case EntityType::Hashtag:
		using HashtagMentionType = MarkedTextContext::HashtagMentionType;
		if (my && my->type == HashtagMentionType::Twitter) {
			return std::make_shared<UrlClickHandler>(
				(qsl("https://twitter.com/hashtag/")
					+ data.data.mid(1)
					+ qsl("?src=hash")),
				true);
		} else if (my && my->type == HashtagMentionType::Instagram) {
			return std::make_shared<UrlClickHandler>(
				(qsl("https://instagram.com/explore/tags/")
					+ data.data.mid(1)
					+ '/'),
				true);
		}
		return std::make_shared<HashtagClickHandler>(data.data);

	case EntityType::Cashtag:
		return std::make_shared<CashtagClickHandler>(data.data);

	case EntityType::Mention:
		using HashtagMentionType = MarkedTextContext::HashtagMentionType;
		if (my && my->type == HashtagMentionType::Twitter) {
			return std::make_shared<UrlClickHandler>(
				qsl("https://twitter.com/") + data.data.mid(1),
				true);
		} else if (my && my->type == HashtagMentionType::Instagram) {
			return std::make_shared<UrlClickHandler>(
				qsl("https://instagram.com/") + data.data.mid(1) + '/',
				true);
		}
		return std::make_shared<MentionClickHandler>(data.data);

	case EntityType::MentionName: {
		auto fields = TextUtilities::MentionNameDataToFields(data.data);
		if (!my || !my->session) {
			LOG(("Mention name without a session: %1").arg(data.data));
		} else if (fields.userId) {
			return std::make_shared<MentionNameClickHandler>(
				my->session,
				data.text,
				fields.userId,
				fields.accessHash);
		} else {
			LOG(("Bad mention name: %1").arg(data.data));
		}
	} break;

	case EntityType::Code:
		return std::make_shared<MonospaceClickHandler>(data.text, data.type);
	case EntityType::Pre:
		return std::make_shared<MonospaceClickHandler>(data.text, data.type);
	}
	return Integration::createLinkHandler(data, context);
}

bool UiIntegration::handleUrlClick(
		const QString &url,
		const QVariant &context) {
	const auto local = Core::TryConvertUrlToLocal(url);
	if (Core::InternalPassportLink(local)) {
		return true;
	}

	if (UrlClickHandler::IsEmail(url)) {
		File::OpenEmailLink(url);
		return true;
	} else if (local.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		Core::App().openLocalUrl(local, context);
		return true;
	} else if (local.startsWith(qstr("internal:"), Qt::CaseInsensitive)) {
		Core::App().openInternalUrl(local, context);
		return true;
	}

	auto parsed = UrlForAutoLogin(url);
	const auto domain = DomainForAutoLogin(parsed);
	const auto skip = context.value<ClickHandlerContext>().skipBotAutoLogin;
	if (skip || !BotAutoLogin(url, domain, context)) {
		File::OpenUrl(UrlWithAutoLoginToken(url, std::move(parsed), domain));
	}
	return true;
}

std::unique_ptr<Ui::Text::CustomEmoji> UiIntegration::createCustomEmoji(
		const QString &data,
		const std::any &context) {
	const auto my = std::any_cast<MarkedTextContext>(&context);
	if (!my || !my->session) {
		return nullptr;
	}
	auto result = my->session->data().customEmojiManager().create(
		data,
		my->customEmojiRepaint);
	if (my->customEmojiLoopLimit > 0) {
		return std::make_unique<Ui::Text::LimitedLoopsEmoji>(
			std::move(result),
			my->customEmojiLoopLimit);
	}
	return result;
}

Fn<void()> UiIntegration::createSpoilerRepaint(const std::any &context) {
	const auto my = std::any_cast<MarkedTextContext>(&context);
	return my ? my->customEmojiRepaint : nullptr;
}

rpl::producer<> UiIntegration::forcePopupMenuHideRequests() {
	return Core::App().passcodeLockChanges() | rpl::to_empty;
}

const Ui::Emoji::One *UiIntegration::defaultEmojiVariant(
		const Ui::Emoji::One *emoji) {
	if (!emoji || !emoji->hasVariants()) {
		return emoji;
	}
	const auto nonColored = emoji->nonColoredId();
	const auto &variants = Core::App().settings().emojiVariants();
	const auto i = variants.find(nonColored);
	const auto result = (i != end(variants))
		? emoji->variant(i->second)
		: emoji;
	Core::App().settings().incrementRecentEmoji({ result });
	return result;
}

QString UiIntegration::phraseContextCopyText() {
	return tr::lng_context_copy_text(tr::now);
}

QString UiIntegration::phraseContextCopyEmail() {
	return tr::lng_context_copy_email(tr::now);
}

QString UiIntegration::phraseContextCopyLink() {
	return tr::lng_context_copy_link(tr::now);
}

QString UiIntegration::phraseContextCopySelected() {
	return tr::lng_context_copy_selected(tr::now);
}

QString UiIntegration::phraseFormattingTitle() {
	return tr::lng_menu_formatting(tr::now);
}

QString UiIntegration::phraseFormattingLinkCreate() {
	return tr::lng_menu_formatting_link_create(tr::now);
}

QString UiIntegration::phraseFormattingLinkEdit() {
	return tr::lng_menu_formatting_link_edit(tr::now);
}

QString UiIntegration::phraseFormattingClear() {
	return tr::lng_menu_formatting_clear(tr::now);
}

QString UiIntegration::phraseFormattingBold() {
	return tr::lng_menu_formatting_bold(tr::now);
}

QString UiIntegration::phraseFormattingItalic() {
	return tr::lng_menu_formatting_italic(tr::now);
}

QString UiIntegration::phraseFormattingUnderline() {
	return tr::lng_menu_formatting_underline(tr::now);
}

QString UiIntegration::phraseFormattingStrikeOut() {
	return tr::lng_menu_formatting_strike_out(tr::now);
}

QString UiIntegration::phraseFormattingMonospace() {
	return tr::lng_menu_formatting_monospace(tr::now);
}

QString UiIntegration::phraseFormattingSpoiler() {
	return tr::lng_menu_formatting_spoiler(tr::now);
}

QString UiIntegration::phraseButtonOk() {
	return tr::lng_box_ok(tr::now);
}

QString UiIntegration::phraseButtonClose() {
	return tr::lng_close(tr::now);
}

QString UiIntegration::phraseButtonCancel() {
	return tr::lng_cancel(tr::now);
}

QString UiIntegration::phrasePanelCloseWarning() {
	return tr::lng_bot_close_warning_title(tr::now);
}

QString UiIntegration::phrasePanelCloseUnsaved() {
	return tr::lng_bot_close_warning(tr::now);
}

QString UiIntegration::phrasePanelCloseAnyway() {
	return tr::lng_bot_close_warning_sure(tr::now);
}

#if 0 // disabled for now
QString UiIntegration::phraseBotSharePhone() {
	return tr::lng_bot_share_phone(tr::now);
}

QString UiIntegration::phraseBotSharePhoneTitle() {
	return tr::lng_settings_phone_label(tr::now);
}

QString UiIntegration::phraseBotSharePhoneConfirm() {
	return tr::lng_bot_share_phone_confirm(tr::now);
}
#endif

bool OpenGLLastCheckFailed() {
	return QFile::exists(OpenGLCheckFilePath());
}

} // namespace Core
