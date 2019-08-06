/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/click_handler_types.h"

#include "lang/lang_keys.h"
#include "core/application.h"
#include "core/local_url_handlers.h"
#include "core/file_utilities.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "platform/platform_specific.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "boxes/confirm_box.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "storage/localstorage.h"
#include "ui/widgets/tooltip.h"
#include "data/data_user.h"
#include "data/data_session.h"

namespace {

bool UrlRequiresConfirmation(const QUrl &url) {
	using namespace qthelp;
	return !regex_match(qsl("(^|\\.)(telegram\\.org|telegra\\.ph|telesco\\.pe)$"), url.host(), RegExOption::CaseInsensitive);
}

} // namespace

UrlClickHandler::UrlClickHandler(const QString &url, bool fullDisplayed)
: TextClickHandler(fullDisplayed)
, _originalUrl(url) {
	if (isEmail()) {
		_readable = _originalUrl;
	} else {
		const auto original = QUrl(_originalUrl);
		const auto good = QUrl(original.isValid()
			? original.toEncoded()
			: QString());
		_readable = good.isValid() ? good.toDisplayString() : _originalUrl;
	}
}

QString UrlClickHandler::copyToClipboardContextItemText() const {
	return isEmail()
		? tr::lng_context_copy_email(tr::now)
		: tr::lng_context_copy_link(tr::now);
}

QString UrlClickHandler::url() const {
	if (isEmail()) {
		return _originalUrl;
	}

	QUrl u(_originalUrl), good(u.isValid() ? u.toEncoded() : QString());
	QString result(good.isValid() ? QString::fromUtf8(good.toEncoded()) : _originalUrl);

	if (!result.isEmpty() && !QRegularExpression(qsl("^[a-zA-Z]+:")).match(result).hasMatch()) { // no protocol
		return qsl("http://") + result;
	}
	return result;
}

void UrlClickHandler::Open(QString url, QVariant context) {
	url = Core::TryConvertUrlToLocal(url);
	if (Core::InternalPassportLink(url)) {
		return;
	}

	Ui::Tooltip::Hide();
	if (isEmail(url)) {
		File::OpenEmailLink(url);
	} else if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		Core::App().openLocalUrl(url, context);
	} else if (!url.isEmpty()) {
		QDesktopServices::openUrl(url);
	}
}

auto UrlClickHandler::getTextEntity() const -> TextEntity {
	const auto type = isEmail(_originalUrl)
		? EntityType::Email
		: EntityType::Url;
	return { type, _originalUrl };
}

void HiddenUrlClickHandler::Open(QString url, QVariant context) {
	url = Core::TryConvertUrlToLocal(url);
	if (Core::InternalPassportLink(url)) {
		return;
	}

	const auto open = [=] {
		UrlClickHandler::Open(url, context);
	};
	if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		open();
	} else {
		const auto parsedUrl = QUrl::fromUserInput(url);
		if (UrlRequiresConfirmation(url)
			&& QGuiApplication::keyboardModifiers() != Qt::ControlModifier) {
			Core::App().hideMediaView();
			const auto displayUrl = parsedUrl.isValid()
				? parsedUrl.toDisplayString()
				: url;
			Ui::show(
				Box<ConfirmBox>(
					tr::lng_open_this_link(tr::now) + qsl("\n\n") + displayUrl,
					tr::lng_open_link(tr::now),
					[=] { Ui::hideLayer(); open(); }),
				LayerOption::KeepOther);
		} else {
			open();
		}
	}
}

void BotGameUrlClickHandler::onClick(ClickContext context) const {
	const auto url = Core::TryConvertUrlToLocal(this->url());
	if (Core::InternalPassportLink(url)) {
		return;
	}

	const auto open = [=] {
		UrlClickHandler::Open(url, context.other);
	};
	if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		open();
	} else if (!_bot || _bot->isVerified() || Local::isBotTrusted(_bot)) {
		open();
	} else {
		const auto callback = [=, bot = _bot] {
			Ui::hideLayer();
			Local::makeBotTrusted(bot);
			open();
		};
		Ui::show(Box<ConfirmBox>(
			tr::lng_allow_bot_pass(tr::now, lt_bot_name, _bot->name),
			tr::lng_allow_bot(tr::now),
			callback));
	}
}

auto HiddenUrlClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::CustomUrl, url() };
}

QString MentionClickHandler::copyToClipboardContextItemText() const {
	return tr::lng_context_copy_mention(tr::now);
}

void MentionClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		App::main()->openPeerByName(_tag.mid(1), ShowAtProfileMsgId);
	}
}

auto MentionClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Mention };
}

void MentionNameClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		if (auto user = Auth().data().userLoaded(_userId)) {
			Ui::showPeerProfile(user);
		}
	}
}

auto MentionNameClickHandler::getTextEntity() const -> TextEntity {
	auto data = QString::number(_userId) + '.' + QString::number(_accessHash);
	return { EntityType::MentionName, data };
}

QString MentionNameClickHandler::tooltip() const {
	if (auto user = Auth().data().userLoaded(_userId)) {
		auto name = App::peerName(user);
		if (name != _text) {
			return name;
		}
	}
	return QString();
}

QString HashtagClickHandler::copyToClipboardContextItemText() const {
	return tr::lng_context_copy_hashtag(tr::now);
}

void HashtagClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		App::searchByHashtag(_tag, Ui::getPeerForMouseAction());
	}
}

auto HashtagClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Hashtag };
}

QString CashtagClickHandler::copyToClipboardContextItemText() const {
	return tr::lng_context_copy_hashtag(tr::now);
}

void CashtagClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		App::searchByHashtag(_tag, Ui::getPeerForMouseAction());
	}
}

auto CashtagClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Cashtag };
}

PeerData *BotCommandClickHandler::_peer = nullptr;
UserData *BotCommandClickHandler::_bot = nullptr;
void BotCommandClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		if (auto peer = peerForCommand()) {
			if (auto bot = peer->isUser() ? peer->asUser() : botForCommand()) {
				Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
				App::sendBotCommand(peer, bot, _cmd);
				return;
			}
		}

		if (auto peer = Ui::getPeerForMouseAction()) { // old way
			auto bot = peer->isUser() ? peer->asUser() : nullptr;
			if (!bot) {
				if (const auto view = App::hoveredLinkItem()) {
					// may return nullptr
					bot = view->data()->fromOriginal()->asUser();
				}
			}
			Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
			App::sendBotCommand(peer, bot, _cmd);
		} else {
			App::insertBotCommand(_cmd);
		}
	}
}

auto BotCommandClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::BotCommand };
}
