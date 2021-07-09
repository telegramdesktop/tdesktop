/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_file.h"

class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

using StickersSetsOrder = QList<uint64>;
using SavedGifs = QVector<DocumentData*>;
using StickersPack = QVector<DocumentData*>;
using StickersByEmojiMap = QMap<EmojiPtr, StickersPack>;

class StickersSet;
using StickersSets = base::flat_map<uint64, std::unique_ptr<StickersSet>>;

class StickersSetThumbnailView final {
public:
	explicit StickersSetThumbnailView(not_null<StickersSet*> owner);

	[[nodiscard]] not_null<StickersSet*> owner() const;

	void set(not_null<Main::Session*> session, QByteArray content);

	[[nodiscard]] Image *image() const;
	[[nodiscard]] QByteArray content() const;

private:
	const not_null<StickersSet*> _owner;
	std::unique_ptr<Image> _image;
	QByteArray _content;

};

enum class StickersSetFlag {
	Installed = (1 << 0),
	Archived = (1 << 1),
	Masks = (1 << 2),
	Official = (1 << 3),
	NotLoaded = (1 << 4),
	Featured = (1 << 5),
	Unread = (1 << 6),
	Special = (1 << 7),
};
inline constexpr bool is_flag_type(StickersSetFlag) { return true; };
using StickersSetFlags = base::flags<StickersSetFlag>;

[[nodiscard]] StickersSetFlags ParseStickersSetFlags(
	const MTPDstickerSet &data);

class StickersSet final {
public:
	StickersSet(
		not_null<Data::Session*> owner,
		uint64 id,
		uint64 access,
		const QString &title,
		const QString &shortName,
		int count,
		int32 hash,
		StickersSetFlags flags,
		TimeId installDate);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] MTPInputStickerSet mtpInput() const;
	[[nodiscard]] StickerSetIdentifier identifier() const;

	void setThumbnail(const ImageWithLocation &data);

	[[nodiscard]] bool hasThumbnail() const;
	[[nodiscard]] bool thumbnailLoading() const;
	[[nodiscard]] bool thumbnailFailed() const;
	void loadThumbnail();
	[[nodiscard]] const ImageLocation &thumbnailLocation() const;
	[[nodiscard]] int thumbnailByteSize() const;

	[[nodiscard]] std::shared_ptr<StickersSetThumbnailView> createThumbnailView();
	[[nodiscard]] std::shared_ptr<StickersSetThumbnailView> activeThumbnailView();

	uint64 id = 0;
	uint64 access = 0;
	QString title, shortName;
	int count = 0;
	int32 hash = 0;
	StickersSetFlags flags;
	TimeId installDate = 0;
	StickersPack covers;
	StickersPack stickers;
	std::vector<TimeId> dates;
	StickersByEmojiMap emoji;

private:
	const not_null<Data::Session*> _owner;

	CloudFile _thumbnail;
	std::weak_ptr<StickersSetThumbnailView> _thumbnailView;

};

[[nodiscard]] MTPInputStickerSet InputStickerSet(StickerSetIdentifier id);
[[nodiscard]] StickerSetIdentifier FromInputSet(const MTPInputStickerSet &id);

} // namespace Data
