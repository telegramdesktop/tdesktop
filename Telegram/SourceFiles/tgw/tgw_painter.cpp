/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_painter.h"

#include "tgw/tgw_backend.h"
#include "ui/image/image_prepare.h"

namespace Tgw {
namespace {

constexpr auto kMaxFrameBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxLayerBytes = int64(128 * 1024 * 1024);
constexpr auto kMaxLayerWork = int64(256 * 1024 * 1024);
constexpr auto kPixelBytes = 4;
constexpr auto kDeviceMargin = float64(1 << 20);
constexpr auto kInkMargin = 2.;
constexpr auto kKappa = 0.5522847498;
constexpr auto kStopEpsilon = 1e-4;
constexpr auto kMiterLimit = 4.;
constexpr auto kMaxDashSteps = int64(1 << 17);
constexpr auto kMaxFlattenBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxCurvePoints = int64(512);
constexpr auto kPolygonReserve = int64(16);
constexpr auto kBlockHeader = int64(64);
constexpr auto kPageBytes = int64(4096);
constexpr auto kMaxMaskBlur = 24.;
constexpr auto kMaxMaskPixels = float64(1 << 20);
constexpr auto kMaskMargin = 2;
constexpr auto kMaxShadowBytes = int64(16 * 1024 * 1024);
constexpr auto kMaxShadows = 128;

struct CornerRadius {
	float64 x = 0.;
	float64 y = 0.;

	friend inline bool operator==(CornerRadius, CornerRadius) = default;
};

struct Corners {
	CornerRadius topLeft;
	CornerRadius topRight;
	CornerRadius bottomRight;
	CornerRadius bottomLeft;

	friend inline bool operator==(
		const Corners &,
		const Corners &) = default;
};

struct ShadowSource {
	QRectF box;
	Corners boxCorners;
	QRectF area;
	QRectF shape;
	Corners shapeCorners;
	bool inset = false;
};

struct MaskLayout {
	QSize size;
	float64 scale = 0.;
	int padding = 0;
	int radius = 0;

	friend inline bool operator==(
		const MaskLayout &,
		const MaskLayout &) = default;
};

struct ShadowKey {
	float64 x = 0.;
	float64 y = 0.;
	float64 width = 0.;
	float64 height = 0.;
	Corners corners;
	MaskLayout layout;
	QRgb color = 0;
	bool inset = false;

	friend inline bool operator==(
		const ShadowKey &,
		const ShadowKey &) = default;
};

class ShadowCache final {
public:
	[[nodiscard]] QImage find(const ShadowKey &key);
	void insert(const ShadowKey &key, QImage image);

private:
	struct Entry {
		ShadowKey key;
		QImage image;
	};

	std::vector<Entry> _entries;
	int64 _bytes = 0;

};

class Dasher final {
public:
	Dasher(
		std::vector<float64> pattern,
		float64 offset,
		float64 scale,
		const QRectF &visible,
		int64 &budget);

	[[nodiscard]] bool add(const QPolygonF &polygon);
	[[nodiscard]] const QPainterPath &result() const;

private:
	[[nodiscard]] bool skip(float64 distance);
	[[nodiscard]] bool walk(QPointF from, QPointF to, float64 length);
	[[nodiscard]] bool next();
	void line(QPointF from, QPointF to);

	const std::vector<float64> _pattern;
	const float64 _period = 0.;
	const float64 _offset = 0.;
	const float64 _scale = 1.;
	const QRectF _visible;
	int64 &_budget;
	QPainterPath _result;
	size_t _index = 0;
	float64 _left = 0.;
	bool _drawing = false;

};

class Executor final {
public:
	Executor(QPainter &root, const QtBackend &backend, float64 scale);

	[[nodiscard]] bool run(const twlite::DisplayList &list);

private:
	enum class EntryKind : uchar {
		Clip,
		Group,
		Layer,
		Hidden,
	};
	struct Entry {
		EntryKind kind = EntryKind::Clip;
		bool saved = false;
	};
	struct Layer {
		QImage image;
		QPainter painter;
		QPoint origin;
		float64 opacity = 1.;
	};

	[[nodiscard]] bool execute(const twlite::Command &command);
	void fillShape(const twlite::Command &command, const QBrush &brush);
	void strokeRect(const twlite::Command &command);
	void fillEllipse(const twlite::Command &command);
	void strokeEllipse(const twlite::Command &command);
	void drawText(const twlite::Command &command);
	void drawImage(const twlite::Command &command);
	[[nodiscard]] bool drawPath(const twlite::Command &command);
	[[nodiscard]] bool strokeDashes(
		const QPainterPath &path,
		const QPen &pen,
		std::vector<float64> dashes,
		float64 offset);
	[[nodiscard]] bool drawShadow(const twlite::Command &command);
	[[nodiscard]] bool drawMask(
		const ShadowSource &source,
		const MaskLayout &layout,
		QRgb color);
	void fillShadow(const ShadowSource &source, QRgb color);
	void clipShadow(const ShadowSource &source);
	void pushClip(const twlite::Command &command);
	void popClip();
	[[nodiscard]] bool pushGroup(const twlite::Command &command);
	[[nodiscard]] bool pushLayer(
		const QTransform &world,
		float64 opacity,
		const QRectF &ink);
	void pushHidden();
	void popGroup();
	void popEntry(bool composite);
	void popLayer(bool composite);
	void unwind(bool composite);

	void clipToRect(const QRectF &rect);
	[[nodiscard]] QPainter &painter() const;
	[[nodiscard]] QPoint origin() const;
	[[nodiscard]] QRectF bounded(const QRectF &rect) const;
	[[nodiscard]] QRectF limit() const;
	[[nodiscard]] bool reachable(QPointF point) const;

	QPainter &_root;
	const QtBackend &_backend;
	std::vector<Entry> _stack;
	std::vector<std::unique_ptr<Layer>> _layers;
	std::vector<QRectF> _inks;
	size_t _group = 0;
	int64 _layerBytes = 0;
	int64 _layerWork = 0;
	int64 _dashSteps = kMaxDashSteps;
	int64 _flattenBytes = kMaxFlattenBytes;
	int _hidden = 0;

};

[[nodiscard]] bool ValidFrame(QSize size, float64 scale) {
	return !size.isEmpty()
		&& (int64(size.width()) * size.height()
			<= kMaxFrameBytes / kPixelBytes)
		&& std::isfinite(scale)
		&& (scale > 0.);
}

[[nodiscard]] int64 LayerBytes(QSize size) {
	return int64(size.width()) * size.height() * kPixelBytes;
}

void PrepareHints(QPainter &painter) {
	painter.setRenderHints(QPainter::Antialiasing
		| QPainter::SmoothPixmapTransform
		| QPainter::TextAntialiasing);
}

[[nodiscard]] QRectF DeviceLimit(const QPaintDevice &device) {
	return QRectF(0., 0., device.width(), device.height()).adjusted(
		-kDeviceMargin,
		-kDeviceMargin,
		kDeviceMargin,
		kDeviceMargin);
}

[[nodiscard]] QRectF DeviceClipBounds(QPainter &painter) {
	const auto world = painter.worldTransform();
	painter.setWorldTransform(QTransform());
	const auto result = painter.clipBoundingRect();
	painter.setWorldTransform(world);
	return result;
}

[[nodiscard]] QRectF VisibleRect(QPainter &painter) {
	const auto device = painter.device();
	const auto result = QRectF(0., 0., device->width(), device->height());
	return painter.hasClipping()
		? result.intersected(DeviceClipBounds(painter))
		: result;
}

[[nodiscard]] QRect LayerArea(
		QPainter &parent,
		QPoint origin,
		const QPaintDevice &frame,
		const QRectF &ink) {
	if (ink.isEmpty()) {
		return QRect();
	}
	auto result = QRectF(0., 0., frame.width(), frame.height());
	result = result.intersected(VisibleRect(parent).translated(origin));
	result = result.intersected(ink.adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin));
	return result.toAlignedRect();
}

[[nodiscard]] bool Finite(QPointF point) {
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

[[nodiscard]] bool Finite(const QTransform &transform) {
	const auto values = std::array{
		transform.m11(),
		transform.m12(),
		transform.m21(),
		transform.m22(),
		transform.dx(),
		transform.dy(),
	};
	return ranges::all_of(values, [](qreal value) {
		return std::isfinite(value);
	});
}

[[nodiscard]] float64 MaxStretch(const QTransform &transform) {
	const auto a = transform.m11();
	const auto b = transform.m12();
	const auto c = transform.m21();
	const auto d = transform.m22();
	return (std::hypot(a + d, c - b) + std::hypot(a - d, b + c)) / 2.;
}

[[nodiscard]] bool Structural(twlite::Cmd kind) {
	using Cmd = twlite::Cmd;
	constexpr auto kinds = std::array{
		Cmd::ClipPush,
		Cmd::ClipPop,
		Cmd::GroupPush,
		Cmd::GroupPop,
	};
	return ranges::contains(kinds, kind);
}

[[nodiscard]] bool Shown(const twlite::Command &command) {
	return !command.hidden
		&& std::isfinite(command.opacity)
		&& (command.opacity > 0.f);
}

[[nodiscard]] std::optional<QTransform> GroupTransform(
		const twlite::Command &command,
		const QTransform &parent) {
	const auto &m = command.matrix;
	const auto matrix = QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
	if (!Finite(matrix) || !matrix.isInvertible()) {
		return std::nullopt;
	}
	const auto result = matrix * parent;
	return (Finite(result) && result.isInvertible())
		? std::make_optional(result)
		: std::nullopt;
}

[[nodiscard]] bool ValidWidth(float width) {
	return std::isfinite(width) && (width > 0.f);
}

[[nodiscard]] QColor ToColor(twlite::Color color) {
	return QColor(color.r, color.g, color.b, color.a);
}

[[nodiscard]] QRectF ToRect(const twlite::Rect &rect) {
	const auto finite = std::isfinite(rect.x)
		&& std::isfinite(rect.y)
		&& std::isfinite(rect.w)
		&& std::isfinite(rect.h);
	return (finite && !rect.empty())
		? QRectF(rect.x, rect.y, rect.w, rect.h)
		: QRectF();
}

[[nodiscard]] QPen StrokePen(twlite::Color color, float width) {
	return QPen(
		ToColor(color),
		width,
		Qt::SolidLine,
		Qt::FlatCap,
		Qt::MiterJoin);
}

[[nodiscard]] float64 OverlapFactor(float64 length, float64 sum) {
	return (sum > length) ? (length / sum) : 1.;
}

[[nodiscard]] std::array<CornerRadius*, 4> CornerList(Corners &corners) {
	return {
		&corners.topLeft,
		&corners.topRight,
		&corners.bottomRight,
		&corners.bottomLeft,
	};
}

[[nodiscard]] Corners FitCorners(const QRectF &rect, Corners corners) {
	const auto factor = std::min({
		OverlapFactor(
			rect.width(),
			corners.topLeft.x + corners.topRight.x),
		OverlapFactor(
			rect.width(),
			corners.bottomLeft.x + corners.bottomRight.x),
		OverlapFactor(
			rect.height(),
			corners.topLeft.y + corners.bottomLeft.y),
		OverlapFactor(
			rect.height(),
			corners.topRight.y + corners.bottomRight.y),
	});
	for (const auto corner : CornerList(corners)) {
		corner->x *= factor;
		corner->y *= factor;
		if (corner->x <= 0. || corner->y <= 0.) {
			*corner = CornerRadius();
		}
	}
	return corners;
}

[[nodiscard]] std::optional<Corners> NormalizedCorners(
		const QRectF &rect,
		const twlite::Radii &radii) {
	const auto values = std::array{
		radii.tlx,
		radii.tly,
		radii.trx,
		radii.try_,
		radii.brx,
		radii.bry,
		radii.blx,
		radii.bly,
	};
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	if (!ranges::all_of(values, finite)) {
		return std::nullopt;
	}
	const auto radius = [](float x, float y) {
		return CornerRadius{
			.x = std::max(float64(x), 0.),
			.y = std::max(float64(y), 0.),
		};
	};
	return FitCorners(rect, Corners{
		.topLeft = radius(radii.tlx, radii.tly),
		.topRight = radius(radii.trx, radii.try_),
		.bottomRight = radius(radii.brx, radii.bry),
		.bottomLeft = radius(radii.blx, radii.bly),
	});
}

[[nodiscard]] Corners SpreadCorners(
		const QRectF &rect,
		Corners corners,
		float64 spread) {
	for (const auto corner : CornerList(corners)) {
		if (corner->x > 0.) {
			corner->x = std::max(corner->x + spread, 0.);
			corner->y = std::max(corner->y + spread, 0.);
		}
	}
	return FitCorners(rect, corners);
}

[[nodiscard]] bool Rounded(const Corners &corners) {
	return (corners.topLeft.x > 0.)
		|| (corners.topRight.x > 0.)
		|| (corners.bottomRight.x > 0.)
		|| (corners.bottomLeft.x > 0.);
}

[[nodiscard]] QPainterPath RoundedPath(
		const QRectF &rect,
		const Corners &corners) {
	const auto left = rect.left();
	const auto top = rect.top();
	const auto right = rect.right();
	const auto bottom = rect.bottom();
	const auto &tl = corners.topLeft;
	const auto &tr = corners.topRight;
	const auto &br = corners.bottomRight;
	const auto &bl = corners.bottomLeft;
	auto result = QPainterPath();
	result.moveTo(left + tl.x, top);
	result.lineTo(right - tr.x, top);
	if (tr.x > 0.) {
		result.cubicTo(
			QPointF(right - tr.x + tr.x * kKappa, top),
			QPointF(right, top + tr.y - tr.y * kKappa),
			QPointF(right, top + tr.y));
	}
	result.lineTo(right, bottom - br.y);
	if (br.x > 0.) {
		result.cubicTo(
			QPointF(right, bottom - br.y + br.y * kKappa),
			QPointF(right - br.x + br.x * kKappa, bottom),
			QPointF(right - br.x, bottom));
	}
	result.lineTo(left + bl.x, bottom);
	if (bl.x > 0.) {
		result.cubicTo(
			QPointF(left + bl.x - bl.x * kKappa, bottom),
			QPointF(left, bottom - bl.y + bl.y * kKappa),
			QPointF(left, bottom - bl.y));
	}
	result.lineTo(left, top + tl.y);
	if (tl.x > 0.) {
		result.cubicTo(
			QPointF(left, top + tl.y - tl.y * kKappa),
			QPointF(left + tl.x - tl.x * kKappa, top),
			QPointF(left + tl.x, top));
	}
	result.closeSubpath();
	return result;
}

[[nodiscard]] QRectF SourceRect(
		QSize size,
		const QRectF &target,
		const QRectF &visible) {
	const auto scaleX = size.width() / target.width();
	const auto scaleY = size.height() / target.height();
	return QRectF(
		(visible.x() - target.x()) * scaleX,
		(visible.y() - target.y()) * scaleY,
		visible.width() * scaleX,
		visible.height() * scaleY);
}

[[nodiscard]] QGradientStops OrderedStops(
		const std::vector<twlite::Stop> &stops,
		bool reversed) {
	auto result = QGradientStops();
	result.reserve(int(stops.size()));
	auto previous = 0.;
	for (const auto &stop : stops) {
		if (std::isfinite(stop.offset)) {
			previous = std::clamp(float64(stop.offset), previous, 1.);
			result.push_back(QGradientStop(previous, ToColor(stop.color)));
		}
	}
	if (reversed) {
		std::reverse(result.begin(), result.end());
		for (auto &stop : result) {
			stop.first = 1. - stop.first;
		}
	}
	return result;
}

[[nodiscard]] QGradientStops RunEnds(const QGradientStops &stops) {
	auto result = QGradientStops();
	const auto count = stops.size();
	for (auto i = 0; i != count; ++i) {
		const auto offset = stops[i].first;
		if (i == 0
			|| i + 1 == count
			|| stops[i - 1].first != offset
			|| stops[i + 1].first != offset) {
			result.push_back(stops[i]);
		}
	}
	return result;
}

[[nodiscard]] QGradientStops SeparatedPairs(QGradientStops stops) {
	const auto count = stops.size();
	for (auto i = 0; i + 1 < count; ++i) {
		const auto offset = stops[i].first;
		if (stops[i + 1].first != offset) {
			continue;
		}
		const auto below = (i > 0) ? stops[i - 1].first : 0.;
		const auto above = (i + 2 < count) ? stops[i + 2].first : 1.;
		stops[i].first = offset
			- std::min(kStopEpsilon / 2., (offset - below) / 3.);
		stops[i + 1].first = offset
			+ std::min(kStopEpsilon / 2., (above - offset) / 3.);
		++i;
	}
	return stops;
}

[[nodiscard]] QGradientStops StrictStops(
		const std::vector<twlite::Stop> &stops,
		bool reversed) {
	return SeparatedPairs(RunEnds(OrderedStops(stops, reversed)));
}

[[nodiscard]] bool Uniform(const QGradientStops &stops) {
	const auto &color = stops.front().second;
	return ranges::all_of(stops, [&](const QGradientStop &stop) {
		return (stop.second == color);
	});
}

[[nodiscard]] QBrush StopsBrush(
		QGradient &gradient,
		const QGradientStops &stops) {
	if (stops.isEmpty()) {
		return QBrush();
	} else if (Uniform(stops)) {
		return QBrush(stops.back().second);
	}
	gradient.setStops(stops);
	return QBrush(gradient);
}

[[nodiscard]] QBrush SolidBrush(twlite::Color color) {
	return color.a ? QBrush(ToColor(color)) : QBrush();
}

[[nodiscard]] QBrush LinearBrush(
		QPointF from,
		QPointF to,
		const std::vector<twlite::Stop> &stops) {
	if (!Finite(from) || !Finite(to)) {
		return QBrush();
	}
	const auto strict = StrictStops(stops, false);
	if (from == to && !strict.isEmpty()) {
		return QBrush(strict.back().second);
	}
	auto gradient = QLinearGradient(from, to);
	return StopsBrush(gradient, strict);
}

[[nodiscard]] QBrush RadialBrush(
		QPointF centre,
		float64 radiusX,
		float64 radiusY,
		QPointF focal,
		const std::vector<twlite::Stop> &stops) {
	if (!Finite(centre)
		|| !Finite(focal)
		|| !std::isfinite(radiusX)
		|| !std::isfinite(radiusY)) {
		return QBrush();
	}
	const auto strict = StrictStops(stops, false);
	if (!(radiusX > 0.) || !(radiusY > 0.)) {
		return strict.isEmpty() ? QBrush() : QBrush(strict.back().second);
	}
	const auto offset = focal - centre;
	auto gradient = QRadialGradient(
		QPointF(),
		1.,
		QPointF(offset.x() / radiusX, offset.y() / radiusY));
	auto result = StopsBrush(gradient, strict);
	if (result.gradient()) {
		result.setTransform(QTransform(
			radiusX,
			0.,
			0.,
			radiusY,
			centre.x(),
			centre.y()));
	}
	return result;
}

[[nodiscard]] QBrush ConicBrush(
		QPointF centre,
		float64 from,
		const std::vector<twlite::Stop> &stops) {
	if (!Finite(centre) || !std::isfinite(from)) {
		return QBrush();
	}
	const auto turn = std::fmod(from, 360.);
	// CSS turns clockwise from 12 o'clock, Qt counter-clockwise from 3 o'clock.
	auto gradient = QConicalGradient(
		centre,
		std::fmod(90. - turn + 360., 360.));
	return StopsBrush(gradient, StrictStops(stops, true));
}

[[nodiscard]] QBrush BackgroundBrush(const twlite::Command &command) {
	const auto origin = QPointF(command.ax, command.ay);
	switch (command.kind) {
	case twlite::Cmd::Linear:
		return LinearBrush(
			origin,
			QPointF(command.bx, command.by),
			command.stops);
	case twlite::Cmd::Radial:
		return RadialBrush(
			origin,
			command.bx,
			command.by,
			origin,
			command.stops);
	case twlite::Cmd::Conic:
		return ConicBrush(origin, command.bx, command.stops);
	default:
		return QBrush();
	}
}

[[nodiscard]] int PointValues(twlite::Path::Op op) {
	switch (op) {
	case twlite::Path::Move:
	case twlite::Path::Line: return 2;
	case twlite::Path::Quad: return 4;
	case twlite::Path::Cubic: return 6;
	case twlite::Path::Close: return 0;
	}
	return -1;
}

[[nodiscard]] QPainterPath VectorPath(const twlite::Path &path) {
	auto result = QPainterPath();
	result.setFillRule(path.evenodd ? Qt::OddEvenFill : Qt::WindingFill);
	const auto &points = path.points;
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	const auto point = [&](size_t at) {
		return QPointF(points[at], points[at + 1]);
	};
	auto index = size_t();
	auto started = false;
	for (const auto op : path.ops) {
		const auto count = PointValues(op);
		if (count < 0 || points.size() - index < size_t(count)) {
			break;
		}
		const auto from = points.begin() + index;
		if (!std::all_of(from, from + count, finite)) {
			break;
		}
		switch (op) {
		case twlite::Path::Move:
			result.moveTo(point(index));
			started = true;
			break;
		case twlite::Path::Line:
			if (started) {
				result.lineTo(point(index));
			}
			break;
		case twlite::Path::Quad:
			if (started) {
				result.quadTo(point(index), point(index + 2));
			}
			break;
		case twlite::Path::Cubic:
			if (started) {
				result.cubicTo(
					point(index),
					point(index + 2),
					point(index + 4));
			}
			break;
		case twlite::Path::Close:
			if (started) {
				result.closeSubpath();
			}
			break;
		}
		index += count;
	}
	return result;
}

[[nodiscard]] QBrush PathBrush(const twlite::Path &path) {
	const auto gradient = path.stops.empty() ? 0 : path.gradient;
	switch (gradient) {
	case 1:
		return LinearBrush(
			QPointF(path.x1, path.y1),
			QPointF(path.x2, path.y2),
			path.stops);
	case 2:
		return RadialBrush(
			QPointF(path.x1, path.y1),
			path.r,
			path.r,
			QPointF(path.fx, path.fy),
			path.stops);
	}
	return SolidBrush(path.fill_color);
}

[[nodiscard]] Qt::PenCapStyle PathCap(int cap) {
	constexpr auto styles = std::array{
		Qt::FlatCap,
		Qt::RoundCap,
		Qt::SquareCap,
	};
	return (cap >= 0 && cap < int(styles.size()))
		? styles[cap]
		: styles.front();
}

[[nodiscard]] Qt::PenJoinStyle PathJoin(int join) {
	constexpr auto styles = std::array{
		Qt::SvgMiterJoin,
		Qt::RoundJoin,
		Qt::BevelJoin,
	};
	return (join >= 0 && join < int(styles.size()))
		? styles[join]
		: styles.front();
}

[[nodiscard]] std::vector<float64> DashPattern(
		const std::vector<float> &dashes) {
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	if (dashes.empty() || !ranges::all_of(dashes, finite)) {
		return {};
	}
	const auto count = dashes.size();
	const auto size = (count % 2) ? (count * 2) : count;
	auto result = std::vector<float64>();
	result.reserve(size);
	for (auto i = size_t(); i != size; ++i) {
		result.push_back(std::max(float64(dashes[i % count]), 0.));
	}
	const auto positive = [](float64 value) {
		return (value > 0.);
	};
	return ranges::any_of(result, positive)
		? result
		: std::vector<float64>();
}

[[nodiscard]] float64 MiterLimit(const twlite::Path &path) {
	return std::isfinite(path.miter_limit)
		? std::max(float64(path.miter_limit), 1.)
		: kMiterLimit;
}

[[nodiscard]] QPen PathPen(const twlite::Path &path) {
	auto result = QPen(
		ToColor(path.stroke_color),
		float64(path.stroke_width),
		Qt::SolidLine,
		PathCap(path.line_cap),
		PathJoin(path.line_join));
	result.setMiterLimit(MiterLimit(path));
	return result;
}

[[nodiscard]] float64 StrokeReach(float64 width, float64 miterLimit) {
	return width * std::max(miterLimit, 2.) / 2.;
}

[[nodiscard]] float64 Wrap(float64 value, float64 period) {
	const auto result = std::fmod(value, period);
	return (result < 0.) ? std::fmod(result + period, period) : result;
}

[[nodiscard]] std::optional<std::pair<float64, float64>> ClipSegment(
		QPointF from,
		QPointF to,
		const QRectF &rect) {
	const auto delta = to - from;
	const auto edges = std::array{
		std::pair{ -delta.x(), from.x() - rect.left() },
		std::pair{ delta.x(), rect.right() - from.x() },
		std::pair{ -delta.y(), from.y() - rect.top() },
		std::pair{ delta.y(), rect.bottom() - from.y() },
	};
	auto enter = 0.;
	auto leave = 1.;
	for (const auto &[direction, distance] : edges) {
		if (direction < 0.) {
			enter = std::max(enter, distance / direction);
		} else if (direction > 0.) {
			leave = std::min(leave, distance / direction);
		} else if (distance < 0.) {
			return std::nullopt;
		}
	}
	return (enter < leave)
		? std::make_optional(std::make_pair(enter, leave))
		: std::nullopt;
}

[[nodiscard]] bool ReserveFlattening(
		const QPainterPath &path,
		int64 &allowance) {
	auto left = allowance;
	const auto charge = [&](int64 count, int64 size) {
		const auto most = (left - kPageBytes) / 2 - kBlockHeader;
		if (most < 0 || count > most / size) {
			return false;
		}
		left -= 2 * (count * size + kBlockHeader) + kPageBytes;
		return true;
	};
	const auto pointBytes = int64(sizeof(QPointF));
	const auto polygonBytes = int64(sizeof(QPolygonF));
	const auto dashBytes = int64(2 * sizeof(QPainterPath::Element));
	const auto limit = left / pointBytes;
	auto capacity = kPolygonReserve;
	auto points = int64();
	auto vertices = int64();
	auto polygons = int64();
	const auto store = [&] {
		if (points < 2) {
			return true;
		}
		capacity = std::max(capacity, points);
		vertices += points;
		++polygons;
		return charge(capacity, pointBytes);
	};
	for (auto i = 0, count = path.elementCount(); i != count; ++i) {
		switch (path.elementAt(i).type) {
		case QPainterPath::MoveToElement:
			if (!store()) {
				return false;
			}
			points = 1;
			break;
		case QPainterPath::LineToElement: ++points; break;
		case QPainterPath::CurveToElement: points += kMaxCurvePoints; break;
		case QPainterPath::CurveToDataElement: break;
		}
		if (points > limit) {
			return false;
		}
	}
	if (!store()
		|| !charge(capacity, pointBytes)
		|| !charge(polygons, polygonBytes)
		|| !charge(polygons, polygonBytes)
		|| !charge(vertices, dashBytes)
		|| !charge(vertices, dashBytes)) {
		return false;
	}
	allowance = left;
	return true;
}

[[nodiscard]] ShadowSource OuterShadow(
		const QRectF &box,
		const Corners &corners,
		QPointF offset,
		float64 spread) {
	const auto shape = box.adjusted(-spread, -spread, spread, spread);
	return {
		.box = box,
		.boxCorners = corners,
		.area = shape.translated(offset),
		.shape = QRectF(QPointF(), shape.size()),
		.shapeCorners = shape.isEmpty()
			? Corners()
			: SpreadCorners(shape, corners, spread),
	};
}

[[nodiscard]] ShadowSource InsetShadow(
		const QRectF &box,
		const Corners &corners,
		QPointF offset,
		float64 spread) {
	const auto inner = QRectF(QPointF(), box.size()).adjusted(
		spread,
		spread,
		-spread,
		-spread);
	const auto hole = inner.translated(offset);
	return {
		.box = box,
		.boxCorners = corners,
		.area = box,
		.shape = hole,
		.shapeCorners = hole.isEmpty()
			? Corners()
			: SpreadCorners(hole, corners, -spread),
		.inset = true,
	};
}

[[nodiscard]] int StackRadius(float64 sigma) {
	return int(base::SafeRound(std::sqrt(1. + 6. * sigma * sigma) - 1.));
}

[[nodiscard]] float64 FittingScale(QSizeF size, float64 margin) {
	const auto a = size.width() * size.height();
	const auto b = margin * (size.width() + size.height());
	const auto c = margin * margin - kMaxMaskPixels;
	return -2. * c / (b + std::sqrt(b * b - 4. * a * c));
}

[[nodiscard]] std::optional<MaskLayout> ChooseMaskLayout(
		QSizeF area,
		float64 blur,
		float64 stretch) {
	if (!std::isfinite(stretch) || !(stretch > 0.)) {
		return std::nullopt;
	}
	const auto sigma = blur / 2.;
	const auto limit = std::min(stretch, kMaxMaskBlur / blur);
	const auto margin = 2. * (StackRadius(sigma * limit) + kMaskMargin) + 1.;
	const auto scale = std::min(limit, FittingScale(area, margin));
	if (!std::isfinite(scale) || !(scale > 0.)) {
		return std::nullopt;
	}
	const auto radius = StackRadius(sigma * scale);
	const auto padding = radius + kMaskMargin;
	const auto width = std::ceil(area.width() * scale) + 2. * padding;
	const auto height = std::ceil(area.height() * scale) + 2. * padding;
	if (width * height > kMaxMaskPixels) {
		return std::nullopt;
	}
	return MaskLayout{
		.size = QSize(int(width), int(height)),
		.scale = scale,
		.padding = padding,
		.radius = radius,
	};
}

[[nodiscard]] int64 BlurBytes(QSize size) {
	const auto mask = LayerBytes(size);
	const auto scratch = mask;
	return mask + scratch;
}

[[nodiscard]] QRectF TextInk(
		const twlite::Command &command,
		const QtBackend &backend) {
	const auto font = backend.font(command.font);
	if (!font || !(font->scale > 0.) || command.text.empty()) {
		return QRectF();
	}
	const auto &metrics = font->metrics;
	const auto margin = metrics.height();
	const auto text = QString::fromUtf8(
		command.text.data(),
		int(command.text.size()));
	auto transform = QTransform::fromTranslate(command.x, command.y);
	transform.scale(font->scale, font->scale);
	return transform.mapRect(metrics.boundingRect(text).adjusted(
		-margin,
		-margin,
		margin,
		margin));
}

[[nodiscard]] QRectF PathInk(const twlite::Path &path) {
	const auto vector = VectorPath(path);
	if (vector.isEmpty()) {
		return QRectF();
	}
	const auto stroked = path.stroke
		&& path.stroke_color.a
		&& ValidWidth(path.stroke_width);
	const auto reach = stroked
		? StrokeReach(path.stroke_width, MiterLimit(path))
		: 0.;
	return vector.controlPointRect().adjusted(-reach, -reach, reach, reach);
}

[[nodiscard]] QRectF ShadowInk(
		const twlite::Command &command,
		float64 stretch) {
	const auto box = ToRect(command.rect);
	if (command.inset || box.isEmpty()) {
		return box;
	}
	const auto offset = QPointF(command.ax, command.ay);
	const auto blur = std::max(float64(command.bx), 0.);
	const auto spread = float64(command.by);
	if (!Finite(offset) || !std::isfinite(blur) || !std::isfinite(spread)) {
		return QRectF();
	}
	const auto shape = box.adjusted(-spread, -spread, spread, spread);
	const auto area = shape.translated(offset);
	const auto layout = (blur > 0. && !area.isEmpty())
		? ChooseMaskLayout(area.size(), blur, stretch)
		: std::nullopt;
	const auto margin = layout ? (layout->padding / layout->scale) : 0.;
	return area.adjusted(-margin, -margin, margin, margin);
}

[[nodiscard]] QRectF CommandInk(
		const twlite::Command &command,
		const QtBackend &backend,
		float64 stretch) {
	using Cmd = twlite::Cmd;
	const auto rect = ToRect(command.rect);
	const auto width = ValidWidth(command.width)
		? float64(command.width)
		: 0.;
	switch (command.kind) {
	case Cmd::FillRect:
	case Cmd::Image:
	case Cmd::Linear:
	case Cmd::Radial:
	case Cmd::Conic:
		return command.has_clip
			? rect.intersected(ToRect(command.clip))
			: rect;
	case Cmd::StrokeRect:
		return rect.isEmpty()
			? QRectF()
			: rect.adjusted(-width, -width, width, width);
	case Cmd::FillEllipse:
	case Cmd::StrokeEllipse: return rect;
	case Cmd::Text: return TextInk(command, backend);
	case Cmd::Path: return command.path ? PathInk(*command.path) : QRectF();
	case Cmd::Shadow: return ShadowInk(command, stretch);
	case Cmd::ClipPush:
	case Cmd::ClipPop:
	case Cmd::GroupPush:
	case Cmd::GroupPop: break;
	}
	return QRectF();
}

[[nodiscard]] QRectF DeviceInk(
		const QTransform &world,
		const QRectF &rect,
		const QRectF &frame) {
	if (rect.isEmpty()) {
		return QRectF();
	}
	const auto result = world.mapRect(rect);
	return (Finite(result.topLeft()) && Finite(result.bottomRight()))
		? result.intersected(frame)
		: frame;
}

[[nodiscard]] std::vector<QRectF> GroupInks(
		const twlite::DisplayList &list,
		const QtBackend &backend,
		const QTransform &root,
		const QRectF &frame) {
	struct Open {
		size_t slot = 0;
		QTransform world;
		QRectF ink;
		bool hidden = false;
		bool layer = false;
	};
	auto result = std::vector<QRectF>();
	auto stack = std::vector<Open>{ Open{ .world = root } };
	auto layers = 0;
	const auto close = [&] {
		const auto group = stack.back();
		stack.pop_back();
		result[group.slot] = group.ink;
		stack.back().ink |= group.ink;
		if (group.layer) {
			--layers;
		}
	};
	for (const auto &command : list) {
		if (command.kind == twlite::Cmd::GroupPush) {
			const auto parent = stack.back();
			const auto world = (parent.hidden || !Shown(command))
				? std::nullopt
				: GroupTransform(command, parent.world);
			const auto layer = world && (command.opacity < 1.f);
			stack.push_back({
				.slot = result.size(),
				.world = world.value_or(QTransform()),
				.hidden = !world,
				.layer = layer,
			});
			result.emplace_back();
			if (layer) {
				++layers;
			}
		} else if (command.kind == twlite::Cmd::GroupPop) {
			if (stack.size() > 1) {
				close();
			}
		} else if (layers > 0
			&& !stack.back().hidden
			&& !stack.back().ink.contains(frame)) {
			auto &top = stack.back();
			top.ink |= DeviceInk(
				top.world,
				CommandInk(command, backend, MaxStretch(top.world)),
				frame);
		}
	}
	while (stack.size() > 1) {
		close();
	}
	return result;
}

[[nodiscard]] QImage ColorizeMask(QImage mask, QRgb color) {
	auto colors = std::array<QRgb, 256>();
	for (auto coverage = 0; coverage != 256; ++coverage) {
		colors[coverage] = qPremultiply(qRgba(
			qRed(color),
			qGreen(color),
			qBlue(color),
			(qAlpha(color) * coverage + 127) / 255));
	}
	for (auto y = 0; y != mask.height(); ++y) {
		const auto row = reinterpret_cast<QRgb*>(mask.scanLine(y));
		// BlurLargeImage() leaves alpha unblurred: coverage is blue, as in Ui::BoxShadow.
		std::transform(row, row + mask.width(), row, [&](QRgb pixel) {
			return colors[pixel & 0xFFU];
		});
	}
	return mask;
}

[[nodiscard]] QImage PrepareShadow(const ShadowKey &key) {
	const auto &layout = key.layout;
	auto result = QImage(layout.size, QImage::Format_ARGB32_Premultiplied);
	if (result.isNull()) {
		return QImage();
	}
	result.fill(key.inset ? Qt::white : Qt::transparent);
	auto p = QPainter();
	if (!p.begin(&result)) {
		return QImage();
	}
	p.setRenderHint(QPainter::Antialiasing);
	if (key.inset) {
		p.setCompositionMode(QPainter::CompositionMode_Clear);
	}
	p.translate(layout.padding, layout.padding);
	p.scale(layout.scale, layout.scale);
	p.fillPath(
		RoundedPath(
			QRectF(key.x, key.y, key.width, key.height),
			key.corners),
		Qt::white);
	p.end();
	return ColorizeMask(
		Images::BlurLargeImage(std::move(result), layout.radius),
		key.color);
}

[[nodiscard]] ShadowCache &Shadows() {
	static auto result = ShadowCache();
	return result;
}

QImage ShadowCache::find(const ShadowKey &key) {
	const auto i = ranges::find(_entries, key, &Entry::key);
	if (i == end(_entries)) {
		return QImage();
	}
	std::rotate(i, i + 1, end(_entries));
	return _entries.back().image;
}

void ShadowCache::insert(const ShadowKey &key, QImage image) {
	_bytes += LayerBytes(image.size());
	_entries.push_back({ .key = key, .image = std::move(image) });
	auto evicted = begin(_entries);
	while (_bytes > kMaxShadowBytes
		|| end(_entries) - evicted > kMaxShadows) {
		_bytes -= LayerBytes(evicted->image.size());
		++evicted;
	}
	_entries.erase(begin(_entries), evicted);
}

Dasher::Dasher(
		std::vector<float64> pattern,
		float64 offset,
		float64 scale,
		const QRectF &visible,
		int64 &budget)
: _pattern(std::move(pattern))
, _period(ranges::accumulate(_pattern, 0.))
, _offset(Wrap(offset, _period))
, _scale(scale)
, _visible(visible)
, _budget(budget) {
}

bool Dasher::add(const QPolygonF &polygon) {
	_index = 0;
	_left = _pattern.front();
	_drawing = false;
	if (!skip(_offset)) {
		return false;
	}
	for (auto i = 1; i < polygon.size(); ++i) {
		const auto from = polygon[i - 1];
		const auto delta = polygon[i] - from;
		const auto length = std::hypot(delta.x(), delta.y()) / _scale;
		if (!std::isfinite(length) || !(length > 0.)) {
			continue;
		}
		const auto chord = ClipSegment(from, polygon[i], _visible);
		if (!chord) {
			if (!skip(length)) {
				return false;
			}
			continue;
		}
		const auto [enter, leave] = *chord;
		const auto start = from + delta * enter;
		const auto end = from + delta * leave;
		const auto visible = end - start;
		if (!skip(length * enter)
			|| !walk(
				start,
				end,
				std::hypot(visible.x(), visible.y()) / _scale)
			|| !skip(length * (1. - leave))) {
			return false;
		}
	}
	return true;
}

const QPainterPath &Dasher::result() const {
	return _result;
}

bool Dasher::skip(float64 distance) {
	if (!(distance > 0.)) {
		return true;
	}
	_drawing = false;
	if (distance < _left) {
		_left -= distance;
		return true;
	}
	distance = Wrap(distance - _left, _period);
	if (!next()) {
		return false;
	}
	for (auto i = size_t(); i != _pattern.size(); ++i) {
		if (distance < _left || (distance == 0. && _left == 0.)) {
			break;
		}
		distance -= _left;
		if (!next()) {
			return false;
		}
	}
	_left = std::max(_left - distance, 0.);
	return true;
}

bool Dasher::walk(QPointF from, QPointF to, float64 length) {
	if (!(length > 0.)) {
		return true;
	}
	const auto delta = to - from;
	auto done = 0.;
	while (true) {
		const auto rest = length - done;
		const auto on = !(_index % 2);
		const auto start = from + delta * (done / length);
		if (_left > rest) {
			if (on) {
				line(start, to);
			}
			_left -= rest;
			return true;
		} else if (on) {
			line(start, from + delta * ((done + _left) / length));
			_drawing = false;
		}
		done += _left;
		if (!next()) {
			return false;
		} else if (done >= length) {
			return true;
		}
	}
}

bool Dasher::next() {
	if (--_budget < 0) {
		return false;
	}
	_index = (_index + 1) % _pattern.size();
	_left = _pattern[_index];
	return true;
}

void Dasher::line(QPointF from, QPointF to) {
	if (!_drawing) {
		_result.moveTo(from);
		_drawing = true;
	}
	_result.lineTo(to);
}

Executor::Executor(
		QPainter &root,
		const QtBackend &backend,
		float64 scale)
: _root(root)
, _backend(backend) {
	PrepareHints(_root);
	_root.scale(scale, scale);
}

bool Executor::run(const twlite::DisplayList &list) {
	const auto device = _root.device();
	_inks = GroupInks(
		list,
		_backend,
		_root.worldTransform(),
		QRectF(0., 0., device->width(), device->height()));
	for (const auto &command : list) {
		if (!execute(command)) {
			unwind(false);
			return false;
		}
	}
	unwind(true);
	return true;
}

bool Executor::execute(const twlite::Command &command) {
	using Cmd = twlite::Cmd;
	if (_hidden > 0 && !Structural(command.kind)) {
		return true;
	}
	switch (command.kind) {
	case Cmd::FillRect: fillShape(command, SolidBrush(command.color)); break;
	case Cmd::StrokeRect: strokeRect(command); break;
	case Cmd::FillEllipse: fillEllipse(command); break;
	case Cmd::StrokeEllipse: strokeEllipse(command); break;
	case Cmd::Text: drawText(command); break;
	case Cmd::Image: drawImage(command); break;
	case Cmd::Linear:
	case Cmd::Radial:
	case Cmd::Conic: fillShape(command, BackgroundBrush(command)); break;
	case Cmd::ClipPush: pushClip(command); break;
	case Cmd::ClipPop: popClip(); break;
	case Cmd::Path: return drawPath(command);
	case Cmd::GroupPush: return pushGroup(command);
	case Cmd::GroupPop: popGroup(); break;
	case Cmd::Shadow: return drawShadow(command);
	}
	return true;
}

void Executor::fillShape(
		const twlite::Command &command,
		const QBrush &brush) {
	const auto rect = ToRect(command.rect);
	const auto clip = command.has_clip ? ToRect(command.clip) : rect;
	if (brush.style() == Qt::NoBrush || rect.isEmpty() || clip.isEmpty()) {
		return;
	}
	auto &p = painter();
	const auto corners = NormalizedCorners(rect, command.radii);
	if (!corners) {
		return;
	} else if (!Rounded(*corners)) {
		p.fillRect(rect.intersected(clip), brush);
	} else if (clip.contains(rect)) {
		p.fillPath(RoundedPath(rect, *corners), brush);
	} else {
		p.save();
		clipToRect(clip);
		p.fillPath(RoundedPath(rect, *corners), brush);
		p.restore();
	}
}

void Executor::strokeRect(const twlite::Command &command) {
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty() || !command.color.a || !ValidWidth(command.width)) {
		return;
	}
	if (const auto corners = NormalizedCorners(rect, command.radii)) {
		painter().strokePath(
			RoundedPath(rect, *corners),
			StrokePen(command.color, command.width));
	}
}

void Executor::fillEllipse(const twlite::Command &command) {
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty() || !command.color.a) {
		return;
	}
	auto path = QPainterPath();
	path.addEllipse(rect);
	painter().fillPath(path, ToColor(command.color));
}

void Executor::strokeEllipse(const twlite::Command &command) {
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty() || !command.color.a || !ValidWidth(command.width)) {
		return;
	}
	const auto half = command.width / 2.;
	const auto inset = rect.adjusted(half, half, -half, -half);
	if (inset.isEmpty()) {
		return;
	}
	auto path = QPainterPath();
	path.addEllipse(inset);
	painter().strokePath(path, StrokePen(command.color, command.width));
}

void Executor::drawText(const twlite::Command &command) {
	const auto font = _backend.font(command.font);
	const auto baseline = QPointF(command.x, command.y);
	if (!font
		|| !(font->scale > 0.)
		|| command.text.empty()
		|| !command.color.a
		|| !Finite(baseline)
		|| !reachable(baseline)) {
		return;
	}
	auto &p = painter();
	p.save();
	p.translate(baseline);
	if (font->scale != 1.) {
		p.scale(font->scale, font->scale);
	}
	p.setFont(font->font);
	p.setPen(ToColor(command.color));
	p.drawText(
		QPointF(),
		QString::fromUtf8(command.text.data(), int(command.text.size())));
	p.restore();
}

void Executor::drawImage(const twlite::Command &command) {
	const auto image = command.canvas
		? nullptr
		: _backend.image(command.image);
	const auto target = ToRect(command.rect);
	const auto base = command.has_clip ? ToRect(command.clip) : target;
	if (!image || image->isNull() || target.isEmpty() || base.isEmpty()) {
		return;
	}
	const auto corners = NormalizedCorners(base, command.radii);
	const auto visible = bounded(target.intersected(base));
	if (!corners || visible.isEmpty()) {
		return;
	}
	auto &p = painter();
	const auto rounded = Rounded(*corners);
	if (!rounded && base.contains(target)) {
		p.drawImage(target, *image);
		return;
	}
	p.save();
	if (rounded) {
		p.setClipPath(RoundedPath(base, *corners), Qt::IntersectClip);
	} else {
		clipToRect(base);
	}
	p.drawImage(target, *image);
	p.restore();
}

bool Executor::drawPath(const twlite::Command &command) {
	const auto source = command.path.get();
	if (!source) {
		return true;
	}
	const auto path = VectorPath(*source);
	if (path.isEmpty()) {
		return true;
	}
	const auto brush = source->fill ? PathBrush(*source) : QBrush();
	if (brush.style() != Qt::NoBrush) {
		painter().fillPath(path, brush);
	}
	if (!source->stroke
		|| !source->stroke_color.a
		|| !ValidWidth(source->stroke_width)) {
		return true;
	}
	const auto pen = PathPen(*source);
	auto dashes = DashPattern(source->dashes);
	if (dashes.empty()) {
		painter().strokePath(path, pen);
		return true;
	}
	return strokeDashes(
		path,
		pen,
		std::move(dashes),
		std::isfinite(source->dash_offset) ? source->dash_offset : 0.);
}

bool Executor::strokeDashes(
		const QPainterPath &path,
		const QPen &pen,
		std::vector<float64> dashes,
		float64 offset) {
	auto &p = painter();
	const auto scale = MaxStretch(p.combinedTransform());
	auto invertible = false;
	const auto fromDevice = p.combinedTransform().inverted(&invertible)
		* QTransform::fromScale(scale, scale);
	if (!std::isfinite(scale) || !(scale > 0.) || !invertible) {
		return true;
	}
	const auto width = pen.widthF() * scale;
	const auto reach = StrokeReach(width, pen.miterLimit());
	const auto device = VisibleRect(p).adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin);
	const auto visible = fromDevice.mapRect(device).adjusted(
		-reach,
		-reach,
		reach,
		reach);
	auto dasher = Dasher(
		std::move(dashes),
		offset,
		scale,
		visible,
		_dashSteps);
	const auto scaled = QTransform::fromScale(scale, scale);
	if (!ReserveFlattening(path, _flattenBytes)) {
		return false;
	}
	for (const auto &polygon : path.toSubpathPolygons(scaled)) {
		if (!dasher.add(polygon)) {
			return false;
		}
	}
	auto stroker = QPainterPathStroker(pen);
	stroker.setWidth(width);
	const auto outline = stroker.createStroke(dasher.result());
	p.save();
	p.scale(1. / scale, 1. / scale);
	p.fillPath(outline, pen.brush());
	p.restore();
	return true;
}

bool Executor::drawShadow(const twlite::Command &command) {
	const auto box = ToRect(command.rect);
	const auto offset = QPointF(command.ax, command.ay);
	const auto blur = std::max(float64(command.bx), 0.);
	const auto spread = float64(command.by);
	const auto corners = box.isEmpty()
		? std::nullopt
		: NormalizedCorners(box, command.radii);
	if (!corners
		|| !command.color.a
		|| !Finite(offset)
		|| !std::isfinite(blur)
		|| !std::isfinite(spread)
		|| (blur == 0. && spread == 0. && offset.isNull())
		|| limit().isEmpty()) {
		return true;
	}
	const auto source = command.inset
		? InsetShadow(box, *corners, offset, spread)
		: OuterShadow(box, *corners, offset, spread);
	const auto color = ToColor(command.color).rgba();
	if (source.shape.isEmpty() && !source.inset) {
		return true;
	} else if (blur == 0. || source.shape.isEmpty()) {
		fillShadow(source, color);
		return true;
	}
	const auto layout = ChooseMaskLayout(
		source.area.size(),
		blur,
		MaxStretch(painter().combinedTransform()));
	if (!layout) {
		return true;
	} else if (layout->radius < 1) {
		fillShadow(source, color);
		return true;
	}
	return drawMask(source, *layout, color);
}

bool Executor::drawMask(
		const ShadowSource &source,
		const MaskLayout &layout,
		QRgb color) {
	auto &p = painter();
	const auto padding = layout.padding / layout.scale;
	const auto target = QRectF(
		source.area.topLeft() - QPointF(padding, padding),
		QSizeF(layout.size) / layout.scale);
	const auto visible = bounded(target);
	if (visible.isEmpty()
		|| (p.hasClipping() && !p.clipBoundingRect().intersects(visible))) {
		return true;
	}
	const auto key = ShadowKey{
		.x = source.shape.x(),
		.y = source.shape.y(),
		.width = source.shape.width(),
		.height = source.shape.height(),
		.corners = source.shapeCorners,
		.layout = layout,
		.color = color,
		.inset = source.inset,
	};
	auto image = Shadows().find(key);
	if (image.isNull()) {
		if (BlurBytes(layout.size) > kMaxLayerBytes - _layerBytes) {
			return false;
		}
		image = PrepareShadow(key);
		if (image.isNull()) {
			return false;
		}
		Shadows().insert(key, image);
	}
	p.save();
	clipShadow(source);
	p.drawImage(visible, image, SourceRect(layout.size, target, visible));
	p.restore();
	return true;
}

void Executor::fillShadow(const ShadowSource &source, QRgb color) {
	auto path = source.shape.isEmpty()
		? QPainterPath()
		: RoundedPath(
			source.shape.translated(source.area.topLeft()),
			source.shapeCorners);
	if (source.inset) {
		path.addRect(limit());
	}
	path.setFillRule(Qt::OddEvenFill);
	auto &p = painter();
	p.save();
	clipShadow(source);
	p.fillPath(path, QColor::fromRgba(color));
	p.restore();
}

void Executor::clipShadow(const ShadowSource &source) {
	if (source.inset && !Rounded(source.boxCorners)) {
		clipToRect(source.box);
		return;
	}
	auto path = RoundedPath(source.box, source.boxCorners);
	if (!source.inset) {
		path.addRect(limit());
		path.setFillRule(Qt::OddEvenFill);
	}
	painter().setClipPath(path, Qt::IntersectClip);
}

void Executor::pushClip(const twlite::Command &command) {
	if (_hidden > 0) {
		_stack.push_back({ .kind = EntryKind::Clip });
		return;
	}
	auto &p = painter();
	p.save();
	_stack.push_back({ .kind = EntryKind::Clip, .saved = true });
	const auto rect = ToRect(command.rect);
	const auto corners = rect.isEmpty()
		? std::nullopt
		: NormalizedCorners(rect, command.radii);
	if (!corners) {
		p.setClipRect(QRectF(), Qt::IntersectClip);
	} else if (Rounded(*corners)) {
		p.setClipPath(RoundedPath(rect, *corners), Qt::IntersectClip);
	} else {
		clipToRect(rect);
	}
}

void Executor::popClip() {
	if (!_stack.empty() && _stack.back().kind == EntryKind::Clip) {
		popEntry(true);
	}
}

bool Executor::pushGroup(const twlite::Command &command) {
	const auto ink = _inks[_group++];
	auto &p = painter();
	const auto world = (_hidden > 0 || !Shown(command))
		? std::nullopt
		: GroupTransform(command, p.worldTransform());
	if (!world) {
		pushHidden();
		return true;
	} else if (command.opacity < 1.f) {
		return pushLayer(*world, command.opacity, ink);
	}
	p.save();
	p.setWorldTransform(*world);
	_stack.push_back({ .kind = EntryKind::Group, .saved = true });
	return true;
}

bool Executor::pushLayer(
		const QTransform &world,
		float64 opacity,
		const QRectF &ink) {
	auto &parent = painter();
	const auto offset = origin();
	const auto area = LayerArea(parent, offset, *_root.device(), ink);
	if (area.isEmpty()) {
		pushHidden();
		return true;
	}
	const auto bytes = LayerBytes(area.size());
	if (bytes > kMaxLayerBytes - _layerBytes
		|| bytes > kMaxLayerWork - _layerWork) {
		return false;
	}
	auto layer = std::make_unique<Layer>();
	layer->image = QImage(area.size(), QImage::Format_ARGB32_Premultiplied);
	if (layer->image.isNull()) {
		return false;
	}
	layer->image.fill(Qt::transparent);
	if (!layer->painter.begin(&layer->image)) {
		return false;
	}
	PrepareHints(layer->painter);
	layer->painter.setWorldTransform(world
		* QTransform::fromTranslate(
			offset.x() - area.x(),
			offset.y() - area.y()));
	layer->origin = area.topLeft();
	layer->opacity = opacity;
	_layerBytes += bytes;
	_layerWork += bytes;
	_layers.push_back(std::move(layer));
	_stack.push_back({ .kind = EntryKind::Layer });
	return true;
}

void Executor::pushHidden() {
	++_hidden;
	_stack.push_back({ .kind = EntryKind::Hidden });
}

void Executor::popGroup() {
	const auto group = [](const Entry &entry) {
		return (entry.kind != EntryKind::Clip);
	};
	if (!ranges::any_of(_stack, group)) {
		return;
	}
	while (!group(_stack.back())) {
		popEntry(true);
	}
	popEntry(true);
}

void Executor::popEntry(bool composite) {
	const auto entry = _stack.back();
	_stack.pop_back();
	if (entry.saved) {
		painter().restore();
	}
	if (entry.kind == EntryKind::Layer) {
		popLayer(composite);
	} else if (entry.kind == EntryKind::Hidden) {
		--_hidden;
	}
}

void Executor::popLayer(bool composite) {
	const auto layer = std::move(_layers.back());
	_layers.pop_back();
	layer->painter.end();
	_layerBytes -= LayerBytes(layer->image.size());
	if (!composite) {
		return;
	}
	auto &parent = painter();
	parent.save();
	parent.setWorldTransform(QTransform());
	parent.setOpacity(layer->opacity);
	parent.drawImage(layer->origin - origin(), layer->image);
	parent.restore();
}

void Executor::unwind(bool composite) {
	while (!_stack.empty()) {
		popEntry(composite);
	}
}

void Executor::clipToRect(const QRectF &rect) {
	painter().setClipRect(bounded(rect), Qt::IntersectClip);
}

QPainter &Executor::painter() const {
	return _layers.empty() ? _root : _layers.back()->painter;
}

QPoint Executor::origin() const {
	return _layers.empty() ? QPoint() : _layers.back()->origin;
}

QRectF Executor::bounded(const QRectF &rect) const {
	const auto area = limit();
	return area.isEmpty() ? QRectF() : rect.intersected(area);
}

QRectF Executor::limit() const {
	const auto &p = painter();
	auto invertible = false;
	const auto inverse = p.combinedTransform().inverted(&invertible);
	return invertible
		? inverse.mapRect(DeviceLimit(*p.device()))
		: QRectF();
}

bool Executor::reachable(QPointF point) const {
	const auto &p = painter();
	return DeviceLimit(*p.device()).contains(
		p.combinedTransform().map(point));
}

} // namespace

QImage PaintFrame(
		const twlite::DisplayList &list,
		const QtBackend &backend,
		QSize deviceSize,
		float64 cssToDevice) {
	if (!ValidFrame(deviceSize, cssToDevice)) {
		return QImage();
	}
	auto result = QImage(deviceSize, QImage::Format_ARGB32_Premultiplied);
	if (result.isNull()) {
		return QImage();
	}
	result.fill(Qt::transparent);
	auto painter = QPainter();
	if (!painter.begin(&result)) {
		return QImage();
	}
	const auto painted = Executor(painter, backend, cssToDevice).run(list);
	painter.end();
	return painted ? result : QImage();
}

} // namespace Tgw
