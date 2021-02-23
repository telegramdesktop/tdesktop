/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_send_data.h"

#include "api/api_text_entities.h"
#include "data/data_document.h"
#include "inline_bots/inline_bot_result.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_message.h"
#include "data/data_channel.h"
#include "app.h"

namespace InlineBots {
namespace internal {

QString SendData::getLayoutTitle(const Result *owner) const {
	return owner->_title;
}

QString SendData::getLayoutDescription(const Result *owner) const {
	return owner->_description;
}

void SendDataCommon::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		MsgId msgId,
		PeerId fromId,
		MTPint mtpDate,
		UserId viaBotId,
		MsgId replyToId,
		const QString &postAuthor,
		const MTPReplyMarkup &markup) const {
	auto fields = getSentMessageFields();
	if (!fields.entities.v.isEmpty()) {
		flags |= MTPDmessage::Flag::f_entities;
	}
	auto action = Api::SendAction(history);
	action.replyTo = replyToId;
	const auto replyHeader = NewMessageReplyHeader(action);
	if (replyToId) {
		flags |= MTPDmessage::Flag::f_reply_to;
	}
	const auto views = 1;
	const auto forwards = 0;
	history->addNewMessage(
		MTP_message(
			MTP_flags(flags),
			MTP_int(msgId),
			peerToMTP(fromId),
			peerToMTP(history->peer->id),
			MTPMessageFwdHeader(),
			MTP_int(viaBotId),
			replyHeader,
			mtpDate,
			fields.text,
			fields.media,
			markup,
			fields.entities,
			MTP_int(views),
			MTP_int(forwards),
			MTPMessageReplies(),
			MTPint(), // edit_date
			MTP_string(postAuthor),
			MTPlong(),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTPint()), // ttl_period
		clientFlags,
		NewMessageType::Unread);
}

QString SendDataCommon::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto error = Data::RestrictionError(
		history->peer,
		ChatRestriction::f_send_messages);
	return error.value_or(QString());
}

SendDataCommon::SentMTPMessageFields SendText::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.text = MTP_string(_message);
	result.entities = Api::EntitiesToMTP(&session(), _entities);
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
	const auto userId = 0;
	const auto vcard = QString();
	result.media = MTP_messageMediaContact(
		MTP_string(_phoneNumber),
		MTP_string(_firstName),
		MTP_string(_lastName),
		MTP_string(vcard),
		MTP_int(userId));
	return result;
}

QString SendContact::getLayoutDescription(const Result *owner) const {
	auto result = SendData::getLayoutDescription(owner);
	if (result.isEmpty()) {
		return App::formatPhone(_phoneNumber);
	}
	return result;
}

void SendPhoto::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		MsgId msgId,
		PeerId fromId,
		MTPint mtpDate,
		UserId viaBotId,
		MsgId replyToId,
		const QString &postAuthor,
		const MTPReplyMarkup &markup) const {
	history->addNewLocalMessage(
		msgId,
		flags,
		clientFlags,
		viaBotId,
		replyToId,
		mtpDate.v,
		fromId,
		postAuthor,
		_photo,
		{ _message, _entities },
		markup);
}

QString SendPhoto::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto error = Data::RestrictionError(
		history->peer,
		ChatRestriction::f_send_media);
	return error.value_or(QString());
}

void SendFile::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		MsgId msgId,
		PeerId fromId,
		MTPint mtpDate,
		UserId viaBotId,
		MsgId replyToId,
		const QString &postAuthor,
		const MTPReplyMarkup &markup) const {
	history->addNewLocalMessage(
		msgId,
		flags,
		clientFlags,
		viaBotId,
		replyToId,
		mtpDate.v,
		fromId,
		postAuthor,
		_document,
		{ _message, _entities },
		markup);
}

QString SendFile::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto errorMedia = Data::RestrictionError(
		history->peer,
		ChatRestriction::f_send_media);
	const auto errorStickers = Data::RestrictionError(
		history->peer,
		ChatRestriction::f_send_stickers);
	const auto errorGifs = Data::RestrictionError(
		history->peer,
		ChatRestriction::f_send_gifs);
	return errorMedia
		? *errorMedia
		: (errorStickers && (_document->sticker() != nullptr))
		? *errorStickers
		: (errorGifs
			&& _document->isAnimation()
			&& !_document->isVideoMessage())
		? *errorGifs
		: QString();
}

void SendGame::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		MsgId msgId,
		PeerId fromId,
		MTPint mtpDate,
		UserId viaBotId,
		MsgId replyToId,
		const QString &postAuthor,
		const MTPReplyMarkup &markup) const {
	history->addNewLocalMessage(
		msgId,
		flags,
		clientFlags,
		viaBotId,
		replyToId,
		mtpDate.v,
		fromId,
		postAuthor,
		_game,
		markup);
}

QString SendGame::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto error = Data::RestrictionError(
		history->peer,
		ChatRestriction::f_send_games);
	return error.value_or(QString());
}

} // namespace internal
} // namespace InlineBots
