/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "core/click_handler_types.h"

#include "lang/lang_keys.h"
#include "messenger.h"
#include "platform/platform_specific.h"
#include "boxes/confirm_box.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "storage/localstorage.h"
#include "ui/widgets/tooltip.h"
#include "core/file_utilities.h"

namespace {

QString tryConvertUrlToLocal(QString url) {
	if (url.size() > 8192) url = url.mid(0, 8192);

	using namespace qthelp;
	auto matchOptions = RegExOption::CaseInsensitive;
	auto telegramMeMatch = regex_match(qsl("^https?://(www\\.)?(telegram\\.(me|dog)|t\\.me)/(.+)$"), url, matchOptions);
	if (telegramMeMatch) {
		auto query = telegramMeMatch->capturedRef(4);
		if (auto joinChatMatch = regex_match(qsl("^joinchat/([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"), query, matchOptions)) {
			return qsl("tg://join?invite=") + url_encode(joinChatMatch->captured(1));
		} else if (auto stickerSetMatch = regex_match(qsl("^addstickers/([a-zA-Z0-9\\.\\_]+)(\\?|$)"), query, matchOptions)) {
			return qsl("tg://addstickers?set=") + url_encode(stickerSetMatch->captured(1));
		} else if (auto shareUrlMatch = regex_match(qsl("^share/url/?\\?(.+)$"), query, matchOptions)) {
			return qsl("tg://msg_url?") + shareUrlMatch->captured(1);
		} else if (auto confirmPhoneMatch = regex_match(qsl("^confirmphone/?\\?(.+)"), query, matchOptions)) {
			return qsl("tg://confirmphone?") + confirmPhoneMatch->captured(1);
		} else if (auto ivMatch = regex_match(qsl("iv/?\\?(.+)(#|$)"), query, matchOptions)) {
			auto params = url_parse_params(ivMatch->captured(1), UrlParamNameTransform::ToLower);
			auto previewedUrl = params.value(qsl("url"));
			if (previewedUrl.startsWith(qstr("http://"), Qt::CaseInsensitive)
				|| previewedUrl.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
				return previewedUrl;
			}
		} else if (auto socksMatch = regex_match(qsl("socks/?\\?(.+)(#|$)"), query, matchOptions)) {
			return qsl("tg://socks?") + socksMatch->captured(1);
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

bool UrlRequiresConfirmation(const QUrl &url) {
	using namespace qthelp;
	return !regex_match(qsl("(^|\\.)(telegram\\.org|telegra\\.ph|telesco\\.pe)$"), url.host(), RegExOption::CaseInsensitive);
}

} // namespace

QString UrlClickHandler::copyToClipboardContextItemText() const {
	return lang(isEmail() ? lng_context_copy_email : lng_context_copy_link);
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

void UrlClickHandler::doOpen(QString url) {
	Ui::Tooltip::Hide();

	if (isEmail(url)) {
		File::OpenEmailLink(url);
		return;
	}

	url = tryConvertUrlToLocal(url);

	if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		Messenger::Instance().openLocalUrl(url);
	} else {
		QDesktopServices::openUrl(url);
	}
}

QString UrlClickHandler::getExpandedLinkText(ExpandLinksMode mode, const QStringRef &textPart) const {
	QString result;
	if (mode != ExpandLinksNone) {
		result = _originalUrl;
	}
	return result;
}

TextWithEntities UrlClickHandler::getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const {
	TextWithEntities result;
	auto entityType = isEmail(_originalUrl) ? EntityInTextEmail : EntityInTextUrl;
	int entityLength = textPart.size();
	if (mode != ExpandLinksNone) {
		result.text = _originalUrl;
		entityLength = _originalUrl.size();
	}
	result.entities.push_back({ entityType, entityOffset, entityLength });
	return result;
}

void HiddenUrlClickHandler::doOpen(QString url) {
	auto urlText = tryConvertUrlToLocal(url);

	if (urlText.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		Messenger::Instance().openLocalUrl(urlText);
	} else {
		auto parsedUrl = QUrl::fromUserInput(urlText);
		if (UrlRequiresConfirmation(urlText)) {
			auto displayUrl = parsedUrl.isValid() ? parsedUrl.toDisplayString() : urlText;
			Ui::show(Box<ConfirmBox>(lang(lng_open_this_link) + qsl("\n\n") + displayUrl, lang(lng_open_link), [urlText] {
				Ui::hideLayer();
				UrlClickHandler::doOpen(urlText);
			}));
		} else {
			UrlClickHandler::doOpen(urlText);
		}
	}
}

void BotGameUrlClickHandler::onClick(Qt::MouseButton button) const {
	auto urlText = tryConvertUrlToLocal(url());

	if (urlText.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		Messenger::Instance().openLocalUrl(urlText);
	} else if (!_bot || _bot->isVerified() || Local::isBotTrusted(_bot)) {
		doOpen(urlText);
	} else {
		Ui::show(Box<ConfirmBox>(lng_allow_bot_pass(lt_bot_name, _bot->name), lang(lng_allow_bot), [bot = _bot, urlText] {
			Ui::hideLayer();
			Local::makeBotTrusted(bot);
			UrlClickHandler::doOpen(urlText);
		}));
	}
}

QString HiddenUrlClickHandler::getExpandedLinkText(ExpandLinksMode mode, const QStringRef &textPart) const {
	QString result;
	if (mode == ExpandLinksAll) {
		result = textPart.toString() + qsl(" (") + url() + ')';
	} else if (mode == ExpandLinksUrlOnly) {
		result = url();
	}
	return result;
}

TextWithEntities HiddenUrlClickHandler::getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const {
	TextWithEntities result;
	if (mode == ExpandLinksUrlOnly) {
		result.text = url();
		result.entities.push_back({ EntityInTextUrl, entityOffset, result.text.size() });
	} else {
		result.entities.push_back({ EntityInTextCustomUrl, entityOffset, textPart.size(), url() });
		if (mode == ExpandLinksAll) {
			result.text = textPart.toString() + qsl(" (") + url() + ')';
		}
	}
	return result;
}

QString MentionClickHandler::copyToClipboardContextItemText() const {
	return lang(lng_context_copy_mention);
}

void MentionClickHandler::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		App::openPeerByName(_tag.mid(1), ShowAtProfileMsgId);
	}
}

TextWithEntities MentionClickHandler::getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const {
	return simpleTextWithEntity({ EntityInTextMention, entityOffset, textPart.size() });
}

void MentionNameClickHandler::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		if (auto user = App::userLoaded(_userId)) {
			Ui::showPeerProfile(user);
		}
	}
}

TextWithEntities MentionNameClickHandler::getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const {
	auto data = QString::number(_userId) + '.' + QString::number(_accessHash);
	return simpleTextWithEntity({ EntityInTextMentionName, entityOffset, textPart.size(), data });
}

QString MentionNameClickHandler::tooltip() const {
	if (auto user = App::userLoaded(_userId)) {
		auto name = App::peerName(user);
		if (name != _text) {
			return name;
		}
	}
	return QString();
}

QString HashtagClickHandler::copyToClipboardContextItemText() const {
	return lang(lng_context_copy_hashtag);
}

void HashtagClickHandler::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		App::searchByHashtag(_tag, Ui::getPeerForMouseAction());
	}
}

TextWithEntities HashtagClickHandler::getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const {
	return simpleTextWithEntity({ EntityInTextHashtag, entityOffset, textPart.size() });
}

PeerData *BotCommandClickHandler::_peer = nullptr;
UserData *BotCommandClickHandler::_bot = nullptr;
void BotCommandClickHandler::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		if (auto peer = peerForCommand()) {
			if (auto bot = peer->isUser() ? peer->asUser() : botForCommand()) {
				Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
				App::sendBotCommand(peer, bot, _cmd);
				return;
			}
		}

		if (auto peer = Ui::getPeerForMouseAction()) { // old way
			UserData *bot = peer->isUser() ? peer->asUser() : nullptr;
			if (auto item = App::hoveredLinkItem()) {
				if (!bot) {
					bot = item->fromOriginal()->asUser(); // may return nullptr
				}
			}
			Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
			App::sendBotCommand(peer, bot, _cmd);
		} else {
			App::insertBotCommand(_cmd);
		}
	}
}

TextWithEntities BotCommandClickHandler::getExpandedLinkTextWithEntities(ExpandLinksMode mode, int entityOffset, const QStringRef &textPart) const {
	return simpleTextWithEntity({ EntityInTextHashtag, entityOffset, textPart.size() });
}
