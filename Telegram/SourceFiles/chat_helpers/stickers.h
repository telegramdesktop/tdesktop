/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

namespace Stickers {

constexpr auto DefaultSetId = 0; // for backward compatibility
constexpr auto CustomSetId = 0xFFFFFFFFFFFFFFFFULL;
constexpr auto RecentSetId = 0xFFFFFFFFFFFFFFFEULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto NoneSetId = 0xFFFFFFFFFFFFFFFDULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto CloudRecentSetId = 0xFFFFFFFFFFFFFFFCULL; // for cloud-stored recent stickers
constexpr auto FeaturedSetId = 0xFFFFFFFFFFFFFFFBULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto FavedSetId = 0xFFFFFFFFFFFFFFFAULL; // for cloud-stored faved stickers
constexpr auto MegagroupSetId = 0xFFFFFFFFFFFFFFEFULL; // for setting up megagroup sticker set

using Order = QList<uint64>;
using SavedGifs = QVector<DocumentData*>;
using Pack = QVector<DocumentData*>;
using ByEmojiMap = QMap<EmojiPtr, Pack>;

struct Set {
	Set(
		uint64 id,
		uint64 access,
		const QString &title,
		const QString &shortName,
		int count,
		int32 hash,
		MTPDstickerSet::Flags flags,
		TimeId installDate)
	: id(id)
	, access(access)
	, title(title)
	, shortName(shortName)
	, count(count)
	, hash(hash)
	, flags(flags)
	, installDate(installDate) {
	}
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
};
using Sets = QMap<uint64, Set>;

inline MTPInputStickerSet inputSetId(const Set &set) {
	if (set.id && set.access) {
		return MTP_inputStickerSetID(MTP_long(set.id), MTP_long(set.access));
	}
	return MTP_inputStickerSetShortName(MTP_string(set.shortName));
}

void ApplyArchivedResult(const MTPDmessages_stickerSetInstallResultArchive &d);
bool ApplyArchivedResultFake(); // For testing.
void InstallLocally(uint64 setId);
void UndoInstallLocally(uint64 setId);
bool IsFaved(not_null<DocumentData*> document);
void SetFaved(not_null<DocumentData*> document, bool faved);

void SetsReceived(const QVector<MTPStickerSet> &data, int32 hash);
void SpecialSetReceived(
	uint64 setId,
	const QString &setTitle,
	const QVector<MTPDocument> &items,
	int32 hash,
	const QVector<MTPStickerPack> &packs = QVector<MTPStickerPack>(),
	const QVector<MTPint> &usageDates = QVector<MTPint>());
void FeaturedSetsReceived(
	const QVector<MTPStickerSetCovered> &data,
	const QVector<MTPlong> &unread,
	int32 hash);
void GifsReceived(const QVector<MTPDocument> &items, int32 hash);

std::vector<not_null<DocumentData*>> GetListByEmoji(
	not_null<EmojiPtr> emoji,
	uint64 seed);
base::optional<std::vector<not_null<EmojiPtr>>> GetEmojiListFromSet(
	not_null<DocumentData*> document);

Set *FeedSet(const MTPDstickerSet &data);
Set *FeedSetFull(const MTPmessages_StickerSet &data);

QString GetSetTitle(const MTPDstickerSet &s);

RecentStickerPack &GetRecentPack();

void IncrementRecentHashtag(RecentHashtagPack &recent, const QString &tag);

} // namespace Stickers
