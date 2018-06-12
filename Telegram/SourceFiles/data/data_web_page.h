/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
		int duration,
		const QString &author,
		int pendingTill)
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

	bool applyChanges(
		const QString &newType,
		const QString &newUrl,
		const QString &newDisplayUrl,
		const QString &newSiteName,
		const QString &newTitle,
		const TextWithEntities &newDescription,
		PhotoData *newPhoto,
		DocumentData *newDocument,
		int newDuration,
		const QString &newAuthor,
		int newPendingTill);

	WebPageId id = 0;
	WebPageType type = WebPageArticle;
	QString url;
	QString displayUrl;
	QString siteName;
	QString title;
	TextWithEntities description;
	int duration = 0;
	QString author;
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	int pendingTill = 0;
	int version = 0;

};
