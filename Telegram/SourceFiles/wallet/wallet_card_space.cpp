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
constexpr auto kPlaceTolerance = 1e-6;
constexpr auto kParallelSine = 1e-12;
constexpr auto kRootBisections = 40;
constexpr auto kGridDivisions = 8;
constexpr auto kStartSpread = 2.;
constexpr auto kNudge = 2.;
constexpr auto kRefineStarts = 8;
constexpr auto kRefineLevels = 6;
constexpr auto kRefineMoves = 4;
constexpr auto kCurveDivisions = 16;
constexpr auto kCurveSteps = 24;
constexpr auto kGolden = 0.6180339887498949;

using Curve = std::array<QPointF, 4>;

struct Footprint {
	QPointF center;
	float64 half = 0.;
	float64 rotation = 0.;
	std::array<QPointF, 4> corners;
	QRectF bounds;
};

struct Edge {
	QPointF from;
	QPointF direction;
	float64 length = 0.;
	QRectF bounds;
};

struct Forbidden {
	std::vector<Edge> edges;
	QRectF bounds;
};

struct Corner {
	std::array<QPointF, 4> points;
	QPointF center;
	QPointF outward;
	QRectF bounds;
};

struct Region {
	QRectF box;
	std::vector<Edge> lines;
	std::vector<Corner> corners;
};

struct Search {
	std::vector<Footprint> footprints;
	std::vector<Forbidden> forbidden;
	Region region;
	QPointF anchor;
	float64 half = 0.;
	float64 epsilon = 0.;
};

struct Parameters {
	std::array<float64, 8> values = {};
	int count = 0;
};

struct Clipped {
	std::array<float64, 16> xs = {};
	std::array<float64, 16> ys = {};
	int count = 0;
};

struct Spot {
	QPointF center;
	float64 overlap = 0.;
};

struct Bracket {
	int curve = 0;
	float64 from = 0.;
	float64 till = 0.;
};

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

[[nodiscard]] float64 Cross(QPointF a, QPointF b) {
	return a.x() * b.y() - a.y() * b.x();
}

[[nodiscard]] float64 Length(QPointF vector) {
	return std::sqrt(QPointF::dotProduct(vector, vector));
}

[[nodiscard]] QRectF Bounds(const std::array<QPointF, 4> &points) {
	auto left = points[0].x();
	auto top = points[0].y();
	auto right = left;
	auto bottom = top;
	for (const auto &point : points) {
		left = std::min(left, point.x());
		top = std::min(top, point.y());
		right = std::max(right, point.x());
		bottom = std::max(bottom, point.y());
	}
	return QRectF(QPointF(left, top), QPointF(right, bottom));
}

[[nodiscard]] bool Near(const QRectF &a, const QRectF &b, float64 margin) {
	return (a.left() <= b.right() + margin)
		&& (b.left() <= a.right() + margin)
		&& (a.top() <= b.bottom() + margin)
		&& (b.top() <= a.bottom() + margin);
}

[[nodiscard]] Edge MakeEdge(QPointF from, QPointF to) {
	const auto delta = to - from;
	const auto length = Length(delta);
	return {
		.from = from,
		.direction = delta / length,
		.length = length,
		.bounds = QRectF(from, to).normalized(),
	};
}

[[nodiscard]] std::array<QPointF, 4> SquareCorners(float64 half) {
	return {
		QPointF(-half, -half),
		QPointF(half, -half),
		QPointF(half, half),
		QPointF(-half, half),
	};
}

[[nodiscard]] std::vector<Footprint> CollectFootprints(
		const std::vector<Data::StoryArea> &shown,
		QSize card,
		int ratio) {
	auto result = std::vector<Footprint>();
	result.reserve(shown.size());
	for (const auto &area : shown) {
		const auto side = CardStickerSide(area, card);
		const auto center = CardStickerCenter(area, card);
		if (side <= 0
			|| !std::isfinite(center.x())
			|| !std::isfinite(center.y())) {
			continue;
		}
		result.push_back({
			.center = center,
			.half = CardStickerOuterSide(side, ratio) / 2.,
			.rotation = CardStickerRotation(area),
		});
	}
	ranges::sort(result, std::less<>(), [](const Footprint &footprint) {
		return std::make_tuple(
			footprint.center.x(),
			footprint.center.y(),
			footprint.half,
			footprint.rotation);
	});
	for (auto &footprint : result) {
		const auto rotation = QTransform().rotate(footprint.rotation);
		const auto offsets = SquareCorners(footprint.half);
		for (auto i = 0; i != 4; ++i) {
			footprint.corners[i] = footprint.center + rotation.map(offsets[i]);
		}
		footprint.bounds = Bounds(footprint.corners);
	}
	return result;
}

void AddHullPoint(std::vector<QPointF> &hull, QPointF point, size_t floor) {
	while (hull.size() >= floor) {
		const auto &origin = hull[hull.size() - 2];
		if (Cross(hull.back() - origin, point - origin) > 0.) {
			break;
		}
		hull.pop_back();
	}
	hull.push_back(point);
}

[[nodiscard]] std::vector<QPointF> ConvexHull(
		std::array<QPointF, 16> points,
		float64 epsilon) {
	ranges::sort(points, std::less<>(), [](QPointF point) {
		return std::make_pair(point.x(), point.y());
	});
	auto result = std::vector<QPointF>();
	result.reserve(points.size() + 1);
	for (const auto &point : points) {
		AddHullPoint(result, point, 2);
	}
	const auto lower = result.size() + 1;
	for (auto i = int(points.size()) - 2; i >= 0; --i) {
		AddHullPoint(result, points[i], lower);
	}
	result.pop_back();
	result.erase(ranges::unique(result, [&](QPointF a, QPointF b) {
		return Length(b - a) <= epsilon;
	}), end(result));
	while (result.size() > 1
		&& Length(result.back() - result.front()) <= epsilon) {
		result.pop_back();
	}
	return result;
}

[[nodiscard]] Forbidden ForbiddenCenters(
		const Footprint &footprint,
		float64 half,
		float64 epsilon) {
	const auto offsets = SquareCorners(half);
	auto sums = std::array<QPointF, 16>();
	for (auto i = 0; i != 4; ++i) {
		for (auto j = 0; j != 4; ++j) {
			sums[i * 4 + j] = footprint.corners[i] + offsets[j];
		}
	}
	const auto hull = ConvexHull(sums, epsilon);
	auto result = Forbidden{
		.bounds = footprint.bounds.adjusted(-half, -half, half, half),
	};
	if (hull.size() > 2) {
		for (auto i = size_t(), count = hull.size(); i != count; ++i) {
			result.edges.push_back(MakeEdge(hull[i], hull[(i + 1) % count]));
		}
	}
	return result;
}

[[nodiscard]] bool Blocks(
		const Forbidden &forbidden,
		QPointF point,
		float64 epsilon) {
	if (forbidden.edges.empty() || !forbidden.bounds.contains(point)) {
		return false;
	}
	for (const auto &edge : forbidden.edges) {
		if (Cross(edge.direction, point - edge.from) <= epsilon) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QPointF Bezier(
		const std::array<QPointF, 4> &points,
		float64 t) {
	const auto s = 1. - t;
	return s * s * s * points[0]
		+ 3. * s * s * t * points[1]
		+ 3. * s * t * t * points[2]
		+ t * t * t * points[3];
}

[[nodiscard]] std::array<float64, 4> Coefficients(
		const std::array<QPointF, 4> &points,
		QPointF origin,
		QPointF direction) {
	const auto &[p0, p1, p2, p3] = points;
	return {
		Cross(direction, p0 - origin),
		Cross(direction, 3. * (p1 - p0)),
		Cross(direction, 3. * (p2 - 2. * p1 + p0)),
		Cross(direction, p3 - 3. * p2 + 3. * p1 - p0),
	};
}

[[nodiscard]] float64 Polynomial(
		const std::array<float64, 4> &c,
		float64 t) {
	return ((c[3] * t + c[2]) * t + c[1]) * t + c[0];
}

[[nodiscard]] Parameters MonotoneSplits(const std::array<float64, 4> &c) {
	auto result = Parameters{ .values = { 0. }, .count = 1 };
	const auto add = [&](float64 t) {
		if (t > 0. && t < 1.) {
			result.values[result.count++] = t;
		}
	};
	const auto a = 3. * c[3];
	const auto b = 2. * c[2];
	const auto discriminant = b * b - 4. * a * c[1];
	if (a == 0.) {
		if (b != 0.) {
			add(-c[1] / b);
		}
	} else if (discriminant >= 0.) {
		const auto q = -(b + std::copysign(std::sqrt(discriminant), b)) / 2.;
		if (q != 0.) {
			add(q / a);
			add(c[1] / q);
		}
	}
	std::sort(
		result.values.begin() + 1,
		result.values.begin() + result.count);
	result.values[result.count++] = 1.;
	return result;
}

[[nodiscard]] std::optional<float64> Bisected(
		const std::array<float64, 4> &c,
		float64 low,
		float64 high) {
	const auto negative = (Polynomial(c, low) < 0.);
	if (negative == (Polynomial(c, high) < 0.)) {
		return std::nullopt;
	}
	for (auto i = 0; i != kRootBisections; ++i) {
		const auto middle = (low + high) / 2.;
		if ((Polynomial(c, middle) < 0.) == negative) {
			low = middle;
		} else {
			high = middle;
		}
	}
	return (low + high) / 2.;
}

[[nodiscard]] Parameters CubicRoots(
		const std::array<float64, 4> &c,
		float64 tolerance) {
	const auto splits = MonotoneSplits(c);
	const auto &values = splits.values;
	auto result = Parameters();
	for (auto i = 0; i != splits.count; ++i) {
		if (std::abs(Polynomial(c, values[i])) <= tolerance) {
			result.values[result.count++] = values[i];
		}
	}
	for (auto i = 1; i != splits.count; ++i) {
		if (const auto root = Bisected(c, values[i - 1], values[i])) {
			result.values[result.count++] = *root;
		}
	}
	return result;
}

[[nodiscard]] Corner MakeCorner(
		const std::array<QPointF, 4> &points,
		QPointF middle) {
	const auto first = QPointF(points[0].x(), points[3].y());
	const auto second = QPointF(points[3].x(), points[0].y());
	const auto inner = (Length(first - middle) < Length(second - middle));
	const auto center = inner ? first : second;
	return {
		.points = points,
		.center = center,
		.outward = (inner ? second : first) - center,
		.bounds = Bounds(points),
	};
}

[[nodiscard]] std::optional<Region> CenterRegion(
		QSize card,
		float64 half,
		float64 epsilon) {
	const auto radius = float64(st::walletCardRadius);
	const auto box = QRectF(
		half,
		half,
		card.width() - 2. * half,
		card.height() - 2. * half);
	if (half <= 0.
		|| box.width() < 2. * radius
		|| box.height() < 2. * radius) {
		return std::nullopt;
	}
	auto path = QPainterPath();
	path.addRoundedRect(box, radius, radius);
	auto result = Region{ .box = box };
	auto from = QPointF();
	for (auto i = 0, count = path.elementCount(); i != count; ++i) {
		const auto &element = path.elementAt(i);
		if (element.isCurveTo() && i + 2 < count) {
			result.corners.push_back(MakeCorner({
				from,
				element,
				path.elementAt(i + 1),
				path.elementAt(i + 2),
			}, box.center()));
			i += 2;
		} else if (element.isLineTo()
			&& Length(QPointF(element) - from) > epsilon) {
			result.lines.push_back(MakeEdge(from, element));
		}
		from = path.elementAt(i);
	}
	return result;
}

[[nodiscard]] bool WithinCorner(
		const Corner &corner,
		QPointF offset,
		float64 epsilon) {
	const auto distance = Length(offset);
	const auto direction = offset / distance;
	const auto roots = CubicRoots(
		Coefficients(corner.points, corner.center, direction),
		epsilon);
	for (auto i = 0; i != roots.count; ++i) {
		const auto reach = Bezier(corner.points, roots.values[i])
			- corner.center;
		if (QPointF::dotProduct(reach, direction) > 0.) {
			return (distance <= Length(reach) + epsilon);
		}
	}
	return false;
}

[[nodiscard]] bool Contains(
		const Region &region,
		QPointF point,
		float64 epsilon) {
	const auto &box = region.box;
	if (point.x() < box.left() - epsilon
		|| point.x() > box.right() + epsilon
		|| point.y() < box.top() - epsilon
		|| point.y() > box.bottom() + epsilon) {
		return false;
	}
	for (const auto &corner : region.corners) {
		const auto offset = point - corner.center;
		if (offset.x() * corner.outward.x() > 0.
			&& offset.y() * corner.outward.y() > 0.) {
			return WithinCorner(corner, offset, epsilon);
		}
	}
	return true;
}

void AddCrossings(
		std::vector<QPointF> &points,
		const Edge &a,
		const Edge &b,
		float64 epsilon) {
	const auto sine = Cross(a.direction, b.direction);
	if (std::abs(sine) <= kParallelSine
		|| !Near(a.bounds, b.bounds, epsilon)) {
		return;
	}
	const auto delta = b.from - a.from;
	const auto along = Cross(delta, b.direction) / sine;
	const auto across = Cross(delta, a.direction) / sine;
	if (along >= -epsilon
		&& along <= a.length + epsilon
		&& across >= -epsilon
		&& across <= b.length + epsilon) {
		points.push_back(a.from + a.direction * along);
	}
}

void AddCrossings(
		std::vector<QPointF> &points,
		const Edge &edge,
		const Corner &corner,
		float64 epsilon) {
	if (!Near(edge.bounds, corner.bounds, epsilon)) {
		return;
	}
	const auto roots = CubicRoots(
		Coefficients(corner.points, edge.from, edge.direction),
		epsilon);
	for (auto i = 0; i != roots.count; ++i) {
		const auto point = Bezier(corner.points, roots.values[i]);
		const auto along = QPointF::dotProduct(
			point - edge.from,
			edge.direction);
		if (along >= -epsilon && along <= edge.length + epsilon) {
			points.push_back(point);
		}
	}
}

// WHY: a nearest free centre for a blocked anchor lies on a forbidden
// boundary: at a vertex, at the anchor's projection or at a crossing.
[[nodiscard]] std::vector<QPointF> ContactCandidates(const Search &search) {
	const auto &forbidden = search.forbidden;
	const auto &region = search.region;
	const auto epsilon = search.epsilon;
	auto result = std::vector<QPointF>();
	for (auto i = size_t(); i != forbidden.size(); ++i) {
		for (const auto &edge : forbidden[i].edges) {
			const auto projection = QPointF::dotProduct(
				search.anchor - edge.from,
				edge.direction);
			const auto along = std::clamp(projection, 0., edge.length);
			result.push_back(edge.from);
			result.push_back(edge.from + edge.direction * along);
			for (const auto &line : region.lines) {
				AddCrossings(result, edge, line, epsilon);
			}
			for (const auto &corner : region.corners) {
				AddCrossings(result, edge, corner, epsilon);
			}
			for (auto j = i + 1; j != forbidden.size(); ++j) {
				if (!Near(edge.bounds, forbidden[j].bounds, epsilon)) {
					continue;
				}
				for (const auto &other : forbidden[j].edges) {
					AddCrossings(result, edge, other, epsilon);
				}
			}
		}
	}
	result.erase(ranges::remove_if(result, [&](QPointF point) {
		return !Contains(region, point, epsilon);
	}), end(result));
	return result;
}

[[nodiscard]] bool Nearer(const Search &search, QPointF a, QPointF b) {
	const auto epsilon = search.epsilon;
	const auto anchor = search.anchor;
	const auto distance = Length(a - anchor) - Length(b - anchor);
	const auto vertical = std::abs(a.y() - anchor.y())
		- std::abs(b.y() - anchor.y());
	if (std::abs(distance) > epsilon) {
		return (distance < 0.);
	} else if (std::abs(vertical) > epsilon) {
		return (vertical < 0.);
	} else if (std::abs(a.x() - b.x()) > epsilon) {
		return (a.x() > b.x());
	}
	return (a.y() < b.y() - epsilon);
}

[[nodiscard]] bool Free(const Search &search, QPointF point) {
	return ranges::none_of(search.forbidden, [&](const Forbidden &forbidden) {
		return Blocks(forbidden, point, search.epsilon);
	});
}

[[nodiscard]] std::optional<QPointF> NearestFree(
		const Search &search,
		const std::vector<QPointF> &candidates) {
	auto result = std::optional<QPointF>();
	for (const auto &point : candidates) {
		if ((!result || Nearer(search, point, *result))
			&& Free(search, point)) {
			result = point;
		}
	}
	return result;
}

[[nodiscard]] Clipped Clip(
		const Clipped &polygon,
		bool vertical,
		float64 bound,
		float64 sign) {
	auto result = Clipped();
	const auto &along = vertical ? polygon.xs : polygon.ys;
	const auto &across = vertical ? polygon.ys : polygon.xs;
	auto &resultAlong = vertical ? result.xs : result.ys;
	auto &resultAcross = vertical ? result.ys : result.xs;
	const auto capacity = int(result.xs.size());
	for (auto i = 0; i != polygon.count; ++i) {
		const auto j = (i + 1 == polygon.count) ? 0 : (i + 1);
		const auto a = sign * (along[i] - bound);
		const auto b = sign * (along[j] - bound);
		if (a >= 0. && result.count < capacity) {
			resultAlong[result.count] = along[i];
			resultAcross[result.count++] = across[i];
		}
		if ((a < 0.) != (b < 0.) && result.count < capacity) {
			resultAlong[result.count] = bound;
			resultAcross[result.count++] = across[i]
				+ (across[j] - across[i]) * (a / (a - b));
		}
	}
	return result;
}

[[nodiscard]] float64 Area(const Clipped &polygon) {
	const auto &xs = polygon.xs;
	const auto &ys = polygon.ys;
	auto twice = 0.;
	for (auto i = 1; i + 1 < polygon.count; ++i) {
		twice += (xs[i] - xs[0]) * (ys[i + 1] - ys[0])
			- (ys[i] - ys[0]) * (xs[i + 1] - xs[0]);
	}
	return std::abs(twice) / 2.;
}

[[nodiscard]] float64 ClippedArea(
		const Footprint &footprint,
		const QRectF &box) {
	auto polygon = Clipped{ .count = 4 };
	for (auto i = 0; i != 4; ++i) {
		polygon.xs[i] = footprint.corners[i].x();
		polygon.ys[i] = footprint.corners[i].y();
	}
	const auto &bounds = footprint.bounds;
	if (bounds.left() < box.left()) {
		polygon = Clip(polygon, true, box.left(), 1.);
	}
	if (bounds.right() > box.right()) {
		polygon = Clip(polygon, true, box.right(), -1.);
	}
	if (bounds.top() < box.top()) {
		polygon = Clip(polygon, false, box.top(), 1.);
	}
	if (bounds.bottom() > box.bottom()) {
		polygon = Clip(polygon, false, box.bottom(), -1.);
	}
	return Area(polygon);
}

[[nodiscard]] float64 Overlap(const Search &search, QPointF center) {
	const auto corner = QPointF(search.half, search.half);
	const auto box = QRectF(center - corner, center + corner);
	auto result = 0.;
	for (const auto &footprint : search.footprints) {
		if (footprint.bounds.intersects(box)) {
			result += ClippedArea(footprint, box);
		}
	}
	return result;
}

[[nodiscard]] bool Better(
		const Search &search,
		const Spot &a,
		const Spot &b) {
	const auto overlap = a.overlap - b.overlap;
	return (std::abs(overlap) > search.epsilon * search.half)
		? (overlap < 0.)
		: Nearer(search, a.center, b.center);
}

[[nodiscard]] bool Allowed(const Search &search, QPointF center) {
	const auto limit = search.epsilon * search.epsilon;
	for (const auto &footprint : search.footprints) {
		const auto delta = center - footprint.center;
		if (QPointF::dotProduct(delta, delta) <= limit) {
			return false;
		}
	}
	return Contains(search.region, center, search.epsilon);
}

[[nodiscard]] std::vector<QPointF> GridCandidates(
		const Search &search,
		float64 step) {
	const auto &box = search.region.box;
	const auto anchor = search.anchor;
	const auto left = int(std::ceil((box.left() - anchor.x()) / step));
	const auto right = int(std::floor((box.right() - anchor.x()) / step));
	const auto top = int(std::ceil((box.top() - anchor.y()) / step));
	const auto bottom = int(std::floor((box.bottom() - anchor.y()) / step));
	auto result = std::vector<QPointF>();
	result.reserve(std::max(right - left + 1, 0)
		* std::max(bottom - top + 1, 0));
	for (auto row = top; row <= bottom; ++row) {
		for (auto column = left; column <= right; ++column) {
			result.push_back(anchor + QPointF(column, row) * step);
		}
	}
	return result;
}

[[nodiscard]] std::vector<QPointF> NudgeCandidates(const Search &search) {
	const auto offsets = SquareCorners(kNudge * search.epsilon);
	auto result = std::vector<QPointF>();
	for (const auto &footprint : search.footprints) {
		for (const auto &offset : offsets) {
			result.push_back(footprint.center + offset);
		}
	}
	return result;
}

[[nodiscard]] int BestSpot(
		const Search &search,
		const std::vector<Spot> &spots,
		int from,
		int till,
		const std::vector<bool> &skip) {
	auto result = -1;
	for (auto i = from; i != till; ++i) {
		if (!skip[i]
			&& (result < 0 || Better(search, spots[i], spots[result]))) {
			result = i;
		}
	}
	return result;
}

void AddStart(
		std::vector<int> &starts,
		std::vector<bool> &skip,
		const std::vector<Spot> &spots,
		int index,
		float64 spread) {
	if (index < 0 || ranges::contains(starts, index)) {
		return;
	}
	starts.push_back(index);
	for (auto i = size_t(); i != spots.size(); ++i) {
		const auto delta = spots[i].center - spots[index].center;
		if (QPointF::dotProduct(delta, delta) <= spread * spread) {
			skip[i] = true;
		}
	}
}

[[nodiscard]] std::vector<int> RefineStarts(
		const Search &search,
		const std::vector<Spot> &spots,
		int grid,
		int edges,
		float64 spread) {
	const auto all = int(spots.size());
	auto skip = std::vector<bool>(spots.size(), false);
	const auto forced = std::array{
		BestSpot(search, spots, 0, all, skip),
		BestSpot(search, spots, 0, grid, skip),
		BestSpot(search, spots, grid, edges, skip),
	};
	auto result = std::vector<int>();
	for (const auto index : forced) {
		AddStart(result, skip, spots, index, spread);
	}
	while (int(result.size()) < kRefineStarts) {
		const auto index = BestSpot(search, spots, 0, all, skip);
		if (index < 0) {
			break;
		}
		AddStart(result, skip, spots, index, spread);
	}
	return result;
}

[[nodiscard]] Spot Refine(const Search &search, Spot spot, float64 step) {
	constexpr auto kDirections = std::array{
		QPointF(1., 0.),
		QPointF(-1., 0.),
		QPointF(0., 1.),
		QPointF(0., -1.),
		QPointF(1., 1.),
		QPointF(1., -1.),
		QPointF(-1., 1.),
		QPointF(-1., -1.),
	};
	for (auto level = 0; level != kRefineLevels; ++level, step /= 2.) {
		for (auto move = 0; move != kRefineMoves; ++move) {
			auto next = spot;
			for (const auto &direction : kDirections) {
				const auto center = spot.center + direction * step;
				if (!Allowed(search, center)) {
					continue;
				}
				const auto candidate = Spot{
					.center = center,
					.overlap = Overlap(search, center),
				};
				if (Better(search, candidate, next)) {
					next = candidate;
				}
			}
			if (!Better(search, next, spot)) {
				break;
			}
			spot = next;
		}
	}
	return spot;
}

[[nodiscard]] std::vector<Curve> BoundaryCurves(const Region &region) {
	auto result = std::vector<Curve>();
	for (const auto &line : region.lines) {
		const auto third = line.direction * (line.length / 3.);
		result.push_back({
			line.from,
			line.from + third,
			line.from + 2. * third,
			line.from + 3. * third,
		});
	}
	for (const auto &corner : region.corners) {
		result.push_back(corner.points);
	}
	return result;
}

[[nodiscard]] Spot CurveMinimum(
		const Search &search,
		const Curve &curve,
		float64 from,
		float64 till) {
	const auto spot = [&](float64 t) {
		const auto center = Bezier(curve, t);
		return Spot{ center, Overlap(search, center) };
	};
	auto low = till - kGolden * (till - from);
	auto high = from + kGolden * (till - from);
	auto lower = spot(low);
	auto higher = spot(high);
	for (auto i = 0; i != kCurveSteps; ++i) {
		if (lower.overlap <= higher.overlap) {
			till = high;
			high = low;
			higher = lower;
			low = till - kGolden * (till - from);
			lower = spot(low);
		} else {
			from = low;
			low = high;
			lower = higher;
			high = from + kGolden * (till - from);
			higher = spot(high);
		}
	}
	return (lower.overlap <= higher.overlap) ? lower : higher;
}

[[nodiscard]] std::vector<Spot> BoundarySpots(const Search &search) {
	const auto step = search.half / kCurveDivisions;
	const auto curves = BoundaryCurves(search.region);
	auto samples = std::vector<Spot>();
	auto brackets = std::vector<Bracket>();
	auto points = std::vector<Spot>();
	for (auto k = 0; k != int(curves.size()); ++k) {
		const auto &curve = curves[k];
		const auto length = Length(curve[1] - curve[0])
			+ Length(curve[2] - curve[1])
			+ Length(curve[3] - curve[2]);
		const auto count = std::max(int(std::ceil(length / step)), 1);
		points.clear();
		for (auto i = 0; i <= count; ++i) {
			const auto center = Bezier(curve, i / float64(count));
			points.push_back({ center, Overlap(search, center) });
		}
		for (auto i = 0; i <= count; ++i) {
			if ((i > 0 && points[i - 1].overlap <= points[i].overlap)
				|| (i < count && points[i + 1].overlap < points[i].overlap)
				|| !Allowed(search, points[i].center)) {
				continue;
			}
			samples.push_back(points[i]);
			brackets.push_back({
				.curve = k,
				.from = std::max(i - 1, 0) / float64(count),
				.till = std::min(i + 1, count) / float64(count),
			});
		}
	}
	auto skip = std::vector<bool>(samples.size(), false);
	auto result = std::vector<Spot>();
	while (int(result.size()) < kRefineStarts) {
		const auto index = BestSpot(
			search,
			samples,
			0,
			int(samples.size()),
			skip);
		if (index < 0) {
			break;
		}
		skip[index] = true;
		const auto &bracket = brackets[index];
		const auto refined = CurveMinimum(
			search,
			curves[bracket.curve],
			bracket.from,
			bracket.till);
		result.push_back((Allowed(search, refined.center)
			&& Better(search, refined, samples[index]))
			? refined
			: samples[index]);
	}
	return result;
}

[[nodiscard]] std::optional<QPointF> LeastOverlap(
		const Search &search,
		const std::vector<QPointF> &contacts) {
	const auto step = search.half / kGridDivisions;
	auto spots = std::vector<Spot>();
	const auto add = [&](const std::vector<QPointF> &centers) {
		for (const auto &center : centers) {
			if (Allowed(search, center)) {
				spots.push_back({ center, Overlap(search, center) });
			}
		}
		return int(spots.size());
	};
	const auto grid = add(GridCandidates(search, step));
	const auto boundary = BoundarySpots(search);
	spots.insert(end(spots), begin(boundary), end(boundary));
	const auto edges = int(spots.size());
	add(contacts);
	add(NudgeCandidates(search));
	const auto starts = RefineStarts(
		search,
		spots,
		grid,
		edges,
		kStartSpread * step);
	auto result = std::optional<Spot>();
	for (const auto index : starts) {
		const auto spot = Refine(search, spots[index], step / 2.);
		if (!result || Better(search, spot, *result)) {
			result = spot;
		}
	}
	return result ? std::make_optional(result->center) : std::nullopt;
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

std::optional<Data::StoryArea> CardStickerAutoArea(
		const std::vector<Data::StoryArea> &shown,
		QSize card,
		int ratio) {
	if (card.isEmpty()) {
		return std::nullopt;
	}
	const auto anchor = QPointF(
		card.width() * kAnchorX,
		card.height() * kAnchorY);
	const auto side = CardStickerPreferredSide(card);
	const auto preferred = CardStickerArea(anchor, side, 0., card);
	const auto half = CardStickerOuterSide(
		CardStickerSide(preferred, card),
		ratio) / 2.;
	const auto epsilon = card.width() * kPlaceTolerance;
	auto region = CenterRegion(card, half, epsilon);
	if (!region || !Contains(*region, anchor, epsilon)) {
		return std::nullopt;
	}
	auto search = Search{
		.footprints = CollectFootprints(shown, card, ratio),
		.region = std::move(*region),
		.anchor = anchor,
		.half = half,
		.epsilon = epsilon,
	};
	for (const auto &footprint : search.footprints) {
		search.forbidden.push_back(
			ForbiddenCenters(footprint, half, epsilon));
	}
	if (Free(search, anchor)) {
		return preferred;
	}
	const auto contacts = ContactCandidates(search);
	const auto nearest = NearestFree(search, contacts);
	const auto center = nearest ? nearest : LeastOverlap(search, contacts);
	if (!center) {
		return std::nullopt;
	}
	return CardStickerArea(*center, side, 0., card);
}

} // namespace Wallet
