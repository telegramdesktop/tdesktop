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
	}
	return result;
}

void SendData::setResultDocument(const Result *owner, const MTPDocument &document) const {
	owner->_mtpDocument = document;
}

void SendData::setResultPhoto(const Result *owner, const MTPPhoto &photo) const {
	owner->_mtpPhoto = photo;
}

MTPDocument SendData::getResultDocument(const Result *owner) const {
	return owner->_mtpDocument;
}

MTPPhoto SendData::getResultPhoto(const Result *owner) const {
	return owner->_mtpPhoto;
}

SendData::SentMTPMessageFields SendText::getSentMessageFields(const Result*) const {
	SentMTPMessageFields result;
	result.text = MTP_string(_message);
	result.entities = linksToMTP(_entities);
	return result;
}

SendData::SentMTPMessageFields SendGeo::getSentMessageFields(const Result*) const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaGeo(MTP_geoPoint(MTP_double(_location.lon), MTP_double(_location.lat)));
	return result;
}

SendData::SentMTPMessageFields SendVenue::getSentMessageFields(const Result*) const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaVenue(MTP_geoPoint(MTP_double(_location.lon), MTP_double(_location.lat)), MTP_string(_title), MTP_string(_address), MTP_string(_provider), MTP_string(_venueId));
	return result;
}

SendData::SentMTPMessageFields SendContact::getSentMessageFields(const Result*) const {
	SentMTPMessageFields result;
	result.media = MTP_messageMediaContact(MTP_string(_phoneNumber), MTP_string(_firstName), MTP_string(_lastName), MTP_int(0));
	return result;
}

QString SendContact::getLayoutDescription(const Result *owner) const {
	return App::formatPhone(_phoneNumber) + '\n' + SendData::getLayoutDescription(owner);
}

SendData::SentMTPMessageFields SendPhoto::getSentMessageFields(const Result *owner) const {
	SentMTPMessageFields result;

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

	result.media = MTP_messageMediaPhoto(photo, MTP_string(_caption));

	return result;
}

void SendFile::prepareDocument(const Result *owner) const {
	if (getResultDocument(owner).type() != mtpc_documentEmpty) return;

	ImagePtr resultThumb = getResultThumb(owner);
	MTPPhotoSize thumbSize;
	QPixmap thumb;
	int32 tw = resultThumb->width(), th = resultThumb->height();
	if (tw > 0 && th > 0 && tw < 20 * th && th < 20 * tw && resultThumb->loaded()) {
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
	uint64 docId = rand_value<uint64>();

	QVector<MTPDocumentAttribute> attributes = prepareResultAttributes(owner);
	MTPDocument document = MTP_document(MTP_long(docId), MTP_long(0), MTP_int(unixtime()), MTP_string(getResultMime(owner)), MTP_int(owner->data().size()), thumbSize, MTP_int(MTP::maindc()), MTP_vector<MTPDocumentAttribute>(attributes));
	if (tw > 0 && th > 0) {
		App::feedDocument(document, thumb);
	}
	if (!owner->data().isEmpty()) {
		Local::writeStickerImage(mediaKey(DocumentFileLocation, MTP::maindc(), docId), owner->data());
	}
	setResultDocument(owner, document);
}

SendData::SentMTPMessageFields SendFile::getSentMessageFields(const Result *owner) const {
	SentMTPMessageFields result;

	prepareDocument(owner);

	MTPDocument document = getResultDocument(owner);
	result.media = MTP_messageMediaDocument(document, MTP_string(_caption));

	return result;
}

} // namespace internal
} // namespace InlineBots
