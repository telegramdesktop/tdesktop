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

struct WebPageCollage {
	using Item = base::variant<PhotoData*, DocumentData*>;

	WebPageCollage() = default;
	explicit WebPageCollage(const MTPDwebPage &data);

	std::vector<Item> items;

};

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
		PhotoData *photo,
		DocumentData *document,
		WebPageCollage &&collage,
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
	, collage(std::move(collage))
	, pendingTill(pendingTill) {
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
		WebPageCollage &&newCollage,
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
	WebPageCollage collage;
	int pendingTill = 0;
	int version = 0;

private:
	void replaceDocumentGoodThumbnail();

};
