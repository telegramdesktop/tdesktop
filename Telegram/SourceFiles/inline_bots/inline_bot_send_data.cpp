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
#include "inline_bots/inline_bot_send_data.h"

#include "data/data_document.h"
#include "inline_bots/inline_bot_result.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"

namespace InlineBots {
namespace internal {

QString SendData::getLayoutTitle(const Result *owner) const {
	return owner->_title;
}

QString SendData::getLayoutDescription(const Result *owner) const {
	return owner->_description;
}

void SendDataCommon::addToHistory(const Result *owner, History *history,
MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate,
UserId viaBotId, MsgId replyToId, const QString &postAuthor, const MTPReplyMarkup &markup) const {
	auto fields = getSentMessageFields();
	if (!fields.entities.v.isEmpty()) {
		flags |= MTPDmessage::Flag::f_entities;
	}
	history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(msgId), MTP_int(fromId), peerToMTP(history->peer->id), MTPnullFwdHeader, MTP_int(viaBotId), MTP_int(replyToId), mtpDate, fields.text, fields.media, markup, fields.entities, MTP_int(1), MTPint(), MTP_string(postAuthor)), NewMessageUnread);
}

QString SendDataCommon::getErrorOnSend(const Result *owner, History *history) const {
	if (auto megagroup = history->peer->asMegagroup()) {
		if (megagroup->restricted(ChannelRestriction::f_send_messages)) {
			return lang(lng_restricted_send_message);
		}
	}
	return QString();
}

SendDataCommon::SentMTPMessageFields SendText::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.text = MTP_string(_message);
	result.entities = TextUtilities::EntitiesToMTP(_entities);
	return result;
}

SendDataCommon::SentMTPMessageFields SendGeo::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaGeo(_location.toMTP());
	return result;
}

SendDataCommon::SentMTPMessageFields SendVenue::getSentMessageFields() const {
	SentMTPMessageFields result;
	auto venueType = QString();
	result.media = MTP_messageMediaVenue(
		_location.toMTP(),
		MTP_string(_title),
		MTP_string(_address),
		MTP_string(_provider),
		MTP_string(_venueId),
		MTP_string(venueType));
	return result;
}

SendDataCommon::SentMTPMessageFields SendContact::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaContact(MTP_string(_phoneNumber), MTP_string(_firstName), MTP_string(_lastName), MTP_int(0));
	return result;
}

QString SendContact::getLayoutDescription(const Result *owner) const {
	auto result = SendData::getLayoutDescription(owner);
	if (result.isEmpty()) {
		return App::formatPhone(_phoneNumber);
	}
	return result;
}

void SendPhoto::addToHistory(const Result *owner, History *history,
MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate,
UserId viaBotId, MsgId replyToId, const QString &postAuthor, const MTPReplyMarkup &markup) const {
	history->addNewPhoto(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, postAuthor, _photo, _caption, markup);
}

QString SendPhoto::getErrorOnSend(const Result *owner, History *history) const {
	if (auto megagroup = history->peer->asMegagroup()) {
		if (megagroup->restricted(ChannelRestriction::f_send_media)) {
			return lang(lng_restricted_send_media);
		}
	}
	return QString();
}

void SendFile::addToHistory(const Result *owner, History *history,
MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate,
UserId viaBotId, MsgId replyToId, const QString &postAuthor, const MTPReplyMarkup &markup) const {
	history->addNewDocument(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, postAuthor, _document, _caption, markup);
}

QString SendFile::getErrorOnSend(const Result *owner, History *history) const {
	if (auto megagroup = history->peer->asMegagroup()) {
		if (megagroup->restricted(ChannelRestriction::f_send_media)) {
			return lang(lng_restricted_send_media);
		} else if (megagroup->restricted(ChannelRestriction::f_send_stickers) && (_document->sticker() != nullptr)) {
			return lang(lng_restricted_send_stickers);
		} else if (megagroup->restricted(ChannelRestriction::f_send_gifs) && _document->isAnimation() && !_document->isRoundVideo()) {
			return lang(lng_restricted_send_gifs);
		}
	}
	return QString();
}

void SendGame::addToHistory(const Result *owner, History *history,
	MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate,
	UserId viaBotId, MsgId replyToId, const QString &postAuthor, const MTPReplyMarkup &markup) const {
	history->addNewGame(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, postAuthor, _game, markup);
}

QString SendGame::getErrorOnSend(const Result *owner, History *history) const {
	if (auto megagroup = history->peer->asMegagroup()) {
		if (megagroup->restricted(ChannelRestriction::f_send_games)) {
			return lang(lng_restricted_send_inline);
		}
	}
	return QString();
}

} // namespace internal
} // namespace InlineBots
