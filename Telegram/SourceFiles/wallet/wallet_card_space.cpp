/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_space.h"

#include "data/data_story.h"

#include <QtCore/QLineF>

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kOutlineRatio = 0.03;
constexpr auto kAnchorX = 0.55;
constexpr auto kAnchorY = 0.70;
constexpr auto kPreferredSide = 0.21;
constexpr auto kLatticeStep = 0.005;
constexpr auto kLeftWeight = 1.5;
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
			if (side <= 0.) {
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
	result.nameLeft = style::RightToLeft()
		? (card.width() - st::walletCardContentLeft - result.nameMax)
		: st::walletCardContentLeft;
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

QPointF CardFoldPoint(const CardFold &fold, QPointF local) {
	return fold.transform.map(QPointF(fold.rest.topLeft()) + local);
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
	auto result = CardSpace{ .card = card, .ratio = std::max(ratio, 1) };
	for (const auto &rect : protectedAreas) {
		if (rect.width() > 0. && rect.height() > 0.) {
			const auto core = rect.adjusted(touch, touch, -touch, -touch);
			result.guarded.push_back(core.isEmpty() ? rect : core);
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

} // namespace Wallet
