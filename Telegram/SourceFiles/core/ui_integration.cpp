/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/ui_integration.h"

#include "core/local_url_handlers.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/click_handler_types.h"
#include "ui/basic_click_handlers.h"
#include "ui/emoji_config.h"
#include "lang/lang_keys.h"
#include "platform/platform_specific.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_app_config.h"
#include "mainwindow.h"

namespace Core {
namespace {

QString UrlWithAutoLoginToken(const QString &url) {
	const auto &config = Core::App().activeAccount().appConfig();
	const auto token = config.get<QString>("autologin_token", {});
	const auto domains = config.get<std::vector<QString>>(
		"autologin_domains",
		{});
	if (domains.empty()
		|| token.isEmpty()
		|| !url.startsWith("https://", Qt::CaseInsensitive)) {
		return url;
	}
	auto parsed = QUrl(url);
	if (!parsed.isValid()) {
		return url;
	} else if (!ranges::contains(domains, parsed.host().toLower())) {
		return url;
	}
	const auto added = "autologin_token=" + token;
	parsed.setQuery(parsed.hasQuery()
		? (parsed.query() + '&' + added)
		: added);
	return QString::fromUtf8(parsed.toEncoded());
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

void UiIntegration::writeLogEntry(const QString &entry) {
	Logs::writeMain(entry);
}

QString UiIntegration::emojiCacheFolder() {
	return cWorkingDir() + "tdata/emoji";
}

void UiIntegration::textActionsUpdated() {
	if (const auto window = App::wnd()) {
		window->updateGlobalMenu();
	}
}

void UiIntegration::activationFromTopPanel() {
	Platform::IgnoreApplicationActivationRightNow();
}

void UiIntegration::startFontsBegin() {
}

void UiIntegration::startFontsEnd() {
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

	File::OpenUrl(UrlWithAutoLoginToken(url));
	return true;

}

rpl::producer<> UiIntegration::forcePopupMenuHideRequests() {
	return Core::App().passcodeLockChanges() | rpl::to_empty;
}

QString UiIntegration::convertTagToMimeTag(const QString &tagId) {
	if (TextUtilities::IsMentionLink(tagId)) {
		if (const auto session = Core::App().activeAccount().maybeSession()) {
			return tagId + ':' + QString::number(session->userId());
		}
	}
	return tagId;
}

const Ui::Emoji::One *UiIntegration::defaultEmojiVariant(
		const Ui::Emoji::One *emoji) {
	if (!emoji || !emoji->hasVariants()) {
		return emoji;
	}
	const auto nonColored = emoji->nonColoredId();
	const auto it = cEmojiVariants().constFind(nonColored);
	const auto result = (it != cEmojiVariants().cend())
		? emoji->variant(it.value())
		: emoji;
	AddRecentEmoji(result);
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

} // namespace Core
