/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QPolygonF>
#include <QtGui/QTransform>

namespace Data {
struct StoryArea;
} // namespace Data

namespace Wallet {

// CardFold is expressed entirely in Content coordinates: `rest` is the
// card's rest rect there, `quad` the folded quadrilateral it is painted
// as, and `transform` maps the first onto the second. A Card paints with
// a widget-local painter, so it reconciles with p.translate(-x(), -y())
// before applying `transform` and draws into `rest`.
struct CardFold {
	QRect rest;
	QPolygonF quad;
	QTransform transform;
	float64 topY = 0.;
	float64 bottomY = 0.;
	float64 opacity = 1.;
	float64 fold = 0.;
	bool valid = false;
};

struct CardTextLayout {
	style::font addressFont;
	int addressBaseline = 0;
	int stripLeft = 0;
	int nameLeft = 0;
	int nameTop = 0;
	int nameMax = 0;
};

struct CardSpace {
	QSize card;
	std::vector<QRectF> guarded;
	std::vector<QRectF> stickers;
	float64 touch = 0.;
	int ratio = 1;
};

enum class CardDropKind : uchar {
	InPlace,
	Moved,
	Chip,
};

struct CardDrop {
	QPointF center;
	float64 side = 0.;
	CardDropKind kind = CardDropKind::InPlace;
};

[[nodiscard]] QRect CardQrRect(int cardWidth);
[[nodiscard]] CardTextLayout ComputeCardTextLayout(
	QSize card,
	const style::font &nameFont);
[[nodiscard]] QRect CardNameRect(
	const CardTextLayout &layout,
	int nameWidth,
	int nameHeight);

[[nodiscard]] CardFold ComputeCardFold(QRect cardRest, float64 fold);
[[nodiscard]] QPolygonF CardFoldOutline(const CardFold &fold);
[[nodiscard]] QPointF CardFoldPoint(const CardFold &fold, QPointF local);
[[nodiscard]] std::optional<QPointF> CardFoldLocal(
	const CardFold &fold,
	QPointF point);
[[nodiscard]] float64 CardFoldScale(const CardFold &fold, QPointF local);

[[nodiscard]] QPointF CardStickerCenter(
	const Data::StoryArea &area,
	QSize card);
[[nodiscard]] float64 CardStickerRotation(const Data::StoryArea &area);
[[nodiscard]] int CardStickerSide(const Data::StoryArea &area, QSize card);
[[nodiscard]] int CardStickerOutline(int frameSide);
[[nodiscard]] float64 CardStickerOuterSide(int side, int ratio);
[[nodiscard]] float64 CardStickerMinimumSide(float64 side, QSize card);
[[nodiscard]] bool CardStickerContains(
	const Data::StoryArea &area,
	QSize card,
	int ratio,
	QPointF local);
[[nodiscard]] Data::StoryArea CardStickerArea(
	QPointF center,
	float64 side,
	float64 rotation,
	QSize card);

[[nodiscard]] CardSpace ComputeCardSpace(
	QSize card,
	const std::vector<QRectF> &protectedAreas,
	const std::vector<Data::StoryArea> &stickers,
	float64 touch,
	int ratio);
[[nodiscard]] float64 FreeSideAt(
	const CardSpace &space,
	QPointF center,
	float64 limit,
	float64 rotation);
[[nodiscard]] std::optional<Data::StoryArea> AutoPlace(
	const CardSpace &space);
[[nodiscard]] std::optional<CardDrop> ResolveCardDrop(
	const CardSpace &space,
	QPointF center);
[[nodiscard]] std::optional<CardDrop> ResolveCardMove(
	const CardSpace &space,
	QPointF center,
	float64 side,
	float64 rotation);

} // namespace Wallet
