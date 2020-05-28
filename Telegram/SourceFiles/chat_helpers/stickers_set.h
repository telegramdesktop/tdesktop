/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_file.h"

class DocumentData;

namespace Data {
class Session;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Stickers {

using Order = QList<uint64>;
using SavedGifs = QVector<DocumentData*>;
using Pack = QVector<DocumentData*>;
using ByEmojiMap = QMap<EmojiPtr, Pack>;

class Set;
using Sets = base::flat_map<uint64, std::unique_ptr<Set>>;

class SetThumbnailView final {
public:
	explicit SetThumbnailView(not_null<Set*> owner);

	[[nodiscard]] not_null<Set*> owner() const;

	void set(not_null<Main::Session*> session, QByteArray content);

	[[nodiscard]] Image *image() const;
	[[nodiscard]] QByteArray content() const;

private:
	const not_null<Set*> _owner;
	std::unique_ptr<Image> _image;
	QByteArray _content;

};

class Set final {
public:
	Set(
		not_null<Data::Session*> owner,
		uint64 id,
		uint64 access,
		const QString &title,
		const QString &shortName,
		int count,
		int32 hash,
		MTPDstickerSet::Flags flags,
		TimeId installDate);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] MTPInputStickerSet mtpInput() const;

	void setThumbnail(const ImageWithLocation &data);

	[[nodiscard]] bool hasThumbnail() const;
	[[nodiscard]] bool thumbnailLoading() const;
	[[nodiscard]] bool thumbnailFailed() const;
	void loadThumbnail();
	[[nodiscard]] const ImageLocation &thumbnailLocation() const;
	[[nodiscard]] int thumbnailByteSize() const;

	[[nodiscard]] std::shared_ptr<SetThumbnailView> createThumbnailView();
	[[nodiscard]] std::shared_ptr<SetThumbnailView> activeThumbnailView();

	uint64 id = 0;
	uint64 access = 0;
	QString title, shortName;
	int count = 0;
	int32 hash = 0;
	MTPDstickerSet::Flags flags;
	TimeId installDate = 0;
	Pack stickers;
	std::vector<TimeId> dates;
	Pack covers;
	ByEmojiMap emoji;

private:
	const not_null<Data::Session*> _owner;

	Data::CloudFile _thumbnail;
	std::weak_ptr<SetThumbnailView> _thumbnailView;

};

} // namespace Stickers
