/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_photo.h"
#include "data/data_document.h"

struct GameData {
	GameData(const GameId &id) : id(id) {
	}
	GameData(
		const GameId &id,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document)
	: id(id)
	, accessHash(accessHash)
	, shortName(shortName)
	, title(title)
	, description(description)
	, photo(photo)
	, document(document) {
	}

	GameId id = 0;
	uint64 accessHash = 0;
	QString shortName;
	QString title;
	QString description;
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;

};
