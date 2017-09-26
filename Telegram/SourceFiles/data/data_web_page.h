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

#include "data/data_photo.h"
#include "data/data_document.h"

enum WebPageType {
	WebPagePhoto,
	WebPageVideo,
	WebPageProfile,
	WebPageArticle
};

inline WebPageType toWebPageType(const QString &type) {
	if (type == qstr("photo")) return WebPagePhoto;
	if (type == qstr("video")) return WebPageVideo;
	if (type == qstr("profile")) return WebPageProfile;
	return WebPageArticle;
}

struct WebPageData {
	WebPageData(const WebPageId &id) : id(id) {
	}
	WebPageData(
		const WebPageId &id,
		WebPageType type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		DocumentData *document,
		PhotoData *photo,
		int32 duration,
		const QString &author,
		int32 pendingTill)
	: id(id)
	, type(type)
	, url(url)
	, displayUrl(displayUrl)
	, siteName(siteName)
	, title(title)
	, description(description)
	, duration(duration)
	, author(author)
	, photo(photo)
	, document(document)
	, pendingTill(pendingTill) {
	}

	void forget() {
		if (document) document->forget();
		if (photo) photo->forget();
	}

	WebPageId id = 0;
	WebPageType type = WebPageArticle;
	QString url;
	QString displayUrl;
	QString siteName;
	QString title;
	TextWithEntities description;
	int32 duration = 0;
	QString author;
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	int32 pendingTill = 0;

};
