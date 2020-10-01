/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_photo.h"
#include "data/data_document.h"

class ChannelData;

namespace Data {
class Session;
} // namespace Data

enum class WebPageType {
	Photo,
	Video,
	Profile,
	WallPaper,
	Theme,
	Article,
	ArticleWithIV,
};

WebPageType ParseWebPageType(const MTPDwebPage &type);

struct WebPageCollage {
	using Item = std::variant<PhotoData*, DocumentData*>;

	WebPageCollage() = default;
	explicit WebPageCollage(
		not_null<Data::Session*> owner,
		const MTPDwebPage &data);

	std::vector<Item> items;

};

struct WebPageData {
	WebPageData(not_null<Data::Session*> owner, const WebPageId &id);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	bool applyChanges(
		WebPageType newType,
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

	static void ApplyChanges(
		not_null<Main::Session*> session,
		ChannelData *channel,
		const MTPmessages_Messages &result);

	WebPageId id = 0;
	WebPageType type = WebPageType::Article;
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

	const not_null<Data::Session*> _owner;

};
