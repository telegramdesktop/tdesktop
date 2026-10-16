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
constexpr auto kLatticeStep = 0.005;
constexpr auto kLeftWeight = 1.5;
constexpr auto kMinDropSide = 0.12;
constexpr auto kMinAutoSide = 0.07;
constexpr auto kChipSide = 8. / 7.;
constexpr auto kChipSink = 0.15;
constexpr auto kChipRotation = 0.5;
constexpr auto kEpsilon = 1e-9;

struct Candidate {
	QPointF center;
	float64 side = 0.;
	float64 overlap = 0.;
	float64 distance = 0.;
	int painted = 0;
	int row = 0;
	int column = 0;
};

[[nodiscard]] float64 RotationSpan(float64 degrees) {
	const auto radians = std::isfinite(degrees)
		? qDegreesToRadians(degrees)
		: 0.;
	return std::abs(std::cos(radians)) + std::abs(std::sin(radians));
}

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

[[nodiscard]] QRectF Square(QPointF center, float64 half) {
	return QRectF(
		center - QPointF(half, half),
		QSizeF(2. * half, 2. * half));
}

[[nodiscard]] QRectF Footprint(
		const Data::StoryArea &area,
		QSize card,
		int ratio) {
	const auto side = CardStickerSide(area, card);
	if (side <= 0) {
		return QRectF();
	}
	const auto span = RotationSpan(CardStickerRotation(area));
	return Square(
		CardStickerCenter(area, card),
		OuterHalf(side, ratio) * span);
}

[[nodiscard]] QRectF GuardedCore(const QRectF &rect, float64 touch) {
	const auto core = rect.adjusted(touch, touch, -touch, -touch);
	return core.isEmpty() ? rect : core;
}

[[nodiscard]] float64 Clearance(const QRectF &box, QPointF point) {
	const auto center = box.center();
	return std::max(
		std::abs(point.x() - center.x()) - box.width() / 2.,
		std::abs(point.y() - center.y()) - box.height() / 2.);
}

[[nodiscard]] float64 Available(
		const CardSpace &space,
		QPointF center,
		bool stickers) {
	auto result = std::min({
		center.x(),
		space.card.width() - center.x(),
		center.y(),
		space.card.height() - center.y(),
	});
	for (const auto &box : space.guarded) {
		result = std::min(result, Clearance(box, center));
	}
	if (stickers) {
		for (const auto &box : space.stickers) {
			result = std::min(result, Clearance(box, center));
		}
	}
	return result;
}

[[nodiscard]] float64 FittedSide(
		float64 available,
		float64 limit,
		float64 span,
		int ratio) {
	const auto most = (limit > 0.) ? int(base::SafeRound(limit)) : 0;
	const auto fits = [&](int side) {
		return OuterHalf(side, ratio) * span <= available + kEpsilon;
	};
	if (most > 0 && fits(most)) {
		return limit;
	}
	const auto bound = (available > 0.) ? int(2. * available / span) : 0;
	for (auto side = std::min(most - 1, bound); side > 0; --side) {
		if (fits(side)) {
			return float64(side);
		}
	}
	return 0.;
}

[[nodiscard]] float64 Overlap(
		const CardSpace &space,
		QPointF center,
		int side) {
	const auto box = Square(center, OuterHalf(side, space.ratio));
	auto result = 0.;
	for (const auto &sticker : space.stickers) {
		const auto common = box.intersected(sticker);
		if (!common.isEmpty()) {
			result += common.width() * common.height();
		}
	}
	return result;
}

[[nodiscard]] bool Better(const Candidate &a, const Candidate &b) {
	if (std::abs(a.overlap - b.overlap) > kEpsilon) {
		return a.overlap < b.overlap;
	} else if (a.painted != b.painted) {
		return a.painted > b.painted;
	} else if (a.distance != b.distance) {
		return a.distance < b.distance;
	}
	return (a.row != b.row) ? (a.row < b.row) : (a.column < b.column);
}

[[nodiscard]] std::optional<Candidate> BestCandidate(
		const CardSpace &space,
		bool disjoint) {
	const auto width = float64(space.card.width());
	const auto height = float64(space.card.height());
	const auto anchor = QPointF(width * kAnchorX, height * kAnchorY);
	const auto limit = width * kPreferredSide;
	const auto least = width * kMinAutoSide;
	const auto step = width * kLatticeStep;
	const auto left = int(std::ceil(-anchor.x() / step));
	const auto right = int(std::floor((width - anchor.x()) / step));
	const auto top = int(std::ceil(-anchor.y() / step));
	const auto bottom = int(std::floor((height - anchor.y()) / step));
	auto result = std::optional<Candidate>();
	for (auto row = top; row <= bottom; ++row) {
		for (auto column = left; column <= right; ++column) {
			const auto center = anchor
				+ QPointF(column * step, row * step);
			const auto side = FittedSide(
				Available(space, center, disjoint),
				limit,
				1.,
				space.ratio);
			if (side <= 0. || side < least - kEpsilon) {
				continue;
			}
			const auto painted = int(base::SafeRound(side));
			const auto weighted = (column < 0)
				? (kLeftWeight * column)
				: float64(column);
			const auto candidate = Candidate{
				.center = center,
				.side = side,
				.overlap = disjoint ? 0. : Overlap(space, center, painted),
				.distance = weighted * weighted + float64(row) * row,
				.painted = painted,
				.row = row,
				.column = column,
			};
			if (!result || Better(candidate, *result)) {
				result = candidate;
			}
		}
	}
	return result;
}

[[nodiscard]] bool Nearer(const Candidate &a, const Candidate &b) {
	if (a.distance != b.distance) {
		return a.distance < b.distance;
	} else if (a.painted != b.painted) {
		return a.painted > b.painted;
	}
	return (a.row != b.row) ? (a.row < b.row) : (a.column < b.column);
}

[[nodiscard]] std::optional<CardDrop> ChipDrop(
		const CardSpace &space,
		QPointF center) {
	const auto plate = QRectF(CardQrRect(space.card.width()));
	const auto touch = space.touch;
	if (!plate.adjusted(-touch, -touch, touch, touch).contains(center)) {
		return std::nullopt;
	}
	const auto side = plate.width() * kChipSide;
	const auto painted = int(base::SafeRound(side));
	if (painted <= 0) {
		return std::nullopt;
	}
	const auto bottom = plate.y() + plate.height() * kChipSink;
	const auto chip = QPointF(plate.center().x(), bottom - side / 2.);
	auto others = space;
	std::erase(others.guarded, GuardedCore(plate, touch));
	const auto available = Available(others, chip, true);
	if (OuterHalf(painted, space.ratio) > available + kEpsilon) {
		return std::nullopt;
	}
	return CardDrop{
		.center = chip,
		.side = side,
		.kind = CardDropKind::Chip,
	};
}

[[nodiscard]] std::optional<CardDrop> NearestDrop(
		const CardSpace &space,
		QPointF center,
		float64 limit,
		float64 least,
		float64 span) {
	const auto width = float64(space.card.width());
	const auto height = float64(space.card.height());
	const auto step = width * kLatticeStep;
	const auto left = int(std::ceil(-center.x() / step));
	const auto right = int(std::floor((width - center.x()) / step));
	const auto top = int(std::ceil(-center.y() / step));
	const auto bottom = int(std::floor((height - center.y()) / step));
	auto result = std::optional<Candidate>();
	for (auto row = top; row <= bottom; ++row) {
		for (auto column = left; column <= right; ++column) {
			const auto distance = float64(column) * column
				+ float64(row) * row;
			if (result && distance > result->distance) {
				continue;
			}
			const auto point = center + QPointF(column * step, row * step);
			const auto side = FittedSide(
				Available(space, point, true),
				limit,
				span,
				space.ratio);
			if (side < least - kEpsilon) {
				continue;
			}
			const auto candidate = Candidate{
				.center = point,
				.side = side,
				.distance = distance,
				.painted = int(base::SafeRound(side)),
				.row = row,
				.column = column,
			};
			if (!result || Nearer(candidate, *result)) {
				result = candidate;
			}
		}
	}
	if (!result) {
		return std::nullopt;
	}
	return CardDrop{
		.center = result->center,
		.side = result->side,
		.kind = CardDropKind::Moved,
	};
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

QRect CardNameRect(
		const CardTextLayout &layout,
		int nameWidth,
		int nameHeight) {
	return QRect(
		layout.nameLeft,
		layout.nameTop,
		std::min(nameWidth, layout.nameMax),
		nameHeight);
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

CardSpace ComputeCardSpace(
		QSize card,
		const std::vector<QRectF> &protectedAreas,
		const std::vector<Data::StoryArea> &stickers,
		float64 touch,
		int ratio) {
	auto result = CardSpace{
		.card = card,
		.touch = touch,
		.ratio = std::max(ratio, 1),
	};
	for (const auto &rect : protectedAreas) {
		if (rect.width() > 0. && rect.height() > 0.) {
			result.guarded.push_back(GuardedCore(rect, touch));
		}
	}
	for (const auto &area : stickers) {
		const auto box = Footprint(area, card, result.ratio);
		if (!box.isEmpty()) {
			result.stickers.push_back(box);
		}
	}
	return result;
}

float64 FreeSideAt(
		const CardSpace &space,
		QPointF center,
		float64 limit,
		float64 rotation) {
	return FittedSide(
		Available(space, center, true),
		limit,
		RotationSpan(rotation),
		space.ratio);
}

std::optional<Data::StoryArea> AutoPlace(const CardSpace &space) {
	if (space.card.isEmpty()) {
		return std::nullopt;
	}
	auto best = BestCandidate(space, true);
	if (!best) {
		best = BestCandidate(space, false);
	}
	if (!best) {
		return std::nullopt;
	}
	return CardStickerArea(best->center, best->side, 0., space.card);
}

std::optional<CardDrop> ResolveCardDrop(
		const CardSpace &space,
		QPointF center) {
	if (space.card.isEmpty()
		|| !std::isfinite(center.x())
		|| !std::isfinite(center.y())) {
		return std::nullopt;
	} else if (const auto chip = ChipDrop(space, center)) {
		return chip;
	}
	const auto width = float64(space.card.width());
	const auto limit = width * kPreferredSide;
	const auto least = CardStickerMinimumSide(limit, space.card);
	const auto side = FreeSideAt(space, center, limit, 0.);
	if (side >= least - kEpsilon) {
		return CardDrop{ .center = center, .side = side };
	}
	if (const auto nearest = NearestDrop(space, center, limit, least, 1.)) {
		return nearest;
	}
	const auto area = AutoPlace(space);
	if (!area) {
		return std::nullopt;
	}
	return CardDrop{
		.center = CardStickerCenter(*area, space.card),
		.side = area->geometry.width() * width,
		.kind = CardDropKind::Moved,
	};
}

std::optional<CardDrop> ResolveCardMove(
		const CardSpace &space,
		QPointF center,
		float64 side,
		float64 rotation) {
	if (space.card.isEmpty()
		|| !std::isfinite(center.x())
		|| !std::isfinite(center.y())
		|| !std::isfinite(side)
		|| !std::isfinite(rotation)
		|| side <= 0.) {
		return std::nullopt;
	} else if (std::abs(std::remainder(rotation, 360.)) < kChipRotation) {
		if (const auto chip = ChipDrop(space, center)) {
			return chip;
		}
	}
	const auto least = CardStickerMinimumSide(side, space.card);
	const auto fitted = FreeSideAt(space, center, side, rotation);
	if (fitted >= least - kEpsilon) {
		return CardDrop{ .center = center, .side = fitted };
	}
	return NearestDrop(space, center, side, least, RotationSpan(rotation));
}

} // namespace Wallet
