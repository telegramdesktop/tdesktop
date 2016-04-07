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
#pragma once

#include "basic_types.h"
#include "structs.h"
#include "mtproto/core_types.h"

namespace InlineBots {

class Result;

namespace internal {

// Abstract class describing the message that will be
// sent if the user chooses this inline bot result.
// For each type of message that can be sent there will be a subclass.
class SendData {
public:
	SendData() = default;
	SendData(const SendData &other) = delete;
	SendData &operator=(const SendData &other) = delete;
	virtual ~SendData() = default;

	virtual bool isValid() const = 0;

	virtual DocumentData *getSentDocument() const {
		return nullptr;
	}
	virtual PhotoData *getSentPhoto() const {
		return nullptr;
	}
	virtual QString getSentCaption() const {
		return QString();
	}
	struct SentMTPMessageFields {
		MTPString text = MTP_string("");
		MTPVector<MTPMessageEntity> entities = MTPnullEntities;
		MTPMessageMedia media = MTP_messageMediaEmpty();
	};
	virtual SentMTPMessageFields getSentMessageFields(const Result *owner) const = 0;

	virtual bool hasLocationCoords() const {
		return false;
	}
	virtual bool getLocationCoords(LocationCoords *outLocation) const {
		return false;
	}
	virtual QString getLayoutTitle(const Result *owner) const;
	virtual QString getLayoutDescription(const Result *owner) const;

protected:

	ImagePtr getResultThumb(const Result *owner) const;
	int getResultWidth(const Result *owner) const;
	int getResultHeight(const Result *owner) const;
	QString getResultMime(const Result *owner) const;
	QVector<MTPDocumentAttribute> prepareResultAttributes(const Result *owner) const;

	void setResultDocument(const Result *owner, const MTPDocument &document) const;
	void setResultPhoto(const Result *owner, const MTPPhoto &photo) const;

	MTPDocument getResultDocument(const Result *owner) const;
	MTPPhoto getResultPhoto(const Result *owner) const;

};

// Plain text message.
class SendText : public SendData {
public:
	SendText(const QString &message, const EntitiesInText &entities, bool/* noWebPage*/)
		: _message(message)
		, _entities(entities) {
	}

	bool isValid() const override {
		return !_message.isEmpty();
	}

	SentMTPMessageFields getSentMessageFields(const Result *owner) const override;

private:
	QString _message;
	EntitiesInText _entities;

};

// Message with geo location point media.
class SendGeo : public SendData {
public:
	SendGeo(const MTPDgeoPoint &point) : _location(point) {
	}

	bool isValid() const override {
		return true;
	}

	SentMTPMessageFields getSentMessageFields(const Result *owner) const override;

	bool hasLocationCoords() const override {
		return true;
	}
	bool getLocationCoords(LocationCoords *outLocation) const override {
		t_assert(outLocation != nullptr);
		*outLocation = _location;
		return true;
	}

private:
	LocationCoords _location;

};

// Message with venue media.
class SendVenue : public SendData {
public:
	SendVenue(const MTPDgeoPoint &point, const QString &venueId,
		const QString &provider, const QString &title, const QString &address)
		: _location(point)
		, _venueId(venueId)
		, _provider(provider)
		, _title(title)
		, _address(address) {
	}

	bool isValid() const override {
		return true;
	}

	SentMTPMessageFields getSentMessageFields(const Result *owner) const override;

	bool hasLocationCoords() const override {
		return true;
	}
	bool getLocationCoords(LocationCoords *outLocation) const override {
		t_assert(outLocation != nullptr);
		*outLocation = _location;
		return true;
	}

private:
	LocationCoords _location;
	QString _venueId, _provider, _title, _address;

};

// Message with shared contact media.
class SendContact : public SendData {
public:
	SendContact(const QString &firstName, const QString &lastName, const QString &phoneNumber)
		: _firstName(firstName)
		, _lastName(lastName)
		, _phoneNumber(phoneNumber) {
	}

	bool isValid() const override {
		return (!_firstName.isEmpty() || !_lastName.isEmpty()) && !_phoneNumber.isEmpty();
	}

	SentMTPMessageFields getSentMessageFields(const Result *owner) const override;

	QString getLayoutDescription(const Result *owner) const override;

private:
	QString _firstName, _lastName, _phoneNumber;

};

// Message with photo.
class SendPhoto : public SendData {
public:
	SendPhoto(PhotoData *photo, const QString &url, const QString &caption)
		: _photo(photo)
		, _url(url)
		, _caption(caption) {
	}

	bool isValid() const override {
		return _photo || !_url.isEmpty();
	}

	PhotoData *getSentPhoto() const override {
		return _photo;
	}
	QString getSentCaption() const override {
		return _caption;
	}
	SentMTPMessageFields getSentMessageFields(const Result *owner) const override;

private:
	PhotoData *_photo;
	QString _url, _caption;

};

// Message with file.
class SendFile : public SendData {
public:
	SendFile(DocumentData *document, const QString &url, const QString &caption)
		: _document(document)
		, _url(url)
		, _caption(caption) {
	}

	bool isValid() const override {
		return _document || !_url.isEmpty();
	}

	DocumentData *getSentDocument() const override {
		return _document;
	}
	QString getSentCaption() const override {
		return _caption;
	}
	SentMTPMessageFields getSentMessageFields(const Result *owner) const override;

private:
	void prepareDocument(const Result *owner) const;

	DocumentData *_document;
	QString _url, _caption;

};

} // namespace internal
} // namespace InlineBots
