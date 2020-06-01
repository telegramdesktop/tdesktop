/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class DocumentData;

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Lottie {
class SinglePlayer;
class MultiPlayer;
class FrameRenderer;
class Animation;
enum class Quality : char;
struct ColorReplacements;
} // namespace Lottie

namespace Stickers {

constexpr auto DefaultSetId = 0; // for backward compatibility
constexpr auto CustomSetId = 0xFFFFFFFFFFFFFFFFULL;
constexpr auto RecentSetId = 0xFFFFFFFFFFFFFFFEULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto NoneSetId = 0xFFFFFFFFFFFFFFFDULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto CloudRecentSetId = 0xFFFFFFFFFFFFFFFCULL; // for cloud-stored recent stickers
constexpr auto FeaturedSetId = 0xFFFFFFFFFFFFFFFBULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto FavedSetId = 0xFFFFFFFFFFFFFFFAULL; // for cloud-stored faved stickers
constexpr auto MegagroupSetId = 0xFFFFFFFFFFFFFFEFULL; // for setting up megagroup sticker set

class Set;
class SetThumbnailView;

void ApplyArchivedResult(const MTPDmessages_stickerSetInstallResultArchive &d);
bool ApplyArchivedResultFake(); // For testing.
void InstallLocally(uint64 setId);
void UndoInstallLocally(uint64 setId);
bool IsFaved(not_null<const DocumentData*> document);
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
	const QVector<MTPStickerSetCovered> &list,
	const QVector<MTPlong> &unread,
	int32 hash);
void GifsReceived(const QVector<MTPDocument> &items, int32 hash);

std::vector<not_null<DocumentData*>> GetListByEmoji(
	not_null<Main::Session*> session,
	not_null<EmojiPtr> emoji,
	uint64 seed);
std::optional<std::vector<not_null<EmojiPtr>>> GetEmojiListFromSet(
	not_null<DocumentData*> document);

Set *FeedSet(const MTPDstickerSet &data);
Set *FeedSetFull(const MTPmessages_StickerSet &data);
void NewSetReceived(const MTPmessages_StickerSet &data);

QString GetSetTitle(const MTPDstickerSet &s);

RecentStickerPack &GetRecentPack();

enum class LottieSize : uchar {
	MessageHistory,
	StickerSet,
	StickersPanel,
	StickersFooter,
	SetsListThumbnail,
	InlineResults,
};

[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
	not_null<Data::DocumentMedia*> media,
	LottieSize sizeTag,
	QSize box,
	Lottie::Quality quality = Lottie::Quality(),
	std::shared_ptr<Lottie::FrameRenderer> renderer = nullptr);
[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
	not_null<Data::DocumentMedia*> media,
	const Lottie::ColorReplacements *replacements,
	LottieSize sizeTag,
	QSize box,
	Lottie::Quality quality = Lottie::Quality(),
	std::shared_ptr<Lottie::FrameRenderer> renderer = nullptr);
[[nodiscard]] not_null<Lottie::Animation*> LottieAnimationFromDocument(
	not_null<Lottie::MultiPlayer*> player,
	not_null<Data::DocumentMedia*> media,
	LottieSize sizeTag,
	QSize box);

[[nodiscard]] bool HasLottieThumbnail(
	SetThumbnailView *thumb,
	Data::DocumentMedia *media);
[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> LottieThumbnail(
	SetThumbnailView *thumb,
	Data::DocumentMedia *media,
	LottieSize sizeTag,
	QSize box,
	std::shared_ptr<Lottie::FrameRenderer> renderer = nullptr);

} // namespace Stickers
