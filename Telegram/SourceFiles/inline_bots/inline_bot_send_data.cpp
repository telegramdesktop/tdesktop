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
#include "inline_bots/inline_bot_send_data.h"

#include "inline_bots/inline_bot_result.h"
#include "localstorage.h"

namespace InlineBots {
namespace internal {

QString SendData::getLayoutTitle(const Result *owner) const {
	return owner->_title;
}

QString SendData::getLayoutDescription(const Result *owner) const {
	return owner->_description;
}

ImagePtr SendData::getResultThumb(const Result *owner) const {
	return owner->_thumb;
}

int SendData::getResultWidth(const Result *owner) const {
	return owner->_width;
}

int SendData::getResultHeight(const Result *owner) const {
	return owner->_height;
}

QString SendData::getResultMime(const Result *owner) const {
	return owner->_content_type;
}

QVector<MTPDocumentAttribute> SendData::prepareResultAttributes(const Result *owner) const {
	QVector<MTPDocumentAttribute> result;
	int duration = owner->_duration;
	QSize dimensions(owner->_width, owner->_height);
	using Type = Result::Type;
	if (owner->_type == Type::Gif) {
		const char *filename = (owner->_content_type == qstr("video/mp4") ? "animation.gif.mp4" : "animation.gif");
		result.push_back(MTP_documentAttributeFilename(MTP_string(filename)));
		result.push_back(MTP_documentAttributeAnimated());
		result.push_back(MTP_documentAttributeVideo(MTP_int(owner->_duration), MTP_int(owner->_width), MTP_int(owner->_height)));
	} else if (owner->_type == Type::Video) {
		result.push_back(MTP_documentAttributeVideo(MTP_int(owner->_duration), MTP_int(owner->_width), MTP_int(owner->_height)));
	} else if (owner->_type == Type::Audio) {
		MTPDdocumentAttributeAudio::Flags flags = 0;
		if (owner->_content_type == qstr("audio/ogg")) {
			flags |= MTPDdocumentAttributeAudio::Flag::f_voice;
		}
		result.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(owner->_duration), MTPstring(), MTPstring(), MTPbytes()));
	}
	return result;
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
	result.media = MTP_messageMediaGeo(MTP_geoPoint(MTP_double(_location.lon), MTP_double(_location.lat)));
	return result;
}

SendDataCommon::SentMTPMessageFields SendVenue::getSentMessageFields() const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaVenue(MTP_geoPoint(MTP_double(_location.lon), MTP_double(_location.lat)), MTP_string(_title), MTP_string(_address), MTP_string(_provider), MTP_string(_venueId));
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
	if (_photo) {
		history->addNewPhoto(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, _photo, _caption, markup);
		return;
	}

	ImagePtr resultThumb = getResultThumb(owner);
	QImage fileThumb(resultThumb->pix().toImage());

	QVector<MTPPhotoSize> photoSizes;

	QPixmap thumb = (fileThumb.width() > 100 || fileThumb.height() > 100) ? QPixmap::fromImage(fileThumb.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(fileThumb);
	ImagePtr thumbPtr = ImagePtr(thumb, "JPG");
	photoSizes.push_back(MTP_photoSize(MTP_string("s"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

	QSize medium = resizeKeepAspect(getResultWidth(owner), getResultHeight(owner), 320, 320);
	photoSizes.push_back(MTP_photoSize(MTP_string("m"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

	photoSizes.push_back(MTP_photoSize(MTP_string("x"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(getResultWidth(owner)), MTP_int(getResultHeight(owner)), MTP_int(0)));

	uint64 photoId = rand_value<uint64>();
	PhotoData *ph = App::photoSet(photoId, 0, 0, unixtime(), thumbPtr, ImagePtr(medium.width(), medium.height()), ImagePtr(getResultWidth(owner), getResultHeight(owner)));
	MTPPhoto photo = MTP_photo(MTP_long(photoId), MTP_long(0), MTP_int(ph->date), MTP_vector<MTPPhotoSize>(photoSizes));

	MTPMessageMedia media = MTP_messageMediaPhoto(photo, MTP_string(_caption));
	history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(msgId), MTP_int(fromId), peerToMTP(history->peer->id), MTPnullFwdHeader, MTP_int(viaBotId), MTP_int(replyToId), mtpDate, MTP_string(""), media, markup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
}

void SendFile::addToHistory(const Result *owner, History *history,
MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate,
UserId viaBotId, MsgId replyToId, const MTPReplyMarkup &markup) const {
	if (_document) {
		history->addNewDocument(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, _document, _caption, markup);
		return;
	}

	uint64 docId = rand_value<uint64>();

	ImagePtr resultThumb = getResultThumb(owner);
	MTPPhotoSize thumbSize;
	QPixmap thumb;
	int tw = resultThumb->width(), th = resultThumb->height();
	if (!resultThumb->isNull() && tw > 0 && th > 0 && tw < 20 * th && th < 20 * tw && resultThumb->loaded()) {
		if (tw > th) {
			if (tw > 90) {
				th = th * 90 / tw;
				tw = 90;
			}
		} else if (th > 90) {
			tw = tw * 90 / th;
			th = 90;
		}
		thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(tw), MTP_int(th), MTP_int(0));
		thumb = resultThumb->pixNoCache(tw, th, ImagePixSmooth);
	} else {
		tw = th = 0;
		thumbSize = MTP_photoSizeEmpty(MTP_string(""));
	}

	QVector<MTPDocumentAttribute> attributes = prepareResultAttributes(owner);
	MTPDocument document = MTP_document(MTP_long(docId), MTP_long(0), MTP_int(unixtime()), MTP_string(getResultMime(owner)), MTP_int(owner->data().size()), thumbSize, MTP_int(MTP::maindc()), MTP_vector<MTPDocumentAttribute>(attributes));

	if (!owner->data().isEmpty()) {
		Local::writeStickerImage(mediaKey(DocumentFileLocation, MTP::maindc(), docId), owner->data());
	}

	if (tw > 0 && th > 0) {
		App::feedDocument(document, thumb);
	}
	MTPMessageMedia media = MTP_messageMediaDocument(document, MTP_string(_caption));
	history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(msgId), MTP_int(fromId), peerToMTP(history->peer->id), MTPnullFwdHeader, MTP_int(viaBotId), MTP_int(replyToId), mtpDate, MTP_string(""), media, markup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
}

} // namespace internal
} // namespace InlineBots
