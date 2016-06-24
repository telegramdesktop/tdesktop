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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "core/click_handler_types.h"

#include "lang.h"
#include "pspecific.h"
#include "boxes/confirmbox.h"

QString UrlClickHandler::copyToClipboardContextItemText() const {
	return lang(isEmail() ? lng_context_copy_email : lng_context_copy_link);
}

namespace {

QString tryConvertUrlToLocal(const QString &url) {
	QRegularExpressionMatch telegramMeUser = QRegularExpression(qsl("^https?://telegram\\.me/([a-zA-Z0-9\\.\\_]+)(/?\\?|/?$|/(\\d+)/?(?:\\?|$))"), QRegularExpression::CaseInsensitiveOption).match(url);
	QRegularExpressionMatch telegramMeGroup = QRegularExpression(qsl("^https?://telegram\\.me/joinchat/([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"), QRegularExpression::CaseInsensitiveOption).match(url);
	QRegularExpressionMatch telegramMeStickers = QRegularExpression(qsl("^https?://telegram\\.me/addstickers/([a-zA-Z0-9\\.\\_]+)(\\?|$)"), QRegularExpression::CaseInsensitiveOption).match(url);
	QRegularExpressionMatch telegramMeShareUrl = QRegularExpression(qsl("^https?://telegram\\.me/share/url\\?(.+)$"), QRegularExpression::CaseInsensitiveOption).match(url);
	if (telegramMeGroup.hasMatch()) {
		return qsl("tg://join?invite=") + myUrlEncode(telegramMeGroup.captured(1));
	} else if (telegramMeStickers.hasMatch()) {
		return qsl("tg://addstickers?set=") + myUrlEncode(telegramMeStickers.captured(1));
	} else if (telegramMeShareUrl.hasMatch()) {
		return qsl("tg://msg_url?") + telegramMeShareUrl.captured(1);
	} else if (telegramMeUser.hasMatch()) {
		QString params = url.mid(telegramMeUser.captured(0).size()), postParam;
		if (QRegularExpression(qsl("^/\\d+/?(?:\\?|$)")).match(telegramMeUser.captured(2)).hasMatch()) {
			postParam = qsl("&post=") + telegramMeUser.captured(3);
		}
		return qsl("tg://resolve/?domain=") + myUrlEncode(telegramMeUser.captured(1)) + postParam + (params.isEmpty() ? QString() : '&' + params);
	}
	return url;
}

} // namespace

void UrlClickHandler::doOpen(QString url) {
	PopupTooltip::Hide();

	if (isEmail(url)) {
		QUrl u(qstr("mailto:") + url);
		if (!QDesktopServices::openUrl(u)) {
			psOpenFile(u.toString(QUrl::FullyEncoded), true);
		}
		return;
	}

	url = tryConvertUrlToLocal(url);

	if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		App::openLocalUrl(url);
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

void HiddenUrlClickHandler::onClick(Qt::MouseButton button) const {
	auto u = url();

	u = tryConvertUrlToLocal(u);

	if (u.startsWith(qstr("tg://"))) {
		App::openLocalUrl(u);
	} else {
		Ui::showLayer(new ConfirmLinkBox(u));
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
