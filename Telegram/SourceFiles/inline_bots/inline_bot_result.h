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
#pragma once

#include "core/basic_types.h"
#include "structs.h"
#include "mtproto/core_types.h"

class FileLoader;

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
	explicit Result(const Creator &creator);
	static std::unique_ptr<Result> create(uint64 queryId, const MTPBotInlineResult &mtpData);
	Result(const Result &other) = delete;
	Result &operator=(const Result &other) = delete;

	uint64 getQueryId() const {
		return _queryId;
	}
	QString getId() const {
		return _id;
	}

	// This is real SendClickHandler::onClick implementation for the specified
	// inline bot result. If it returns true you need to send this result.
	bool onChoose(Layout::ItemBase *layout);

	void forget();
	void openFile();
	void cancelFile();

	bool hasThumbDisplay() const;

	void addToHistory(History *history, MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate, UserId viaBotId, MsgId replyToId, const QString &postAuthor) const;
	QString getErrorOnSend(History *history) const;

	// interface for Layout:: usage
	bool getLocationCoords(LocationCoords *outLocation) const;
	QString getLayoutTitle() const;
	QString getLayoutDescription() const;

	~Result();

private:
	void createPhoto();
	void createDocument();
	void createGame();

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
		uint64 queryId;
		Type type;
	};

	uint64 _queryId = 0;
	QString _id;
	Type _type = Type::Unknown;
	QString _title, _description, _url, _thumb_url;
	QString _content_type, _content_url;
	int _width = 0;
	int _height = 0;
	int _duration = 0;

	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	GameData *_game = nullptr;

	std::unique_ptr<MTPReplyMarkup> _mtpKeyboard;

	ImagePtr _thumb, _locationThumb;

	std::unique_ptr<internal::SendData> sendData;

};

} // namespace InlineBots
