/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_file.h"

class DocumentData;
enum class StickerType : uchar;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

using StickersSetsOrder = QList<uint64>;
using SavedGifs = QVector<DocumentData*>;
using StickersPack = QVector<DocumentData*>;

enum class StickersType : uchar;

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

enum class StickersSetFlag : ushort {
	Installed = (1 << 0),
	Archived = (1 << 1),
	Masks = (1 << 2),
	Official = (1 << 3),
	NotLoaded = (1 << 4),
	Featured = (1 << 5),
	Unread = (1 << 6),
	Special = (1 << 7),
	Emoji = (1 << 9),
	TextColor = (1 << 10),
	ChannelStatus = (1 << 11),
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
		uint64 accessHash,
		uint64 hash,
		const QString &title,
		const QString &shortName,
		int count,
		StickersSetFlags flags,
		TimeId installDate);
	~StickersSet();

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] MTPInputStickerSet mtpInput() const;
	[[nodiscard]] StickerSetIdentifier identifier() const;
	[[nodiscard]] StickersType type() const;
	[[nodiscard]] bool textColor() const;
	[[nodiscard]] bool channelStatus() const;

	void setThumbnail(const ImageWithLocation &data, StickerType type);

	[[nodiscard]] bool hasThumbnail() const;
	[[nodiscard]] StickerType thumbnailType() const;
	[[nodiscard]] bool thumbnailLoading() const;
	[[nodiscard]] bool thumbnailFailed() const;
	void loadThumbnail();
	[[nodiscard]] const ImageLocation &thumbnailLocation() const;
	[[nodiscard]] Storage::Cache::Key thumbnailBigFileBaseCacheKey() const;
	[[nodiscard]] int thumbnailByteSize() const;
	[[nodiscard]] DocumentData *lookupThumbnailDocument() const;

	[[nodiscard]] std::shared_ptr<StickersSetThumbnailView> createThumbnailView();
	[[nodiscard]] std::shared_ptr<StickersSetThumbnailView> activeThumbnailView();

	uint64 id = 0;
	uint64 accessHash = 0;
	uint64 hash = 0;
	DocumentId thumbnailDocumentId = 0;
	QString title, shortName;
	int count = 0;
	int locked = 0;
	StickersSetFlags flags;

private:
	StickerType _thumbnailType = {};

public:
	TimeId installDate = 0;
	StickersPack covers;
	StickersPack stickers;
	std::vector<TimeId> dates;
	base::flat_map<EmojiPtr, StickersPack> emoji;

private:
	const not_null<Data::Session*> _owner;

	CloudFile _thumbnail;
	std::weak_ptr<StickersSetThumbnailView> _thumbnailView;

};

[[nodiscard]] MTPInputStickerSet InputStickerSet(StickerSetIdentifier id);
[[nodiscard]] StickerSetIdentifier FromInputSet(const MTPInputStickerSet &id);

} // namespace Data
