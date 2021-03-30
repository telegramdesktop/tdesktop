/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_location_manager.h"

namespace Main {
class Session;
} // namespace Main

class History;

namespace InlineBots {

class Result;

namespace internal {

// Abstract class describing the message that will be
// sent if the user chooses this inline bot result.
// For each type of message that can be sent there will be a subclass.
class SendData {
public:
	explicit SendData(not_null<Main::Session*> session) : _session(session) {
	}
	SendData(const SendData &other) = delete;
	SendData &operator=(const SendData &other) = delete;
	virtual ~SendData() = default;

	[[nodiscard]] Main::Session &session() const {
		return *_session;
	}

	virtual bool isValid() const = 0;

	virtual void addToHistory(
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
		const MTPReplyMarkup &markup) const = 0;
	virtual QString getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const = 0;

	virtual bool hasLocationCoords() const {
		return false;
	}
	virtual std::optional<Data::LocationPoint> getLocationPoint() const {
		return std::nullopt;
	}
	virtual QString getLayoutTitle(const Result *owner) const;
	virtual QString getLayoutDescription(const Result *owner) const;

private:
	not_null<Main::Session*> _session;

};

// This class implements addHistory() for most of the types hiding
// the differences in getSentMessageFields() method.
// Only SendFile and SendPhoto work by their own.
class SendDataCommon : public SendData {
public:
	using SendData::SendData;

	struct SentMTPMessageFields {
		MTPString text = MTP_string();
		MTPVector<MTPMessageEntity> entities = MTP_vector<MTPMessageEntity>();
		MTPMessageMedia media = MTP_messageMediaEmpty();
	};
	virtual SentMTPMessageFields getSentMessageFields() const = 0;

	void addToHistory(
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
		const MTPReplyMarkup &markup) const override;

	QString getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const override;

};

// Plain text message.
class SendText : public SendDataCommon {
public:
	SendText(
		not_null<Main::Session*> session,
		const QString &message,
		const EntitiesInText &entities,
		bool/* noWebPage*/)
	: SendDataCommon(session)
	, _message(message)
	, _entities(entities) {
	}

	bool isValid() const override {
		return !_message.isEmpty();
	}

	SentMTPMessageFields getSentMessageFields() const override;

private:
	QString _message;
	EntitiesInText _entities;

};

// Message with geo location point media.
class SendGeo : public SendDataCommon {
public:
	SendGeo(
		not_null<Main::Session*> session,
		const MTPDgeoPoint &point)
	: SendDataCommon(session)
	, _location(point) {
	}
	SendGeo(
		not_null<Main::Session*> session,
		const MTPDgeoPoint &point,
		int period,
		std::optional<int> heading,
		std::optional<int> proximityNotificationRadius)
	: SendDataCommon(session)
	, _location(point)
	, _period(period)
	, _heading(heading)
	, _proximityNotificationRadius(proximityNotificationRadius){
	}

	bool isValid() const override {
		return true;
	}

	SentMTPMessageFields getSentMessageFields() const override;

	bool hasLocationCoords() const override {
		return true;
	}
	std::optional<Data::LocationPoint> getLocationPoint() const override {
		return _location;
	}

private:
	Data::LocationPoint _location;
	std::optional<int> _period;
	std::optional<int> _heading;
	std::optional<int> _proximityNotificationRadius;

};

// Message with venue media.
class SendVenue : public SendDataCommon {
public:
	SendVenue(
		not_null<Main::Session*> session,
		const MTPDgeoPoint &point,
		const QString &venueId,
		const QString &provider,
		const QString &title,
		const QString &address)
	: SendDataCommon(session)
	, _location(point)
	, _venueId(venueId)
	, _provider(provider)
	, _title(title)
	, _address(address) {
	}

	bool isValid() const override {
		return true;
	}

	SentMTPMessageFields getSentMessageFields() const override;

	bool hasLocationCoords() const override {
		return true;
	}
	std::optional<Data::LocationPoint> getLocationPoint() const override {
		return _location;
	}

private:
	Data::LocationPoint _location;
	QString _venueId, _provider, _title, _address;

};

// Message with shared contact media.
class SendContact : public SendDataCommon {
public:
	SendContact(
		not_null<Main::Session*> session,
		const QString &firstName,
		const QString &lastName,
		const QString &phoneNumber)
	: SendDataCommon(session)
	, _firstName(firstName)
	, _lastName(lastName)
	, _phoneNumber(phoneNumber) {
	}

	bool isValid() const override {
		return (!_firstName.isEmpty() || !_lastName.isEmpty()) && !_phoneNumber.isEmpty();
	}

	SentMTPMessageFields getSentMessageFields() const override;

	QString getLayoutDescription(const Result *owner) const override;

private:
	QString _firstName, _lastName, _phoneNumber;

};

// Message with photo.
class SendPhoto : public SendData {
public:
	SendPhoto(
		not_null<Main::Session*> session,
		PhotoData *photo,
		const QString &message,
		const EntitiesInText &entities)
	: SendData(session)
	, _photo(photo)
	, _message(message)
	, _entities(entities) {
	}

	bool isValid() const override {
		return _photo != nullptr;
	}

	void addToHistory(
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
		const MTPReplyMarkup &markup) const override;

	QString getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const override;

private:
	PhotoData *_photo;
	QString _message;
	EntitiesInText _entities;

};

// Message with file.
class SendFile : public SendData {
public:
	SendFile(
		not_null<Main::Session*> session,
		DocumentData *document,
		const QString &message,
		const EntitiesInText &entities)
	: SendData(session)
	, _document(document)
	, _message(message)
	, _entities(entities) {
	}

	bool isValid() const override {
		return _document != nullptr;
	}

	void addToHistory(
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
		const MTPReplyMarkup &markup) const override;

	QString getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const override;

private:
	DocumentData *_document;
	QString _message;
	EntitiesInText _entities;

};

// Message with game.
class SendGame : public SendData {
public:
	SendGame(not_null<Main::Session*> session, GameData *game)
	: SendData(session)
	, _game(game) {
	}

	bool isValid() const override {
		return _game != nullptr;
	}

	void addToHistory(
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
		const MTPReplyMarkup &markup) const override;

	QString getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const override;

private:
	GameData *_game;

};

class SendInvoice : public SendDataCommon {
public:
	SendInvoice(
		not_null<Main::Session*> session,
		MTPMessageMedia media)
	: SendDataCommon(session)
	, _media(media) {
	}

	bool isValid() const override {
		return true;
	}

	SentMTPMessageFields getSentMessageFields() const override;

	QString getLayoutDescription(const Result *owner) const override;

private:
	MTPMessageMedia _media;

};

} // namespace internal
} // namespace InlineBots
