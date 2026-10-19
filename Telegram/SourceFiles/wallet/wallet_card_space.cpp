/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_space.h"

#include "data/data_story.h"

#include <QtCore/QLineF>
#include <QtGui/QPainterPath>

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kCardFoldMinHeight = 1.;
constexpr auto kOutlineRatio = 0.03;
constexpr auto kAnchorX = 0.55;
constexpr auto kAnchorY = 0.70;
constexpr auto kPreferredSide = 0.21;
constexpr auto kMinDropSide = 0.12;

[[nodiscard]] float64 OuterHalf(int side, int ratio) {
	return side / 2. + CardStickerOutline(side * ratio) / float64(ratio);
}

[[nodiscard]] QPolygonF FoldQuad(
		QRect cardRest,
		float64 fold,
		float64 topY,
		float64 bottomY) {
	const auto bottomWidth = cardRest.width()
		* (1. - (1. - st::walletCardFoldBottomScale) * fold);
	const auto topWidth = bottomWidth
		* (1. - (1. - st::walletCardFoldTopScale) * fold);
	const auto center = cardRest.left() + cardRest.width() / 2.;
	return QPolygonF({
		QPointF(center - topWidth / 2., topY),
		QPointF(center + topWidth / 2., topY),
		QPointF(center + bottomWidth / 2., bottomY),
		QPointF(center - bottomWidth / 2., bottomY),
	});
}

} // namespace

QRect CardQrRect(int cardWidth) {
	return QRect(
		cardWidth - st::walletCardQrRight - st::walletCardQrSize.width(),
		st::walletCardQrTop,
		st::walletCardQrSize.width(),
		st::walletCardQrSize.height());
}

CardTextLayout ComputeCardTextLayout(
		QSize card,
		const style::font &nameFont) {
	auto result = CardTextLayout{
		.addressFont = st::walletCardAddressFont->monospace(),
	};
	const auto &font = result.addressFont;
	result.addressBaseline = card.width()
		- st::walletCardAddressRight
		- font->height
		- font->ascent;
	result.stripLeft = result.addressBaseline - font->descent;
	result.nameTop = card.height()
		- st::walletCardNameBottom
		- nameFont->ascent;
	result.nameMax = result.stripLeft
		- st::walletCardContentSkip
		- st::walletCardContentLeft;
	result.nameLeft = st::walletCardContentLeft;
	return result;
}

CardFold ComputeCardFold(QRect cardRest, float64 fold) {
	auto result = CardFold();
	result.rest = cardRest;
	result.fold = fold;
	result.topY = cardRest.top() - cardRest.top() * fold;
	result.bottomY = result.topY + cardRest.height() * (1. - fold);
	result.opacity = 1. - std::pow(fold, st::walletCardFoldFadePower);
	if (result.bottomY - result.topY < kCardFoldMinHeight) {
		return result;
	}
	const auto rest = QRectF(cardRest);
	const auto restQuad = QPolygonF({
		rest.topLeft(),
		rest.topRight(),
		rest.bottomRight(),
		rest.bottomLeft(),
	});
	auto quad = FoldQuad(cardRest, fold, result.topY, result.bottomY);
	auto transform = QTransform();
	if (!QTransform::quadToQuad(restQuad, quad, transform)) {
		return result;
	}
	result.quad = std::move(quad);
	result.transform = transform;
	result.valid = true;
	return result;
}

QPolygonF CardFoldOutline(const CardFold &fold) {
	if (!fold.valid) {
		return QPolygonF();
	}
	auto path = QPainterPath();
	path.addRoundedRect(
		QRectF(fold.rest),
		st::walletCardRadius,
		st::walletCardRadius);
	return fold.transform.map(path.toFillPolygon());
}

QPointF CardFoldPoint(const CardFold &fold, QPointF local) {
	return fold.transform.map(QPointF(fold.rest.topLeft()) + local);
}

QPointF CardFoldLimitPoint(const CardFold &fold, QPointF local) {
	if (fold.valid) {
		return CardFoldPoint(fold, local);
	}
	const auto &rest = fold.rest;
	const auto quad = FoldQuad(rest, fold.fold, fold.topY, fold.bottomY);
	const auto x = rest.width() ? (local.x() / rest.width()) : 0.;
	const auto y = rest.height() ? (local.y() / rest.height()) : 0.;
	const auto top = quad[0] + (quad[1] - quad[0]) * x;
	const auto bottom = quad[3] + (quad[2] - quad[3]) * x;
	return top + (bottom - top) * y;
}

std::optional<QPointF> CardFoldLocal(const CardFold &fold, QPointF point) {
	if (!fold.valid
		|| !std::isfinite(point.x())
		|| !std::isfinite(point.y())) {
		return std::nullopt;
	}
	auto invertible = false;
	const auto inverted = fold.transform.inverted(&invertible);
	if (!invertible) {
		return std::nullopt;
	}
	const auto mapped = inverted.map(point);
	if (!std::isfinite(mapped.x()) || !std::isfinite(mapped.y())) {
		return std::nullopt;
	}
	return mapped - QPointF(fold.rest.topLeft());
}

float64 CardFoldScale(const CardFold &fold, QPointF local) {
	const auto half = QPointF(0.5, 0.);
	return QLineF(
		CardFoldPoint(fold, local - half),
		CardFoldPoint(fold, local + half)).length();
}

QPointF CardStickerCenter(const Data::StoryArea &area, QSize card) {
	const auto center = area.geometry.center();
	return QPointF(center.x() * card.width(), center.y() * card.height());
}

float64 CardStickerRotation(const Data::StoryArea &area) {
	return std::isfinite(area.rotation)
		? std::remainder(area.rotation, 360.)
		: 0.;
}

int CardStickerSide(const Data::StoryArea &area, QSize card) {
	const auto limit = float64(std::min(card.width(), card.height()));
	const auto side = std::min(
		area.geometry.width() * card.width(),
		area.geometry.height() * card.height());
	return (side > 0.)
		? int(base::SafeRound(std::min(side, limit)))
		: 0;
}

int CardStickerOutline(int frameSide) {
	return std::max(1, int(base::SafeRound(frameSide * kOutlineRatio)));
}

float64 CardStickerOuterSide(int side, int ratio) {
	return (side > 0) ? (2. * OuterHalf(side, std::max(ratio, 1))) : 0.;
}

float64 CardStickerMinimumSide(float64 side, QSize card) {
	return (card.isEmpty() || !std::isfinite(side) || side <= 0.)
		? 0.
		: std::min(side, card.width() * kMinDropSide);
}

float64 CardStickerPreferredSide(QSize card) {
	return card.isEmpty() ? 0. : (card.width() * kPreferredSide);
}

std::optional<QPointF> CardStickerClampedCenter(QPointF center, QSize card) {
	if (card.isEmpty()
		|| !std::isfinite(center.x())
		|| !std::isfinite(center.y())) {
		return std::nullopt;
	}
	return QPointF(
		std::clamp(center.x(), 0., float64(card.width())),
		std::clamp(center.y(), 0., float64(card.height())));
}

bool CardStickerContains(
		const Data::StoryArea &area,
		QSize card,
		int ratio,
		QPointF local) {
	const auto side = CardStickerSide(area, card);
	const auto center = CardStickerCenter(area, card);
	if (side <= 0
		|| !std::isfinite(local.x())
		|| !std::isfinite(local.y())
		|| !std::isfinite(center.x())
		|| !std::isfinite(center.y())) {
		return false;
	}
	const auto offset = QTransform().rotate(
		-CardStickerRotation(area)
	).map(local - center);
	const auto half = CardStickerOuterSide(side, ratio) / 2.;
	return (std::abs(offset.x()) <= half) && (std::abs(offset.y()) <= half);
}

Data::StoryArea CardStickerArea(
		QPointF center,
		float64 side,
		float64 rotation,
		QSize card) {
	const auto width = float64(card.width());
	const auto height = float64(card.height());
	return {
		.geometry = QRectF(
			(center.x() - side / 2.) / width,
			(center.y() - side / 2.) / height,
			side / width,
			side / height),
		.rotation = rotation,
	};
}

std::optional<Data::StoryArea> CardStickerDefaultArea(QSize card) {
	if (card.isEmpty()) {
		return std::nullopt;
	}
	return CardStickerArea(
		QPointF(card.width() * kAnchorX, card.height() * kAnchorY),
		CardStickerPreferredSide(card),
		0.,
		card);
}

} // namespace Wallet
