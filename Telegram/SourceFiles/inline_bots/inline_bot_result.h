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
		Venue,
	};

	// Constructor is public only for MakeUnique<>() to work.
	// You should use create() static method instead.
	explicit Result(const Creator &creator);
	static UniquePointer<Result> create(uint64 queryId, const MTPBotInlineResult &mtpData);
	Result(const Result &other) = delete;
	Result &operator=(const Result &other) = delete;

	uint64 queryId;
	QString id;
	Type type;
	DocumentData *document = nullptr;
	PhotoData *photo = nullptr;
	QString title, description, url, thumb_url;
	QString content_type, content_url;
	int width = 0;
	int height = 0;
	int duration = 0;

	ImagePtr thumb;

	void automaticLoadGif();
	void automaticLoadSettingsChangedGif();
	void saveFile(const QString &toFile, LoadFromCloudSetting fromCloud, bool autoLoading);
	void cancelFile();

	QByteArray data() const;
	bool loading() const;
	bool loaded() const;
	bool displayLoading() const;
	void forget();
	float64 progress() const;

	bool hasThumbDisplay() const;

	void addToHistory(History *history, MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate, UserId viaBotId, MsgId replyToId) const;

	// interface for Layout:: usage
	bool getLocationCoords(LocationCoords *outLocation) const;
	QString getLayoutTitle() const;
	QString getLayoutDescription() const;

	~Result();

private:
	struct Creator {
		uint64 queryId;
		Type type;
	};

	UniquePointer<internal::SendData> sendData;

	QByteArray _data;
	mutable webFileLoader *_loader = nullptr;

};
Result *getResultFromLoader(FileLoader *loader);

} // namespace InlineBots
