/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_file.h"
#include "api/api_common.h"
#include "media/view/media_view_open_common.h"

class FileLoader;
class History;
class UserData;

namespace Data {
class LocationPoint;
} // namespace Data

namespace InlineBots {

namespace Layout {
class ItemBase;
} // namespace Layout

namespace internal {
class SendData;
} // namespace internal

class Result {
private:
	// See http://stackoverflow.com/a/8147326
	struct Creator;

public:
	// Constructor is public only for std::make_unique<>() to work.
	// You should use create() static method instead.
	Result(not_null<Main::Session*> session, const Creator &creator);

	static std::unique_ptr<Result> Create(
		not_null<Main::Session*> session,
		uint64 queryId,
		const MTPBotInlineResult &mtpData);

	uint64 getQueryId() const {
		return _queryId;
	}
	QString getId() const {
		return _id;
	}

	// This is real SendClickHandler::onClick implementation for the specified
	// inline bot result. If it returns true you need to send this result.
	bool onChoose(Layout::ItemBase *layout);

	Media::View::OpenRequest openRequest();
	void cancelFile();

	bool hasThumbDisplay() const;

	void addToHistory(
		History *history,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		MsgId msgId,
		PeerId fromId,
		MTPint mtpDate,
		UserId viaBotId,
		MsgId replyToId,
		const QString &postAuthor) const;
	QString getErrorOnSend(History *history) const;

	// interface for Layout:: usage
	std::optional<Data::LocationPoint> getLocationPoint() const;
	QString getLayoutTitle() const;
	QString getLayoutDescription() const;

	~Result();

private:
	void createGame(not_null<Main::Session*> session);
	QSize thumbBox() const;
	MTPWebDocument adjustAttributes(const MTPWebDocument &document);
	MTPVector<MTPDocumentAttribute> adjustAttributes(
		const MTPVector<MTPDocumentAttribute> &document,
		const MTPstring &mimeType);

	enum class Type {
		Unknown,
		Photo,
		Video,
		Audio,
		Sticker,
		File,
		Gif,
		Article,
		Contact,
		Geo,
		Venue,
		Game,
	};

	friend class internal::SendData;
	friend class Layout::ItemBase;
	struct Creator {
		uint64 queryId = 0;
		Type type = Type::Unknown;
	};

	not_null<Main::Session*> _session;
	uint64 _queryId = 0;
	QString _id;
	Type _type = Type::Unknown;
	QString _title, _description, _url;
	QString _content_url;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	GameData *_game = nullptr;

	std::unique_ptr<MTPReplyMarkup> _mtpKeyboard;

	Data::CloudImage _thumbnail;
	Data::CloudImage _locationThumbnail;

	std::unique_ptr<internal::SendData> sendData;

};

struct ResultSelected {
	not_null<Result*> result;
	not_null<UserData*> bot;
	Api::SendOptions options;
	// Open in OverlayWidget;
	bool open = false;
};

} // namespace InlineBots
