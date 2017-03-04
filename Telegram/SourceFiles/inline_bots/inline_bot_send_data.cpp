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

#include "inline_bots/inline_bot_result.h"
#include "storage/localstorage.h"

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
UserId viaBotId, MsgId replyToId, const MTPReplyMarkup &markup) const {
	SentMTPMessageFields fields = getSentMessageFields();
	if (!fields.entities.c_vector().v.isEmpty()) {
		flags |= MTPDmessage::Flag::f_entities;
	}
	history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(msgId), MTP_int(fromId), peerToMTP(history->peer->id), MTPnullFwdHeader, MTP_int(viaBotId), MTP_int(replyToId), mtpDate, fields.text, fields.media, markup, fields.entities, MTP_int(1), MTPint()), NewMessageUnread);
}

SendDataCommon::SentMTPMessageFields SendText::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.text = MTP_string(_message);
	result.entities = linksToMTP(_entities);
	return result;
}

SendDataCommon::SentMTPMessageFields SendGeo::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaGeo(_location.toMTP());
	return result;
}

SendDataCommon::SentMTPMessageFields SendVenue::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaVenue(_location.toMTP(), MTP_string(_title), MTP_string(_address), MTP_string(_provider), MTP_string(_venueId));
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
UserId viaBotId, MsgId replyToId, const MTPReplyMarkup &markup) const {
	history->addNewPhoto(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, _photo, _caption, markup);
}

void SendFile::addToHistory(const Result *owner, History *history,
MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate,
UserId viaBotId, MsgId replyToId, const MTPReplyMarkup &markup) const {
	history->addNewDocument(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, _document, _caption, markup);
}

void SendGame::addToHistory(const Result *owner, History *history,
	MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate,
	UserId viaBotId, MsgId replyToId, const MTPReplyMarkup &markup) const {
	history->addNewGame(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, _game, markup);
}

} // namespace internal
} // namespace InlineBots
