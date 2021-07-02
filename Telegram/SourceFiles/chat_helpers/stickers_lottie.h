/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PathShiftGradient;
} // namespace Ui

namespace Data {
class DocumentMedia;
class StickersSetThumbnailView;
} // namespace Data

namespace ChatHelpers {

enum class StickerLottieSize : uchar {
	MessageHistory,
	StickerSet,
	StickersPanel,
	StickersFooter,
	SetsListThumbnail,
	InlineResults,
};

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
	Data::StickersSetThumbnailView *thumb,
	Data::DocumentMedia *media);
[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> LottieThumbnail(
	Data::StickersSetThumbnailView *thumb,
	Data::DocumentMedia *media,
	StickerLottieSize sizeTag,
	QSize box,
	std::shared_ptr<Lottie::FrameRenderer> renderer = nullptr);

bool PaintStickerThumbnailPath(
	QPainter &p,
	not_null<Data::DocumentMedia*> media,
	QRect target,
	QLinearGradient *gradient = nullptr);

bool PaintStickerThumbnailPath(
	QPainter &p,
	not_null<Data::DocumentMedia*> media,
	QRect target,
	not_null<Ui::PathShiftGradient*> gradient);

} // namespace ChatHelpers
