/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

enum class StickerType : uchar;

namespace base {
template <typename Enum>
class Flags;
} // namespace base

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Media::Clip {
class ReaderPointer;
enum class Notification;
} // namespace Media::Clip

namespace Lottie {
class SinglePlayer;
class MultiPlayer;
class FrameRenderer;
class Animation;
enum class Quality : char;
struct ColorReplacements;
} // namespace Lottie

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PathShiftGradient;
} // namespace Ui

namespace Data {
class DocumentMedia;
class StickersSetThumbnailView;
enum class StickersSetFlag : ushort;
using StickersSetFlags = base::flags<StickersSetFlag>;
} // namespace Data

namespace ChatHelpers {

enum class StickerLottieSize : uint8 {
	MessageHistory,
	StickerSet, // In Emoji used for forum topic profile cover icons.
	StickersPanel,
	StickersFooter,
	SetsListThumbnail,
	InlineResults,
	EmojiInteraction,
	EmojiInteractionReserved1,
	EmojiInteractionReserved2,
	EmojiInteractionReserved3,
	EmojiInteractionReserved4,
	EmojiInteractionReserved5,
	EmojiInteractionReserved6,
	EmojiInteractionReserved7,
	ChatIntroHelloSticker,
	StickerEmojiSize,
};
[[nodiscard]] uint8 LottieCacheKeyShift(
	uint8 replacementsTag,
	StickerLottieSize sizeTag);

[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
	not_null<Data::DocumentMedia*> media,
	StickerLottieSize sizeTag,
	QSize box,
	Lottie::Quality quality = Lottie::Quality(),
	std::shared_ptr<Lottie::FrameRenderer> renderer = nullptr);
[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
	not_null<Data::DocumentMedia*> media,
	const Lottie::ColorReplacements *replacements,
	StickerLottieSize sizeTag,
	QSize box,
	Lottie::Quality quality = Lottie::Quality(),
	std::shared_ptr<Lottie::FrameRenderer> renderer = nullptr);
[[nodiscard]] not_null<Lottie::Animation*> LottieAnimationFromDocument(
	not_null<Lottie::MultiPlayer*> player,
	not_null<Data::DocumentMedia*> media,
	StickerLottieSize sizeTag,
	QSize box);

[[nodiscard]] bool HasLottieThumbnail(
	StickerType thumbType,
	Data::StickersSetThumbnailView *thumb,
	Data::DocumentMedia *media);
[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> LottieThumbnail(
	Data::StickersSetThumbnailView *thumb,
	Data::DocumentMedia *media,
	StickerLottieSize sizeTag,
	QSize box,
	std::shared_ptr<Lottie::FrameRenderer> renderer = nullptr);

[[nodiscard]] bool HasWebmThumbnail(
	StickerType thumbType,
	Data::StickersSetThumbnailView *thumb,
	Data::DocumentMedia *media);
[[nodiscard]] Media::Clip::ReaderPointer WebmThumbnail(
	Data::StickersSetThumbnailView *thumb,
	Data::DocumentMedia *media,
	Fn<void(Media::Clip::Notification)> callback);

bool PaintStickerThumbnailPath(
	QPainter &p,
	not_null<Data::DocumentMedia*> media,
	QRect target,
	QLinearGradient *gradient = nullptr,
	bool mirrorHorizontal = false);

bool PaintStickerThumbnailPath(
	QPainter &p,
	not_null<Data::DocumentMedia*> media,
	QRect target,
	not_null<Ui::PathShiftGradient*> gradient,
	bool mirrorHorizontal = false);

[[nodiscard]] QSize ComputeStickerSize(
	not_null<DocumentData*> document,
	QSize box);

} // namespace ChatHelpers
