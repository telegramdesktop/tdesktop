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
	return owner->title;
}

QString SendData::getLayoutDescription(const Result *owner) const {
	return owner->description;
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
	return App::formatPhone(_phoneNumber) + '\n' + owner->description;
}

SendData::SentMTPMessageFields SendPhoto::getSentMessageFields(const Result *owner) const {
	SentMTPMessageFields result;

	QImage fileThumb(owner->thumb->pix().toImage());

	QVector<MTPPhotoSize> photoSizes;

	QPixmap thumb = (fileThumb.width() > 100 || fileThumb.height() > 100) ? QPixmap::fromImage(fileThumb.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(fileThumb);
	ImagePtr thumbPtr = ImagePtr(thumb, "JPG");
	photoSizes.push_back(MTP_photoSize(MTP_string("s"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

	QSize medium = resizeKeepAspect(owner->width, owner->height, 320, 320);
	photoSizes.push_back(MTP_photoSize(MTP_string("m"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

	photoSizes.push_back(MTP_photoSize(MTP_string("x"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(owner->width), MTP_int(owner->height), MTP_int(0)));

	uint64 photoId = rand_value<uint64>();
	PhotoData *ph = App::photoSet(photoId, 0, 0, unixtime(), thumbPtr, ImagePtr(medium.width(), medium.height()), ImagePtr(owner->width, owner->height));
	MTPPhoto photo = MTP_photo(MTP_long(photoId), MTP_long(0), MTP_int(ph->date), MTP_vector<MTPPhotoSize>(photoSizes));

	result.media = MTP_messageMediaPhoto(photo, MTP_string(_caption));

	return result;
}

SendData::SentMTPMessageFields SendFile::getSentMessageFields(const Result *owner) const {
	SentMTPMessageFields result;

	MTPPhotoSize thumbSize;
	QPixmap thumb;
	int32 tw = owner->thumb->width(), th = owner->thumb->height();
	if (tw > 0 && th > 0 && tw < 20 * th && th < 20 * tw && owner->thumb->loaded()) {
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
		thumb = owner->thumb->pixNoCache(tw, th, ImagePixSmooth);
	} else {
		tw = th = 0;
		thumbSize = MTP_photoSizeEmpty(MTP_string(""));
	}
	uint64 docId = rand_value<uint64>();
	QVector<MTPDocumentAttribute> attributes;

	int duration = getSentDuration(owner);
	QSize dimensions = getSentDimensions(owner);
	using Type = Result::Type;
	if (owner->type == Type::Gif) {
		attributes.push_back(MTP_documentAttributeFilename(MTP_string((owner->content_type == qstr("video/mp4") ? "animation.gif.mp4" : "animation.gif"))));
		attributes.push_back(MTP_documentAttributeAnimated());
		attributes.push_back(MTP_documentAttributeVideo(MTP_int(duration), MTP_int(dimensions.width()), MTP_int(dimensions.height())));
	} else if (owner->type == Type::Video) {
		attributes.push_back(MTP_documentAttributeVideo(MTP_int(duration), MTP_int(dimensions.width()), MTP_int(dimensions.height())));
	}
	MTPDocument document = MTP_document(MTP_long(docId), MTP_long(0), MTP_int(unixtime()), MTP_string(owner->content_type), MTP_int(owner->data().size()), thumbSize, MTP_int(MTP::maindc()), MTP_vector<MTPDocumentAttribute>(attributes));
	if (tw > 0 && th > 0) {
		App::feedDocument(document, thumb);
	}
	Local::writeStickerImage(mediaKey(DocumentFileLocation, MTP::maindc(), docId), owner->data());

	result.media = MTP_messageMediaDocument(document, MTP_string(_caption));

	return result;
}

int SendFile::getSentDuration(const Result *owner) const {
	return (_document && _document->duration()) ? _document->duration() : owner->duration;
}
QSize SendFile::getSentDimensions(const Result *owner) const {
	return (!_document || _document->dimensions.isEmpty()) ? QSize(owner->width, owner->height) : _document->dimensions;
}

} // namespace internal
} // namespace InlineBots
