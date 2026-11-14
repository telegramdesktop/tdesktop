/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_painter.h"

#include "tgw/tgw_backend.h"
#include "tgw/tgw_emoji.h"
#include "tgw/tgw_emoji_images.h"
#include "tgw/tgw_painter_filters.h"
#include "ui/image/image_prepare.h"

namespace Tgw {
namespace {

constexpr auto kMaxFrameBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxLayerBytes = int64(128 * 1024 * 1024);
constexpr auto kMaxLayerWork = int64(256 * 1024 * 1024);
constexpr auto kMaxFilterWork = int64(1 << 23);
constexpr auto kFilterWorkRatio = 2.;
constexpr auto kMaxFilterWorkRatio = 4.;
constexpr auto kFilterPassWork = int64(4096);
constexpr auto kMaxFilterScratch = int64(64 * 1024 * 1024);
constexpr auto kPixelBytes = 4;
constexpr auto kDeviceMargin = float64(1 << 20);
constexpr auto kMaxGroupStretch = float64(1 << 20);
constexpr auto kInkMargin = 2.;
constexpr auto kKappa = 0.5522847498;
constexpr auto kStopEpsilon = 1e-4;
constexpr auto kMiterLimit = 4.;
constexpr auto kMaxDashSteps = int64(1 << 17);
constexpr auto kDotStep = 1e-3;
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
constexpr auto kMaxShadowLayers = int64(1 << 12);
constexpr auto kMaxShadowWork = int64(1 << 25);
constexpr auto kShadowLayerWork = int64(1 << 14);
constexpr auto kShadowRowWork = int64(96);
constexpr auto kMaskPixelWork = int64(8);
constexpr auto kCompositePixelWork = int64(3);
constexpr auto kClippedShadowWork = int64(8);
constexpr auto kContourReach = 1024.;
constexpr auto kContourCellWork = int64(128);
constexpr auto kShallowContourCells = 3.;
constexpr auto kMaxPathElements = int64(1 << 18);
constexpr auto kMaxPathEdges = int64(1 << 20);
constexpr auto kMaxPathWork = int64(1 << 27);
constexpr auto kElementWork = int64(256);
constexpr auto kStopWork = int64(256);
constexpr auto kStopBytes = int64(4 * sizeof(QGradientStop));
constexpr auto kShapeWork = 4096.;
constexpr auto kDashPointWork = 256.;
constexpr auto kCurveThreshold = 0.25;
constexpr auto kCurveLevels = 9;
constexpr auto kArcCurves = 5.;
constexpr auto kCoordinateLimit = 32767.;
constexpr auto kEdgeSpanLimit = 4. * kCoordinateLimit + 2.;
constexpr auto kCurveSlack = 1e-4;
constexpr auto kClipEdges = 5.;
constexpr auto kOffsetSweeps = 2.;
constexpr auto kMiterTurn = 3.;
constexpr auto kClipWork = 16.;
constexpr auto kRasterCells = 256.;
constexpr auto kRasterBand = 37.;
constexpr auto kRasterBands = 39.;
constexpr auto kRasterAttempts = 8.;
constexpr auto kMapperMargin = 1024.;
// WHY: gray_find_cell() walks a sorted row list per record; Qt rounds to 26.6.
// Nonzero rises need that rounding slack before the 256-subpixel cell bound.
// Win64's first two pools hold 148 and 489 cells, fewer than this Mac's.
constexpr auto kFirstPoolCells = 148.;
constexpr auto kSecondPoolCells = 489.;
constexpr auto kRowSubpixels = 256.;
constexpr auto kOutlineSubpixels = 64.;
constexpr auto kGradientPixel = 8.;
constexpr auto kMaxColorGlyphBytes = int64(64 * 1024 * 1024);
constexpr auto kColorGlyphBytes = 48.;

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

struct Bounds {
	float64 left = std::numeric_limits<float64>::infinity();
	float64 top = std::numeric_limits<float64>::infinity();
	float64 right = -std::numeric_limits<float64>::infinity();
	float64 bottom = -std::numeric_limits<float64>::infinity();
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
		int64 &budget,
		bool dots);

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
	const bool _dots = false;
	QPainterPath _result;
	size_t _index = 0;
	float64 _left = 0.;
	bool _drawing = false;

};

class Outline final {
public:
	Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit);
	Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit,
		const QPen &pen);

	[[nodiscard]] bool add(const QPainterPath &path);
	[[nodiscard]] bool valid() const;
	[[nodiscard]] float64 edges() const;
	[[nodiscard]] float64 spans() const;
	[[nodiscard]] float64 visibleSpans() const;
	[[nodiscard]] float64 rowSpans() const;
	[[nodiscard]] float64 clampedSpans() const;
	[[nodiscard]] float64 density() const;
	[[nodiscard]] float64 rowCells() const;
	[[nodiscard]] float64 alignedSpans() const;
	[[nodiscard]] float64 alignedRowSpans() const;
	[[nodiscard]] float64 verticalRows() const;
	[[nodiscard]] float64 verticalEdges() const;
	[[nodiscard]] float64 coverage() const;
	[[nodiscard]] QRectF extent() const;
	[[nodiscard]] QRectF anchors() const;

private:
	[[nodiscard]] QPointF map(QPointF point);
	[[nodiscard]] float64 clamped(QPointF from, QPointF to) const;
	void count(
		float64 edges,
		float64 spans,
		float64 clamped,
		float64 density,
		float64 top,
		float64 bottom,
		float64 rise,
		QPointF along = QPointF());
	void move(QPointF to);
	void line(QPointF to);
	void curve(QPointF b, QPointF c, QPointF d);
	void join(QPointF direction);
	void cap(QPointF at, QPointF direction);
	void finish();
	[[nodiscard]] float64 quarter();

	const QTransform _device;
	const float64 _width = 0.;
	const float64 _height = 0.;
	const float64 _threshold = 0.;
	const float64 _limit = 0.;
	const bool _stroke = false;
	const float64 _radius = 0.;
	const float64 _deviceRadius = 0.;
	const float64 _reach = 0.;
	const bool _roundJoin = false;
	const Qt::PenCapStyle _cap = Qt::FlatCap;
	bool _aligned = false;
	float64 _quarter = -1.;
	float64 _edges = 0.;
	float64 _spans = 0.;
	float64 _visibleSpans = 0.;
	float64 _rowSpans = 0.;
	float64 _clampedSpans = 0.;
	float64 _density = 0.;
	float64 _rowCells = 0.;
	float64 _alignedSpans = 0.;
	float64 _alignedRowSpans = 0.;
	float64 _verticalRows = 0.;
	float64 _verticalEdges = 0.;
	Bounds _controls;
	Bounds _anchors;
	QPointF _start;
	QPointF _startMapped;
	QPointF _point;
	QPointF _mapped;
	std::optional<QPointF> _first;
	std::optional<QPointF> _last;
	bool _valid = true;
	bool _curved = false;

};

class Executor final {
public:
	Executor(QPainter &root, const QtBackend &backend, float64 scale);

	[[nodiscard]] bool run(const twlite::DisplayList &list);
	[[nodiscard]] const QString &exceeded() const;

private:
	enum class EntryKind : uchar {
		Clip,
		Group,
		Layer,
		Hidden,
		Filter,
	};
	struct Entry {
		EntryKind kind = EntryKind::Clip;
		bool saved = false;
		bool filter = false;
	};
	struct Layer {
		QImage image;
		QPainter painter;
		QPoint origin;
		float64 opacity = 1.;
		std::shared_ptr<const twlite::Filters> filters;
		float64 filterScale = 0.;
	};

	[[nodiscard]] bool execute(const twlite::Command &command);
	void fillShape(const twlite::Command &command, const QBrush &brush);
	void strokeRect(const twlite::Command &command);
	void fillEllipse(const twlite::Command &command);
	void strokeEllipse(const twlite::Command &command);
	void drawText(const twlite::Command &command);
	void drawImage(const twlite::Command &command);
	[[nodiscard]] bool drawPath(const twlite::Command &command);
	[[nodiscard]] bool drawShadow(const twlite::Command &command);
	[[nodiscard]] bool drawMask(
		const ShadowSource &source,
		const MaskLayout &layout,
		QRgb color);
	[[nodiscard]] bool fillShadow(const ShadowSource &source, QRgb color);
	void clipShadow(const ShadowSource &source, const QRectF &ink);
	[[nodiscard]] bool pushClip(const twlite::Command &command);
	void popClip();
	[[nodiscard]] bool pushGroup(const twlite::Command &command);
	[[nodiscard]] bool pushFilter(const twlite::Command &command);
	[[nodiscard]] bool pushLayer(
		const QTransform &world,
		float64 opacity,
		const QRect &area);
	[[nodiscard]] bool admitFilter(const FilterCost &cost, int64 bytes);
	[[nodiscard]] bool chargeShadow(
		const QRectF &area,
		const QRectF &contour,
		float64 contourCells,
		int64 pixelWork,
		int64 maskPixels);
	[[nodiscard]] bool drawBackdrop(const twlite::Command &command);
	[[nodiscard]] bool admitColorGlyphs(
		const QString &text,
		const std::vector<EmojiCell> &cells,
		float64 side);
	void pushHidden();
	void popGroup();
	void popFilter();
	void popEntry(bool composite);
	void popLayer(bool composite);
	void unwind(bool composite);
	[[nodiscard]] bool exceed(QString reason);

	void clipToRect(const QRectF &rect);
	[[nodiscard]] bool clipToPath(const twlite::Path &source);
	[[nodiscard]] QPainter &painter() const;
	[[nodiscard]] QPoint origin() const;
	[[nodiscard]] QRectF bounded(const QRectF &rect) const;
	[[nodiscard]] QRectF frameInk(const QRectF &rect) const;
	[[nodiscard]] QPolygonF around(const QRectF &ink) const;
	[[nodiscard]] QRectF limit() const;
	[[nodiscard]] bool reachable(QPointF point) const;

	QPainter &_root;
	const QtBackend &_backend;
	std::vector<Entry> _stack;
	std::vector<std::unique_ptr<Layer>> _layers;
	std::vector<QRectF> _inks;
	PaintAllowance _allowance;
	size_t _group = 0;
	int64 _layerBytes = 0;
	int64 _layerWork = 0;
	int64 _maxFilterWork = 0;
	int64 _filterWork = 0;
	int64 _shadowWork = kMaxShadowWork;
	int64 _colorGlyphBytes = kMaxColorGlyphBytes;
	int _hidden = 0;
	bool _pathClips = false;
	bool _refused = false;
	QString _exceeded;

};

[[nodiscard]] bool ValidFrame(QSize size, float64 scale) {
	return !size.isEmpty() && std::isfinite(scale) && (scale > 0.);
}

[[nodiscard]] QString PaintLimit(const QString &what, int64 max) {
	return u"Paint limit exceeded: %1 (max %2)"_q.arg(
		what,
		QString::number(max));
}

[[nodiscard]] int64 LayerBytes(QSize size) {
	return int64(size.width()) * size.height() * kPixelBytes;
}

// WHY: higher interface scales multiply device-pixel work by area.
// Keep the ratio-2 allowance below that ratio and cap its growth at the
// calibrated maximum to bound the work admitted on each frame.
[[nodiscard]] int64 FilterWorkBound(float64 cssToDevice) {
	const auto factor = std::clamp(
		cssToDevice,
		kFilterWorkRatio,
		kMaxFilterWorkRatio) / kFilterWorkRatio;
	return int64(kMaxFilterWork * factor * factor);
}

[[nodiscard]] QRectF DeviceLimit(const QPaintDevice &device) {
	return QRectF(0., 0., device.width(), device.height()).adjusted(
		-kDeviceMargin,
		-kDeviceMargin,
		kDeviceMargin,
		kDeviceMargin);
}

[[nodiscard]] QSize DeviceSize(const QPainter &painter) {
	const auto device = painter.device();
	return QSize(device->width(), device->height());
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
		const QRectF &ink,
		float64 spread) {
	const auto visible = VisibleRect(parent);
	if (ink.isEmpty() || visible.isEmpty()) {
		return QRect();
	}
	auto result = QRectF(0., 0., frame.width(), frame.height());
	result = result.intersected(visible.translated(origin).adjusted(
		-spread,
		-spread,
		spread,
		spread));
	result = result.intersected(ink.adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin));
	return result.toAlignedRect();
}

[[nodiscard]] float64 MaxStretch(const QTransform &transform) {
	const auto a = transform.m11();
	const auto b = transform.m12();
	const auto c = transform.m21();
	const auto d = transform.m22();
	return (std::hypot(a + d, c - b) + std::hypot(a - d, b + c)) / 2.;
}

[[nodiscard]] bool KeepsAxes(const QTransform &transform) {
	return (transform.m12() == 0.)
		&& (transform.m21() == 0.)
		&& (transform.m13() == 0.)
		&& (transform.m23() == 0.)
		&& (transform.m33() == 1.)
		&& (transform.type() < QTransform::TxProject);
}

[[nodiscard]] float64 Spread(const QMarginsF &reach) {
	return std::max({
		reach.left(),
		reach.top(),
		reach.right(),
		reach.bottom(),
	});
}

[[nodiscard]] float64 DeviceSpread(
		float64 spread,
		const QPaintDevice &frame) {
	const auto result = spread + kInkMargin;
	const auto most = float64(frame.width()) + frame.height();
	return (result < most) ? result : most;
}

[[nodiscard]] bool Structural(twlite::Cmd kind) {
	using Cmd = twlite::Cmd;
	constexpr auto kinds = std::array{
		Cmd::ClipPush,
		Cmd::ClipPop,
		Cmd::GroupPush,
		Cmd::GroupPop,
		Cmd::FilterPush,
		Cmd::FilterPop,
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
	return (Finite(result)
		&& result.isInvertible()
		&& (MaxStretch(result) <= kMaxGroupStretch))
		? std::make_optional(result)
		: std::nullopt;
}

[[nodiscard]] bool ValidWidth(float width) {
	return std::isfinite(width) && (width > 0.f);
}

[[nodiscard]] QColor ToColor(twlite::Color color) {
	return QColor(color.r, color.g, color.b, color.a);
}

[[nodiscard]] bool FiniteRect(const twlite::Rect &rect) {
	return std::isfinite(rect.x)
		&& std::isfinite(rect.y)
		&& std::isfinite(rect.w)
		&& std::isfinite(rect.h);
}

[[nodiscard]] QRectF ToRect(const twlite::Rect &rect) {
	return (FiniteRect(rect) && !rect.empty())
		? QRectF(rect.x, rect.y, rect.w, rect.h)
		: QRectF();
}

// WHY: glyphs may overhang their line box, so a run is skipped only when
// its box grown by a whole line on every side still misses the clip.
[[nodiscard]] bool TextMayReachClip(
		const QPainter &painter,
		const twlite::Command &command) {
	const auto box = ToRect(command.rect);
	if (box.isEmpty()
		|| (painter.combinedTransform().type() >= QTransform::TxProject)) {
		return true;
	}
	const auto margin = box.height();
	return painter.clipBoundingRect().intersects(
		box.adjusted(-margin, -margin, margin, margin));
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

[[nodiscard]] bool WithinContourReach(
		QSize device,
		const QRectF &deviceBounds) {
	const auto frame = QRectF(QPointF(), QSizeF(device));
	return frame.adjusted(
		-kContourReach,
		-kContourReach,
		kContourReach,
		kContourReach).contains(deviceBounds);
}

// WHY: Qt flattens and rasterizes a whole contour on every layer, which
// ContourCells() prices. Within the device + kContourReach it keeps its pixels;
// past that Qt adds QPathClipper, so cut the exact curves first, a few elements.
[[nodiscard]] QPainterPath NearRoundedPath(
		const QRectF &rect,
		const Corners &corners,
		const QPainter &painter,
		const QRectF &ink) {
	const auto world = painter.combinedTransform();
	const auto device = DeviceSize(painter);
	auto result = RoundedPath(rect, corners);
	if (WithinContourReach(device, world.mapRect(rect))) {
		return result;
	}
	auto invertible = false;
	const auto inverse = world.inverted(&invertible);
	auto area = QPainterPath();
	area.addRect(ink.adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin));
	result = world.map(result).intersected(area);
	return (invertible
		&& WithinContourReach(device, result.controlPointRect()))
		? inverse.map(result)
		: QPainterPath();
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

[[nodiscard]] QBrush PaintBrush(
		int kind,
		QPointF from,
		QPointF to,
		float64 radius,
		QPointF focal,
		const std::vector<twlite::Stop> &stops,
		twlite::Color color) {
	const auto gradient = stops.empty() ? 0 : kind;
	switch (gradient) {
	case 1: return LinearBrush(from, to, stops);
	case 2: return RadialBrush(from, radius, radius, focal, stops);
	}
	return SolidBrush(color);
}

[[nodiscard]] QBrush PathBrush(const twlite::Path &path) {
	return PaintBrush(
		path.gradient,
		QPointF(path.x1, path.y1),
		QPointF(path.x2, path.y2),
		path.r,
		QPointF(path.fx, path.fy),
		path.stops,
		path.fill_color);
}

[[nodiscard]] QBrush StrokeBrush(const twlite::Path &path) {
	const auto &g = path.sg;
	return PaintBrush(
		path.stroke_gradient,
		QPointF(g[0], g[1]),
		QPointF(g[2], g[3]),
		g[4],
		QPointF(g[5], g[6]),
		path.stroke_stops,
		path.stroke_color);
}

[[nodiscard]] int64 StrokeStops(const twlite::Path &path) {
	const auto gradient = (path.stroke_gradient == 1)
		|| (path.stroke_gradient == 2);
	return (path.stroke && gradient) ? int64(path.stroke_stops.size()) : 0;
}

[[nodiscard]] float64 MiterLimit(const twlite::Path &path) {
	return std::isfinite(path.miter_limit)
		? std::max(float64(path.miter_limit), 1.)
		: kMiterLimit;
}

[[nodiscard]] QPen PathPen(const twlite::Path &path) {
	auto result = QPen(
		StrokeBrush(path),
		float64(path.stroke_width),
		Qt::SolidLine,
		PathCap(path.line_cap),
		PathJoin(path.line_join));
	result.setMiterLimit(MiterLimit(path));
	return result;
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

[[nodiscard]] int64 CurvePieces(
		const std::array<QPointF, 4> &curve,
		float64 threshold,
		float64 limit) {
	struct Piece {
		std::array<QPointF, 4> points;
		int level = 0;
	};
	const auto middle = [](QPointF a, QPointF b) {
		return (a + b) * .5;
	};
	auto stack = std::array<Piece, kCurveLevels + 1>();
	stack[0] = Piece{ .points = curve, .level = kCurveLevels };
	auto top = 0;
	auto result = int64();
	while (top >= 0 && result <= limit) {
		const auto [a, b, c, d] = stack[top].points;
		const auto level = stack[top].level;
		const auto dx = d.x() - a.x();
		const auto dy = d.y() - a.y();
		auto length = std::abs(dx) + std::abs(dy);
		auto distance = 0.;
		if (length > 1.) {
			distance = std::abs(dx * (a.y() - b.y()) - dy * (a.x() - b.x()))
				+ std::abs(dx * (a.y() - c.y()) - dy * (a.x() - c.x()));
		} else {
			distance = std::abs(a.x() - b.x())
				+ std::abs(a.y() - b.y())
				+ std::abs(a.x() - c.x())
				+ std::abs(a.y() - c.y());
			length = 1.;
		}
		if (distance < threshold * length || !level) {
			++result;
			--top;
			continue;
		}
		const auto ab = middle(a, b);
		const auto bc = middle(b, c);
		const auto cd = middle(c, d);
		const auto abc = middle(ab, bc);
		const auto bcd = middle(bc, cd);
		const auto abcd = middle(abc, bcd);
		stack[top] = Piece{
			.points = { abcd, bcd, cd, d },
			.level = level - 1,
		};
		stack[++top] = Piece{
			.points = { a, ab, abc, abcd },
			.level = level - 1,
		};
	}
	return result;
}

[[nodiscard]] float64 Span(QPointF from, QPointF to) {
	return std::abs(to.x() - from.x()) + std::abs(to.y() - from.y());
}

void Include(Bounds &bounds, QPointF point) {
	bounds.left = std::min(bounds.left, point.x());
	bounds.top = std::min(bounds.top, point.y());
	bounds.right = std::max(bounds.right, point.x());
	bounds.bottom = std::max(bounds.bottom, point.y());
}

[[nodiscard]] QRectF BoundsRect(const Bounds &bounds, float64 margin) {
	if (!(bounds.left <= bounds.right) || !(bounds.top <= bounds.bottom)) {
		return QRectF();
	}
	return QRectF(
		QPointF(bounds.left, bounds.top),
		QPointF(bounds.right, bounds.bottom)).adjusted(
			-margin,
			-margin,
			margin,
			margin);
}

[[nodiscard]] std::pair<float64, float64> CurveRange(
		float64 a,
		float64 b,
		float64 c,
		float64 d) {
	auto low = std::min(a, d);
	auto high = std::max(a, d);
	const auto include = [&](float64 t) {
		if (t > 0. && t < 1.) {
			const auto s = 1. - t;
			const auto value = s * s * s * a
				+ 3. * s * s * t * b
				+ 3. * s * t * t * c
				+ t * t * t * d;
			low = std::min(low, value);
			high = std::max(high, value);
		}
	};
	const auto qa = 3. * (b - c) + d - a;
	const auto qb = 2. * (a - 2. * b + c);
	const auto qc = b - a;
	if (qa == 0.) {
		if (qb != 0.) {
			include(-qc / qb);
		}
		return { low, high };
	}
	const auto discriminant = qb * qb - 4. * qa * qc;
	if (discriminant >= 0.) {
		const auto root = std::sqrt(discriminant);
		include((-qb + root) / (2. * qa));
		include((-qb - root) / (2. * qa));
	}
	return { low, high };
}

[[nodiscard]] bool BeyondRaster(const QRectF &rect) {
	return std::max({
		-rect.left(),
		-rect.top(),
		rect.right(),
		rect.bottom(),
		rect.width(),
		rect.height(),
	}) > kCoordinateLimit;
}

[[nodiscard]] float64 Turn(QPointF from, QPointF to) {
	return std::atan2(
		std::abs(from.x() * to.y() - from.y() * to.x()),
		from.x() * to.x() + from.y() * to.y());
}

[[nodiscard]] float64 PolygonTurn(const std::array<QPointF, 4> &curve) {
	auto result = 0.;
	auto previous = std::optional<QPointF>();
	for (auto i = 1; i != 4; ++i) {
		const auto leg = curve[i] - curve[i - 1];
		if (leg.isNull()) {
			continue;
		} else if (previous) {
			result += Turn(*previous, leg);
		}
		previous = leg;
	}
	return result;
}

[[nodiscard]] std::optional<QPointF> Tangent(
		QPointF from,
		std::initializer_list<QPointF> to) {
	for (const auto point : to) {
		if (!(point - from).isNull()) {
			return point - from;
		}
	}
	return std::nullopt;
}

[[nodiscard]] float64 RasterWork(
		const Outline &outline,
		QSize size,
		float64 pixelWeight) {
	const auto frame = QRectF(QPointF(), QSizeF(size));
	const auto extent = outline.extent();
	auto edges = outline.edges();
	if (!extent.intersects(frame)) {
		return edges;
	}
	const auto visible = extent.intersected(frame);
	const auto rows = std::ceil(visible.bottom()) - std::floor(visible.top());
	const auto columns = std::ceil(visible.right())
		- std::floor(visible.left());
	auto spans = outline.spans();
	auto iterated = outline.visibleSpans();
	auto density = std::min(columns + 2., outline.density());
	auto rowCells = std::min(columns + 2., outline.rowCells());
	auto clip = 0.;
	const auto clipping = BeyondRaster(extent);
	if (clipping) {
		clip = kClipWork * edges;
		spans = iterated = BeyondRaster(outline.anchors())
			? (outline.clampedSpans() + (kClipEdges - 1.) * edges)
			: std::min(spans, kClipEdges * edges * kEdgeSpanLimit);
		edges *= kClipEdges;
		density = columns + 2.;
		rowCells = density;
	}
	const auto attempts = std::max(
		(density > kRasterCells)
			? std::min(
				kRasterAttempts,
				1. + std::ceil(std::log2(density / kRasterCells)))
			: 1.,
		(rowCells > kSecondPoolCells + 2.)
			? 3.
			: (rowCells > kFirstPoolCells + 2.)
			? 2.
			: 1.);
	const auto bands = std::clamp(
		std::floor(rows / kRasterBand),
		1.,
		kRasterBands);
	const auto first = rows - (bands - 1.) * kRasterBand;
	const auto crowded = first * density / kRasterCells;
	const auto failing = (crowded > 1.)
		? std::min(std::ceil(std::log2(crowded)), std::ceil(std::log2(first)))
		: 0.;
	const auto cells = std::min(rows * (columns + 2.), edges + spans);
	const auto visits = edges + spans;
	const auto splits = 2. * failing * std::ceil(cells / kRasterCells);
	const auto passes = attempts * std::min(2. * rows, bands + splits);
	const auto levels = std::ceil(std::log2(rows)) + 1.;
	const auto overlap = [&](float64 visibleSpans, float64 rowSpans) {
		const auto deep = 4. * rowSpans + (2. * levels + 2.) * visibleSpans;
		const auto shallow = std::exp2(failing + 2.)
			/ std::min(kRasterBand, rows)
			* rowSpans
			+ (2. * failing + 2.) * visibleSpans;
		return attempts * std::min(deep, shallow);
	};
	const auto full = std::min(
		passes * iterated,
		clipping
			? std::numeric_limits<float64>::infinity()
			: overlap(iterated, outline.rowSpans()));
	// WHY: gray_render_line() walks an exactly vertical edge only over its
	// band's rows and a horizontal one as one cell, unless QOutlineMapper
	// first clipped an outline reaching kMapperMargin past the device.
	const auto grown = frame.adjusted(
		-kMapperMargin,
		-kMapperMargin,
		kMapperMargin,
		kMapperMargin);
	// QOutlineMapper also clamps its grown device rect to kCoordinateLimit.
	const auto most = kCoordinateLimit - 2. * kMapperMargin;
	auto lowered = full;
	if (!clipping
		&& frame.width() <= most
		&& frame.height() <= most
		&& grown.contains(extent)
		&& outline.alignedSpans() > 0.) {
		const auto rest = std::max(iterated - outline.alignedSpans(), 0.);
		const auto crossed = std::max(
			outline.rowSpans() - outline.alignedRowSpans(),
			0.);
		lowered = std::min(passes * rest, overlap(rest, crossed))
			+ std::min(passes, attempts * levels) * outline.verticalRows()
			+ 3. * passes * outline.verticalEdges();
	}
	const auto pixels = std::min(rows * columns, outline.coverage());
	return passes * edges
		+ std::min(full, lowered)
		+ pixels * pixelWeight
		+ clip
		+ visits * rowCells;
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

void DrawTextWithEmoji(
		QPainter &p,
		const QtFont &font,
		const QString &text,
		const std::vector<EmojiCell> &cells,
		int alpha) {
	const auto fontSize = float64(font.font.pixelSize());
	const auto side = EmojiAdvance(fontSize)
		* MaxStretch(p.combinedTransform());
	const auto opacity = p.opacity();
	const auto draw = [&](float64 x, const QString &piece, EmojiPtr emoji) {
		if (!emoji) {
			p.drawText(QPointF(x, 0.), piece);
			return;
		}
		p.setOpacity(opacity * alpha / 255.);
		PaintEmoji(p, emoji, EmojiBox(x, fontSize), side);
		p.setOpacity(opacity);
	};
	WalkTextWithEmoji(font.metrics, text, cells, fontSize, draw);
}

[[nodiscard]] QRectF EmojiTextInk(
		const QFontMetricsF &metrics,
		const QString &text,
		const std::vector<EmojiCell> &cells,
		float64 fontSize) {
	auto result = QRectF();
	const auto add = [&](float64 x, const QString &piece, EmojiPtr emoji) {
		result |= emoji
			? EmojiBox(x, fontSize)
			: metrics.boundingRect(piece).translated(x, 0.);
	};
	WalkTextWithEmoji(metrics, text, cells, fontSize, add);
	return result;
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
	const auto cells = FindEmojiCells(text);
	const auto ink = cells.empty()
		? metrics.boundingRect(text)
		: EmojiTextInk(metrics, text, cells, font->font.pixelSize());
	auto transform = QTransform::fromTranslate(command.x, command.y);
	transform.scale(font->scale, font->scale);
	return transform.mapRect(ink.adjusted(
		-margin,
		-margin,
		margin,
		margin));
}

[[nodiscard]] QRectF PathInk(const twlite::Path &path) {
	const auto vector = VectorPath(path.ops, path.points, path.evenodd);
	if (vector.isEmpty()) {
		return QRectF();
	}
	const auto stroked = path.stroke
		&& ValidWidth(path.stroke_width)
		&& (path.stroke_color.a || StrokeStops(path) > 0);
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
	case Cmd::Backdrop: return rect;
	case Cmd::ClipPush:
	case Cmd::ClipPop:
	case Cmd::GroupPush:
	case Cmd::GroupPop:
	case Cmd::FilterPush:
	case Cmd::FilterPop: break;
	}
	return QRectF();
}

[[nodiscard]] QRectF DeviceInk(
		const QTransform &world,
		const QRectF &rect,
		const QRectF &frame,
		const QMarginsF &reach = QMarginsF()) {
	if (rect.isEmpty()) {
		return QRectF();
	}
	const auto result = world.mapRect(rect);
	if (!Finite(result.topLeft()) || !Finite(result.bottomRight())) {
		return frame;
	}
	const auto most = frame.width() + frame.height();
	const auto capped = [&](float64 value) {
		return (value < most) ? value : most;
	};
	return result.adjusted(
		-capped(reach.left()),
		-capped(reach.top()),
		capped(reach.right()),
		capped(reach.bottom())).intersected(frame);
}

[[nodiscard]] float64 ContourCells(
		const QRectF &rect,
		const Corners &corners,
		const QTransform &world,
		QSize device,
		const QRectF &ink) {
	const auto frame = QRectF(QPointF(), QSizeF(device));
	const auto whole = WithinContourReach(device, world.mapRect(rect));
	const auto bounds = whole ? DeviceInk(world, rect, frame) : ink;
	const auto stretch = MaxStretch(world);
	auto curved = 0.;
	auto pieces = 0.;
	for (const auto &corner : {
			corners.topLeft,
			corners.topRight,
			corners.bottomRight,
			corners.bottomLeft }) {
		curved += (std::abs(world.m11()) + std::abs(world.m12())) * corner.x
			+ (std::abs(world.m21()) + std::abs(world.m22())) * corner.y;
		const auto radius = stretch * std::max(corner.x, corner.y);
		pieces += std::min(
			std::exp2(float64(kCurveLevels)),
			2. * std::sqrt(whole
				? radius
				: std::min(radius, bounds.width() + bounds.height())));
	}
	const auto shallow = [](float64 across, float64 down) {
		return down
			? std::min(kRasterCells, std::abs(across / down))
			: 0.;
	};
	const auto density = std::max(
		shallow(world.m11(), world.m12()),
		shallow(world.m21(), world.m22()));
	const auto perimeter = 2. * (bounds.width() + bounds.height());
	return pieces
		+ (1. + kShallowContourCells * density / kRasterCells)
			* (world.isRotating() ? perimeter : std::min(perimeter, curved));
}

[[nodiscard]] QRectF FilterInk(
		const twlite::Command &command,
		const QTransform &world,
		const QRectF &frame) {
	const auto &chain = command.filter;
	const auto scale = std::sqrt(std::abs(world.determinant()));
	const auto reach = (chain && UsableFilters(*chain))
		? FilterReach(*chain, scale)
		: QMarginsF();
	return DeviceInk(world, ToRect(command.rect), frame, reach);
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
			top.ink |= (command.kind == twlite::Cmd::FilterPush)
				? FilterInk(command, top.world, frame)
				: DeviceInk(
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
		NearRoundedPath(
			QRectF(key.x, key.y, key.width, key.height),
			key.corners,
			p,
			QRectF(result.rect())),
		Qt::white);
	p.end();
	return ShadowFromCoverage(std::move(result), layout.radius, key.color);
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
		int64 &budget,
		bool dots)
: _pattern(std::move(pattern))
, _period(ranges::accumulate(_pattern, 0.))
, _offset(Wrap(offset, _period))
, _scale(scale)
, _visible(visible)
, _budget(budget)
, _dots(dots) {
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
			// WHY: QPainterPath drops a line to the point it is at, but
			// a dash of no length is a dot under round or square caps.
			const auto step = (_dots && !(_left > 0.)) ? kDotStep : _left;
			line(start, from + delta * ((done + step) / length));
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

Outline::Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit)
: _device(device)
, _width(size.width())
, _height(size.height())
, _threshold(threshold)
, _limit(limit)
, _aligned(KeepsAxes(device)) {
}

Outline::Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit,
		const QPen &pen)
: _device(device)
, _width(size.width())
, _height(size.height())
, _threshold(threshold)
, _limit(limit)
, _stroke(true)
, _radius(pen.widthF() / 2.)
, _deviceRadius(_radius * MaxStretch(device))
, _reach(StrokeReach(pen.widthF(), pen.miterLimit()) * MaxStretch(device))
, _roundJoin(pen.joinStyle() == Qt::RoundJoin)
, _cap(pen.capStyle())
, _aligned(KeepsAxes(device)
	&& !pen.isCosmetic()
	&& pen.widthF() > 0.
	&& (_cap == Qt::FlatCap || _cap == Qt::SquareCap)
	&& _deviceRadius >= 1.) {
}

bool Outline::add(const QPainterPath &path) {
	if (_stroke && _aligned) {
		const auto exact = [](float64 value) {
			const auto subpixels = value * kOutlineSubpixels;
			return (std::abs(value) <= kCoordinateLimit)
				&& (subpixels == std::trunc(subpixels));
		};
		const auto elements = path.elementCount();
		// QStroker's sides stay exact only for isolated 1/64-grid axis lines.
		_aligned = exact(_radius) && (elements % 2 == 0);
		for (auto i = 0; _aligned && i != elements; i += 2) {
			const auto &from = path.elementAt(i);
			const auto &to = path.elementAt(i + 1);
			_aligned = from.isMoveTo()
				&& to.isLineTo()
				&& ((from.x == to.x) != (from.y == to.y))
				&& exact(from.x)
				&& exact(from.y)
				&& exact(to.x)
				&& exact(to.y);
		}
	}
	for (auto i = 0, count = path.elementCount(); i != count; ++i) {
		const auto &element = path.elementAt(i);
		switch (element.type) {
		case QPainterPath::MoveToElement: move(element); break;
		case QPainterPath::LineToElement: line(element); break;
		case QPainterPath::CurveToElement:
			curve(element, path.elementAt(i + 1), path.elementAt(i + 2));
			i += 2;
			break;
		case QPainterPath::CurveToDataElement: break;
		}
		if (!(_edges <= _limit)) {
			return false;
		}
	}
	finish();
	return _valid && (_edges <= _limit);
}

bool Outline::valid() const {
	return _valid;
}

float64 Outline::edges() const {
	return _edges;
}

float64 Outline::spans() const {
	return _spans;
}

float64 Outline::visibleSpans() const {
	return _visibleSpans;
}

float64 Outline::rowSpans() const {
	return _rowSpans;
}

float64 Outline::clampedSpans() const {
	return _clampedSpans;
}

float64 Outline::density() const {
	return _density;
}

float64 Outline::rowCells() const {
	return _rowCells;
}

float64 Outline::alignedSpans() const {
	return _alignedSpans;
}

float64 Outline::alignedRowSpans() const {
	return _alignedRowSpans;
}

float64 Outline::verticalRows() const {
	return _verticalRows;
}

float64 Outline::verticalEdges() const {
	return _verticalEdges;
}

float64 Outline::coverage() const {
	return _stroke
		? (_spans * (_deviceRadius + 1.))
		: std::numeric_limits<float64>::infinity();
}

QRectF Outline::extent() const {
	return BoundsRect(_controls, _reach);
}

QRectF Outline::anchors() const {
	const auto magnitude = std::max({
		-_controls.left,
		-_controls.top,
		_controls.right,
		_controls.bottom,
	});
	return BoundsRect(_anchors, -(_reach + 1. + magnitude * kCurveSlack));
}

QPointF Outline::map(QPointF point) {
	const auto result = _device.map(point);
	_valid = _valid && Finite(result);
	Include(_controls, result);
	return result;
}

float64 Outline::clamped(QPointF from, QPointF to) const {
	const auto clamp = [&](QPointF point) {
		return QPointF(
			std::clamp(point.x(), 0., _width),
			std::clamp(point.y(), 0., _height));
	};
	return Span(clamp(from), clamp(to));
}

void Outline::count(
		float64 edges,
		float64 spans,
		float64 clamped,
		float64 density,
		float64 top,
		float64 bottom,
		float64 rise,
		QPointF along) {
	_edges += edges;
	_spans += spans;
	_clampedSpans += clamped;
	const auto from = std::max(top - _reach, 0.);
	const auto till = std::min(bottom + _reach, _height);
	if (from <= till) {
		const auto rows = std::ceil(till) - std::floor(from) + 1.;
		_visibleSpans += spans;
		_rowSpans += spans * rows;
		_density += density;
		const auto padded = (rise > 0.)
			? (rise + 1. / kOutlineSubpixels)
			: rise;
		_rowCells += std::min(
			density,
			edges * (kRowSubpixels * std::min(padded, 1.) + 2.));
		if (_aligned && ((along.x() == 0.) != (along.y() == 0.))) {
			_alignedSpans += spans;
			_alignedRowSpans += spans * rows;
			if (along.x() == 0.) {
				_verticalRows += edges * rows;
				_verticalEdges += edges;
			}
		}
	}
}

void Outline::move(QPointF to) {
	finish();
	_start = _point = to;
	_startMapped = _mapped = map(to);
	Include(_anchors, _mapped);
	if (!_stroke) {
		count(1., 0., 0., 0., _mapped.y(), _mapped.y(), 0.);
	}
}

void Outline::line(QPointF to) {
	const auto mapped = map(to);
	Include(_anchors, mapped);
	const auto span = Span(_mapped, mapped);
	const auto top = std::min(_mapped.y(), mapped.y());
	const auto bottom = std::max(_mapped.y(), mapped.y());
	const auto direction = to - _point;
	const auto inside = clamped(_mapped, mapped);
	const auto slope = std::abs(mapped.x() - _mapped.x())
		/ std::max(bottom - top, 1.)
		+ 2.;
	if (!_stroke) {
		count(
			1.,
			span + 1.,
			inside + 1.,
			slope,
			top,
			bottom,
			bottom - top,
			direction);
	} else if (!direction.isNull()) {
		join(direction);
		count(
			2.,
			2. * (span + 1.),
			2. * (inside + 3. * _deviceRadius + 1.),
			2. * slope,
			top,
			bottom,
			bottom - top,
			direction);
		_last = direction;
	}
	_point = to;
	_mapped = mapped;
}

void Outline::curve(QPointF b, QPointF c, QPointF d) {
	_curved = true;
	const auto a = _point;
	const auto points = std::array{ a, b, c, d };
	const auto mapped = std::array{ _mapped, map(b), map(c), map(d) };
	const auto polygon = Span(mapped[0], mapped[1])
		+ Span(mapped[1], mapped[2])
		+ Span(mapped[2], mapped[3]);
	const auto sideways = std::abs(mapped[1].x() - mapped[0].x())
		+ std::abs(mapped[2].x() - mapped[1].x())
		+ std::abs(mapped[3].x() - mapped[2].x());
	const auto [left, right] = CurveRange(
		mapped[0].x(),
		mapped[1].x(),
		mapped[2].x(),
		mapped[3].x());
	const auto [top, bottom] = CurveRange(
		mapped[0].y(),
		mapped[1].y(),
		mapped[2].y(),
		mapped[3].y());
	Include(_anchors, QPointF(left, top));
	Include(_anchors, QPointF(right, bottom));
	const auto pieces = float64(
		CurvePieces(points, _threshold, _limit - _edges));
	const auto across = 3. * (_width + _height + 2. * _deviceRadius);
	if (!_stroke) {
		count(
			pieces,
			polygon + pieces,
			std::min(polygon, across) + pieces,
			sideways + 2. * pieces,
			top,
			bottom,
			bottom - top);
	} else if (const auto start = Tangent(a, { b, c, d })) {
		const auto turn = PolygonTurn(points);
		const auto offset = 2. * turn * std::sqrt(_radius / (3. * _threshold));
		const auto side = pieces + std::ceil(offset) + 1.;
		const auto length = polygon + 1.5 * turn * _deviceRadius;
		join(*start);
		count(
			2. * side,
			2. * (length + side),
			2. * (std::min(length, kOffsetSweeps * across) + side),
			2. * (sideways + 1.5 * turn * _deviceRadius + 2. * side),
			top,
			bottom,
			bottom - top + 2. * _reach);
		_last = -*Tangent(d, { c, b, a });
	}
	_point = d;
	_mapped = mapped[3];
}

void Outline::join(QPointF direction) {
	if (!_last) {
		_first = direction;
		return;
	}
	const auto turn = Turn(*_last, direction);
	const auto y = _mapped.y();
	if (!_roundJoin) {
		const auto miter = std::tan(std::min(turn, kMiterTurn) / 2.);
		const auto tip = _deviceRadius * (1. + miter);
		const auto spans = 4. + 3. * _deviceRadius + 3. * std::min(_reach, tip);
		count(4., spans, spans, spans + 4., y, y, 2. * _reach);
		return;
	}
	const auto shortest = std::min(
		std::hypot(direction.x(), direction.y()),
		std::hypot(_last->x(), _last->y()));
	const auto inside = (shortest <= _radius);
	const auto quarters = std::ceil(turn / (M_PI / 2.))
		+ 1.
		+ (inside ? kArcCurves : 0.);
	const auto pieces = quarters * quarter();
	const auto sweep = turn + (inside ? (2. * M_PI) : 0.);
	const auto spans = 3.
		+ 3. * _deviceRadius
		+ pieces
		+ 1.5 * sweep * _deviceRadius;
	count(3. + pieces, spans, spans, spans + 3. + pieces, y, y, 2. * _reach);
}

void Outline::cap(QPointF at, QPointF direction) {
	switch (_cap) {
	case Qt::RoundCap: {
		const auto pieces = 2. * quarter();
		const auto spans = pieces + 1.5 * M_PI * _deviceRadius;
		count(
			pieces,
			spans,
			spans,
			spans + pieces,
			at.y(),
			at.y(),
			2. * _reach);
	} break;
	case Qt::SquareCap: {
		const auto spans = 6. * _deviceRadius + 3.;
		count(3., spans, spans, spans + 3., at.y(), at.y(), 2. * _reach);
	} break;
	default: {
		const auto length = std::hypot(direction.x(), direction.y());
		const auto end = QPointF(
			-direction.y() / length,
			direction.x() / length) * (2. * _radius);
		const auto across = std::abs(
			_device.m11() * end.x() + _device.m21() * end.y());
		const auto down = std::abs(
			_device.m12() * end.x() + _device.m22() * end.y());
		const auto straight = !_curved
			&& std::isfinite(across)
			&& std::isfinite(down)
			&& (across > 0. || down > 0.);
		const auto spans = 3. * _deviceRadius + 1.;
		count(
			1.,
			spans,
			spans,
			spans + 1.,
			at.y(),
			at.y(),
			straight ? std::min(across, down) : (2. * _reach));
	} break;
	}
}

void Outline::finish() {
	if (!_stroke) {
		if (_point != _start) {
			const auto top = std::min(_mapped.y(), _startMapped.y());
			const auto bottom = std::max(_mapped.y(), _startMapped.y());
			const auto slope = std::abs(_startMapped.x() - _mapped.x())
				/ std::max(bottom - top, 1.)
				+ 2.;
			count(
				1.,
				Span(_mapped, _startMapped) + 1.,
				clamped(_mapped, _startMapped) + 1.,
				slope,
				top,
				bottom,
				bottom - top,
				_start - _point);
		}
	} else if (_first) {
		if (_point == _start) {
			join(*_first);
		} else {
			cap(_startMapped, *_first);
			cap(_mapped, *_last);
		}
	}
	_first = _last = std::nullopt;
	_curved = false;
}

float64 Outline::quarter() {
	if (_quarter < 0.) {
		const auto radius = _radius;
		const auto handle = radius * kKappa;
		const auto straight = std::array{
			QPointF(radius, 0.),
			QPointF(radius, handle),
			QPointF(handle, radius),
			QPointF(0., radius),
		};
		auto turned = straight;
		for (auto &point : turned) {
			point = QPointF(point.x() - point.y(), point.x() + point.y())
				* std::sqrt(.5);
		}
		const auto most = float64(kMaxCurvePoints);
		_quarter = float64(std::max(
			CurvePieces(straight, _threshold, most),
			CurvePieces(turned, _threshold, most)));
	}
	return _quarter;
}

Executor::Executor(
		QPainter &root,
		const QtBackend &backend,
		float64 scale)
: _root(root)
, _backend(backend)
, _maxFilterWork(FilterWorkBound(scale))
, _filterWork(_maxFilterWork) {
	PrepareHints(_root);
	_root.scale(scale, scale);
}

bool Executor::run(const twlite::DisplayList &list) {
	using Cmd = twlite::Cmd;
	auto passes = int64();
	auto layers = int64();
	for (const auto &command : list) {
		const auto vector = (command.kind == Cmd::Path)
			|| (command.kind == Cmd::ClipPush);
		const auto filter = (command.kind == Cmd::FilterPush)
			|| (command.kind == Cmd::Backdrop);
		if (vector && command.path) {
			_pathClips |= (command.kind == Cmd::ClipPush);
			const auto &path = *command.path;
			if (!_allowance.chargeElements(int64(path.ops.size()))
				|| !_allowance.chargeStops(StrokeStops(path))) {
				return false;
			}
		} else if (filter && command.filter && !command.filter->empty()) {
			++passes;
		} else if (command.kind == Cmd::Shadow) {
			++layers;
		}
	}
	if (passes > _filterWork / kFilterPassWork) {
		return exceed(PaintLimit(
			u"%1 filter passes in one frame"_q.arg(passes),
			_filterWork / kFilterPassWork));
	}
	_filterWork -= passes * kFilterPassWork;
	if (layers > kMaxShadowLayers) {
		return exceed(PaintLimit(
			u"%1 shadow layers in one frame"_q.arg(layers),
			kMaxShadowLayers));
	}
	const auto device = _root.device();
	_inks = GroupInks(
		list,
		_backend,
		_root.worldTransform(),
		QRectF(0., 0., device->width(), device->height()));
	for (const auto &command : list) {
		if (!execute(command) || _refused) {
			unwind(false);
			return false;
		}
	}
	unwind(true);
	return !_refused;
}

const QString &Executor::exceeded() const {
	return _exceeded.isEmpty() ? _allowance.exceeded() : _exceeded;
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
	case Cmd::ClipPush: return pushClip(command);
	case Cmd::ClipPop: popClip(); break;
	case Cmd::Path: return drawPath(command);
	case Cmd::GroupPush: return pushGroup(command);
	case Cmd::GroupPop: popGroup(); break;
	case Cmd::Shadow: return drawShadow(command);
	case Cmd::FilterPush: return pushFilter(command);
	case Cmd::FilterPop: popFilter(); break;
	case Cmd::Backdrop: return drawBackdrop(command);
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
	if (p.hasClipping() && !TextMayReachClip(p, command)) {
		return;
	}
	const auto text = QString::fromUtf8(
		command.text.data(),
		int(command.text.size()));
	const auto cells = FindEmojiCells(text);
	const auto side = font->font.pixelSize()
		* font->scale
		* MaxStretch(p.combinedTransform());
	if (!admitColorGlyphs(text, cells, side)) {
		return;
	}
	p.save();
	p.translate(baseline);
	if (font->scale != 1.) {
		p.scale(font->scale, font->scale);
	}
	p.setFont(font->font);
	p.setPen(ToColor(command.color));
	if (cells.empty()) {
		p.drawText(QPointF(), text);
	} else {
		DrawTextWithEmoji(p, *font, text, cells, command.color.a);
	}
	p.restore();
}

void Executor::drawImage(const twlite::Command &command) {
	const auto image = command.canvas
		? _backend.canvas(command.canvas)
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
	const auto path = VectorPath(source->ops, source->points, source->evenodd);
	if (path.isEmpty()) {
		return true;
	}
	auto &p = painter();
	const auto brush = source->fill ? PathBrush(*source) : QBrush();
	if (brush.style() != Qt::NoBrush) {
		if (!_allowance.chargeFill(p, path, brush)) {
			return false;
		}
		p.fillPath(path, brush);
	}
	if (!source->stroke || !ValidWidth(source->stroke_width)) {
		return true;
	}
	const auto pen = PathPen(*source);
	if (pen.brush().style() == Qt::NoBrush) {
		return true;
	}
	auto dashes = DashPattern(source->dashes);
	if (dashes.empty()) {
		if (!_allowance.chargeStroke(p, path, pen)) {
			return false;
		}
		p.strokePath(path, pen);
		return true;
	}
	return _allowance.strokeDashes(
		p,
		path,
		pen,
		std::move(dashes),
		std::isfinite(source->dash_offset) ? source->dash_offset : 0.);
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
		return fillShadow(source, color);
	}
	const auto layout = ChooseMaskLayout(
		source.area.size(),
		blur,
		MaxStretch(painter().combinedTransform()));
	if (!layout) {
		return true;
	} else if (layout->radius < 1) {
		return fillShadow(source, color);
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
	const auto ink = frameInk(source.inset
		? visible.intersected(source.box)
		: visible);
	if (ink.isEmpty()
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
	const auto maskPixels = image.isNull()
		? int64(layout.size.width()) * layout.size.height()
		: int64();
	auto cells = ContourCells(
		source.box,
		source.boxCorners,
		p.combinedTransform(),
		DeviceSize(p),
		ink);
	if (image.isNull()) {
		cells += ContourCells(
			QRectF(key.x, key.y, key.width, key.height),
			key.corners,
			QTransform::fromTranslate(layout.padding, layout.padding).scale(
				layout.scale,
				layout.scale),
			layout.size,
			QRectF(QPointF(), QSizeF(layout.size)));
	}
	if (!chargeShadow(
			visible,
			source.box,
			cells,
			kCompositePixelWork,
			maskPixels)) {
		return false;
	}
	if (image.isNull()) {
		const auto bytes = BlurBytes(layout.size);
		if (bytes > kMaxLayerBytes - _layerBytes) {
			return exceed(PaintLimit(
				u"%1 active layer bytes at once"_q.arg(_layerBytes + bytes),
				kMaxLayerBytes));
		}
		image = PrepareShadow(key);
		if (image.isNull()) {
			return false;
		}
		Shadows().insert(key, image);
	}
	p.save();
	clipShadow(source, ink);
	p.drawImage(visible, image, SourceRect(layout.size, target, visible));
	p.restore();
	return true;
}

bool Executor::fillShadow(const ShadowSource &source, QRgb color) {
	const auto drawn = source.inset ? source.box : source.area;
	const auto ink = frameInk(drawn);
	if (ink.isEmpty()) {
		return true;
	}
	const auto shape = source.shape.translated(source.area.topLeft());
	auto &p = painter();
	const auto world = p.combinedTransform();
	const auto device = DeviceSize(p);
	auto cells = ContourCells(
		shape,
		source.shapeCorners,
		world,
		device,
		ink);
	cells += ContourCells(
		source.box,
		source.boxCorners,
		world,
		device,
		ink);
	if (!chargeShadow(
			drawn,
			source.inset ? shape : source.box,
			cells,
			1,
			0)) {
		return false;
	}
	auto path = source.shape.isEmpty()
		? QPainterPath()
		: NearRoundedPath(shape, source.shapeCorners, p, ink);
	if (source.inset) {
		path.addPolygon(around(ink));
	}
	path.setFillRule(Qt::OddEvenFill);
	p.save();
	clipShadow(source, ink);
	p.fillPath(path, QColor::fromRgba(color));
	p.restore();
	return true;
}

void Executor::clipShadow(const ShadowSource &source, const QRectF &ink) {
	if (source.inset && !Rounded(source.boxCorners)) {
		clipToRect(source.box);
		return;
	}
	auto &p = painter();
	auto path = NearRoundedPath(source.box, source.boxCorners, p, ink);
	if (!source.inset) {
		path.addPolygon(around(ink));
		path.setFillRule(Qt::OddEvenFill);
	}
	p.setClipPath(path, Qt::IntersectClip);
}

bool Executor::drawBackdrop(const twlite::Command &command) {
	const auto &chain = command.filter;
	if (!chain || chain->empty()) {
		return true;
	} else if (!UsableFilters(*chain) || !FiniteRect(command.rect)) {
		return false;
	}
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty()) {
		return true;
	}
	auto &p = painter();
	const auto local = p.combinedTransform();
	const auto scale = std::sqrt(std::abs(local.determinant()));
	const auto corners = NormalizedCorners(rect, command.radii);
	if (!corners || !Finite(local) || !std::isfinite(scale)) {
		return false;
	}
	const auto target = dynamic_cast<QImage*>(p.device());
	if (!target || !local.isInvertible()) {
		return true;
	}
	const auto spread = Spread(FilterReach(*chain, scale));
	const auto sample = DeviceInk(
		local,
		rect,
		QRectF(target->rect()),
		QMarginsF(spread, spread, spread, spread));
	const auto from = LayerArea(
		p,
		QPoint(),
		*target,
		sample,
		DeviceSpread(spread, *target));
	if (from.isEmpty()) {
		return true;
	}
	const auto bytes = LayerBytes(from.size());
	const auto cost = MeasureFilters(*chain, from.size(), scale);
	if (!cost || !admitFilter(*cost, bytes)) {
		return false;
	}
	_layerWork += bytes;
	auto copy = target->copy(from);
	if (copy.isNull() || !ApplyFilters(copy, *chain, scale)) {
		return false;
	}
	p.save();
	if (Rounded(*corners)) {
		p.setClipPath(RoundedPath(rect, *corners), Qt::IntersectClip);
	} else {
		clipToRect(rect);
	}
	p.setWorldTransform(QTransform());
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.drawImage(from.topLeft(), copy);
	p.restore();
	return true;
}

bool Executor::pushClip(const twlite::Command &command) {
	if (_hidden > 0) {
		_stack.push_back({ .kind = EntryKind::Clip });
		return true;
	}
	auto &p = painter();
	p.save();
	_stack.push_back({ .kind = EntryKind::Clip, .saved = true });
	if (command.path) {
		return clipToPath(*command.path);
	}
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
	return true;
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
		const auto area = LayerArea(p, origin(), *_root.device(), ink, 0.);
		return pushLayer(*world, command.opacity, area);
	}
	p.save();
	p.setWorldTransform(*world);
	_stack.push_back({ .kind = EntryKind::Group, .saved = true });
	return true;
}

bool Executor::pushFilter(const twlite::Command &command) {
	const auto &chain = command.filter;
	if (_hidden > 0 || !chain || chain->empty()) {
		_stack.push_back({ .kind = EntryKind::Filter, .filter = true });
		return true;
	} else if (!UsableFilters(*chain) || !FiniteRect(command.rect)) {
		return false;
	}
	auto &p = painter();
	const auto offset = origin();
	const auto device = p.combinedTransform()
		* QTransform::fromTranslate(offset.x(), offset.y());
	const auto scale = std::sqrt(std::abs(device.determinant()));
	if (!Finite(device) || !std::isfinite(scale)) {
		return false;
	}
	const auto &frame = *_root.device();
	const auto rect = ToRect(command.rect);
	const auto reach = FilterReach(*chain, scale);
	const auto area = (rect.isEmpty() || !(scale > 0.))
		? QRect()
		: LayerArea(
			p,
			offset,
			frame,
			DeviceInk(
				device,
				rect,
				QRectF(0., 0., frame.width(), frame.height()),
				reach),
			DeviceSpread(Spread(reach), frame));
	if (area.isEmpty()) {
		pushHidden();
		_stack.back().filter = true;
		return true;
	}
	const auto cost = MeasureFilters(*chain, area.size(), scale);
	if (!cost
		|| !admitFilter(*cost, LayerBytes(area.size()))
		|| !pushLayer(p.worldTransform(), 1., area)) {
		return false;
	}
	auto &layer = *_layers.back();
	layer.filters = chain;
	layer.filterScale = scale;
	_stack.back().filter = true;
	return true;
}

bool Executor::pushLayer(
		const QTransform &world,
		float64 opacity,
		const QRect &area) {
	if (area.isEmpty()) {
		pushHidden();
		return true;
	}
	const auto offset = origin();
	const auto bytes = LayerBytes(area.size());
	if (bytes > kMaxLayerBytes - _layerBytes) {
		return exceed(PaintLimit(
			u"%1 active layer bytes at once"_q.arg(_layerBytes + bytes),
			kMaxLayerBytes));
	} else if (bytes > kMaxLayerWork - _layerWork) {
		return exceed(PaintLimit(
			u"%1 cumulative layer bytes in one frame"_q.arg(
				_layerWork + bytes),
			kMaxLayerWork));
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

bool Executor::admitFilter(const FilterCost &cost, int64 bytes) {
	const auto total = bytes + cost.scratch;
	if (cost.work > _filterWork) {
		return exceed(PaintLimit(
			u"filter work of %1 units in one frame"_q.arg(
				_maxFilterWork - _filterWork + cost.work),
			_maxFilterWork));
	} else if (cost.scratch > kMaxFilterScratch) {
		return exceed(PaintLimit(
			u"filter scratch of %1 bytes in one filter pass"_q.arg(
				cost.scratch),
			kMaxFilterScratch));
	} else if (total > kMaxLayerBytes - _layerBytes) {
		return exceed(PaintLimit(
			u"%1 active layer bytes at once"_q.arg(_layerBytes + total),
			kMaxLayerBytes));
	} else if (total > kMaxLayerWork - _layerWork) {
		return exceed(PaintLimit(
			u"%1 cumulative layer bytes in one frame"_q.arg(
				_layerWork + total),
			kMaxLayerWork));
	}
	_filterWork -= cost.work;
	_layerWork += cost.scratch;
	return true;
}

bool Executor::chargeShadow(
		const QRectF &area,
		const QRectF &contour,
		float64 contourCells,
		int64 pixelWork,
		int64 maskPixels) {
	const auto &p = painter();
	const auto world = p.combinedTransform();
	const auto ink = frameInk(area);
	const auto covered = ink.isEmpty() ? QRect() : ink.toAlignedRect();
	const auto pixels = int64(covered.width()) * covered.height();
	const auto reached = WithinContourReach(
			DeviceSize(p),
			world.mapRect(contour))
		? (ink | frameInk(contour)).toAlignedRect()
		: covered;
	const auto drawn = int64(reached.height()) * kShadowRowWork
		+ pixels * pixelWork;
	const auto cells = std::max(contourCells - kRasterCells, 0.);
	// A clip path's spans slow every row and pixel the layer draws.
	const auto work = kShadowLayerWork
		+ drawn * (_pathClips ? kClippedShadowWork : 1)
		+ int64(cells) * kContourCellWork
		+ maskPixels * kMaskPixelWork;
	if (work > _shadowWork) {
		return exceed(PaintLimit(
			u"shadow work of %1 units in one frame"_q.arg(
				kMaxShadowWork - _shadowWork + work),
			kMaxShadowWork));
	}
	_shadowWork -= work;
	return true;
}

// Qt rasterizes and caches a colour glyph at its device size, clipped or not.
bool Executor::admitColorGlyphs(
		const QString &text,
		const std::vector<EmojiCell> &cells,
		float64 side) {
	auto count = int64();
	auto from = 0;
	const auto add = [&](int till) {
		count += ColorGlyphCandidates(
			QStringView(text).mid(from, till - from));
	};
	for (const auto &cell : cells) {
		if (cell.emoji) {
			add(cell.from);
			from = cell.till;
		}
	}
	add(int(text.size()));
	if (!count) {
		return true;
	}
	const auto bytes = kColorGlyphBytes * side * side * count;
	if (!(bytes <= float64(_colorGlyphBytes))) {
		return false;
	}
	_colorGlyphBytes -= int64(std::ceil(bytes));
	return true;
}

void Executor::pushHidden() {
	++_hidden;
	_stack.push_back({ .kind = EntryKind::Hidden });
}

void Executor::popGroup() {
	const auto group = [](const Entry &entry) {
		return (entry.kind != EntryKind::Clip) && !entry.filter;
	};
	if (!ranges::any_of(_stack, group)) {
		return;
	}
	while (!group(_stack.back())) {
		popEntry(true);
	}
	popEntry(true);
}

void Executor::popFilter() {
	for (auto i = _stack.size(); i != 0; --i) {
		const auto entry = _stack[i - 1];
		if (entry.filter) {
			while (_stack.size() >= i) {
				popEntry(true);
			}
			return;
		} else if (entry.kind != EntryKind::Clip) {
			return;
		}
	}
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
	if (!composite || _refused) {
		return;
	} else if (layer->filters
		&& !ApplyFilters(layer->image, *layer->filters, layer->filterScale)) {
		_refused = true;
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

bool Executor::exceed(QString reason) {
	_exceeded = std::move(reason);
	return false;
}

void Executor::clipToRect(const QRectF &rect) {
	painter().setClipRect(bounded(rect), Qt::IntersectClip);
}

bool Executor::clipToPath(const twlite::Path &source) {
	auto &p = painter();
	const auto path = VectorPath(source.ops, source.points, source.evenodd);
	if (path.isEmpty()) {
		p.setClipRect(QRectF(), Qt::IntersectClip);
		return true;
	} else if (!_allowance.chargeFill(p, path, QBrush(Qt::SolidPattern))) {
		return false;
	}
	p.setClipPath(path, Qt::IntersectClip);
	return true;
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

QRectF Executor::frameInk(const QRectF &rect) const {
	const auto &p = painter();
	return DeviceInk(
		p.combinedTransform(),
		rect,
		QRectF(QPointF(), QSizeF(DeviceSize(p))));
}

// A polygon: a local bounding rect explodes under a near-singular matrix.
QPolygonF Executor::around(const QRectF &ink) const {
	auto invertible = false;
	const auto inverse = painter().combinedTransform().inverted(&invertible);
	return invertible
		? inverse.map(QPolygonF(ink.adjusted(
			-kInkMargin,
			-kInkMargin,
			kInkMargin,
			kInkMargin)))
		: QPolygonF();
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

PaintAllowance::PaintAllowance()
: _elements(kMaxPathElements)
, _edges(kMaxPathEdges)
, _work(kMaxPathWork)
, _dashSteps(kMaxDashSteps)
, _flattenBytes(kMaxFlattenBytes) {
}

bool PaintAllowance::chargeElements(int64 count) {
	if (count < 0) {
		return false;
	} else if (count > _elements) {
		return exceed(PaintLimit(
			u"%1 path elements in one frame"_q.arg(
				kMaxPathElements - _elements + count),
			kMaxPathElements));
	} else if (count > _work / kElementWork) {
		return exceed(PaintLimit(
			u"path work of %1 units in one frame"_q.arg(
				kMaxPathWork - _work + count * kElementWork),
			kMaxPathWork));
	}
	_elements -= count;
	_work -= count * kElementWork;
	return true;
}

bool PaintAllowance::chargeStops(int64 count) {
	if (count < 0) {
		return false;
	} else if (count > _work / kStopWork) {
		return exceed(PaintLimit(
			u"path work of %1 units in one frame"_q.arg(
				kMaxPathWork - _work + count * kStopWork),
			kMaxPathWork));
	} else if (count > _flattenBytes / kStopBytes) {
		return exceed(PaintLimit(
			u"%1 flattening bytes in one frame"_q.arg(
				kMaxFlattenBytes - _flattenBytes + count * kStopBytes),
			kMaxFlattenBytes));
	}
	_work -= count * kStopWork;
	_flattenBytes -= count * kStopBytes;
	return true;
}

bool PaintAllowance::chargeFill(
		const QPainter &painter,
		const QPainterPath &path,
		const QBrush &brush) {
	return chargeShape(
		path,
		painter.combinedTransform(),
		DeviceSize(painter),
		nullptr,
		brush.style());
}

bool PaintAllowance::chargeStroke(
		const QPainter &painter,
		const QPainterPath &path,
		const QPen &pen) {
	return chargeShape(
		path,
		painter.combinedTransform(),
		DeviceSize(painter),
		&pen,
		pen.brush().style());
}

bool PaintAllowance::strokeDashes(
		QPainter &painter,
		const QPainterPath &path,
		const QPen &pen,
		std::vector<float64> dashes,
		float64 offset) {
	auto &p = painter;
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
		_dashSteps,
		pen.capStyle() != Qt::FlatCap);
	const auto scaled = QTransform::fromScale(scale, scale);
	if (!ReserveFlattening(path, _flattenBytes)) {
		return exceed(PaintLimit(
			u"more than %1 flattening bytes in one frame"_q.arg(
				kMaxFlattenBytes),
			kMaxFlattenBytes));
	}
	const auto polygons = path.toSubpathPolygons(scaled);
	auto points = 0.;
	for (const auto &polygon : polygons) {
		points += polygon.size();
	}
	if (!charge(0., points * kDashPointWork)) {
		return false;
	}
	for (const auto &polygon : polygons) {
		if (!dasher.add(polygon)) {
			return exceed(PaintLimit(
				u"more than %1 dash steps in one frame"_q.arg(kMaxDashSteps),
				kMaxDashSteps));
		}
	}
	auto stroker = QPainterPathStroker(pen);
	stroker.setWidth(width);
	auto outlinePen = pen;
	outlinePen.setWidthF(width);
	const auto toDevice = QTransform::fromScale(1. / scale, 1. / scale)
		* p.combinedTransform();
	if (!chargeShape(
			dasher.result(),
			toDevice,
			DeviceSize(p),
			&outlinePen,
			pen.brush().style())) {
		return false;
	}
	const auto outline = stroker.createStroke(dasher.result());
	auto brush = pen.brush();
	brush.setTransform(brush.transform() * scaled);
	p.save();
	p.scale(1. / scale, 1. / scale);
	p.fillPath(outline, brush);
	p.restore();
	return true;
}

const QString &PaintAllowance::exceeded() const {
	return _exceeded;
}

bool PaintAllowance::chargeShape(
		const QPainterPath &path,
		const QTransform &device,
		QSize size,
		const QPen *pen,
		Qt::BrushStyle style) {
	const auto stretch = MaxStretch(device);
	if (!std::isfinite(stretch)) {
		return false;
	}
	const auto threshold = (stretch > 0.)
		? (kCurveThreshold / stretch)
		: kCurveThreshold;
	const auto limit = float64(_edges);
	auto outline = pen
		? Outline(device, size, threshold, limit, *pen)
		: Outline(device, size, threshold, limit);
	if (!outline.add(path)) {
		if (outline.valid() && outline.edges() > limit) {
			return exceed(PaintLimit(
				u"more than %1 path edges in one frame"_q.arg(kMaxPathEdges),
				kMaxPathEdges));
		}
		return false;
	}
	const auto weight = (style == Qt::SolidPattern) ? 1. : kGradientPixel;
	return charge(
		outline.edges(),
		RasterWork(outline, size, weight) + kShapeWork);
}

bool PaintAllowance::charge(float64 edges, float64 work) {
	if (!std::isfinite(edges) || !std::isfinite(work)) {
		return false;
	} else if (!(edges <= float64(_edges))) {
		return exceed(PaintLimit(
			u"%1 path edges in one frame"_q.arg(
				std::ceil(float64(kMaxPathEdges - _edges) + edges),
				0,
				'f',
				0),
			kMaxPathEdges));
	} else if (!(work <= float64(_work))) {
		return exceed(PaintLimit(
			u"path work of %1 units in one frame"_q.arg(
				std::ceil(float64(kMaxPathWork - _work) + work),
				0,
				'f',
				0),
			kMaxPathWork));
	}
	_edges -= int64(std::ceil(edges));
	_work -= int64(std::ceil(work));
	return true;
}

bool PaintAllowance::exceed(QString reason) {
	_exceeded = std::move(reason);
	return false;
}

void PrepareHints(QPainter &painter) {
	painter.setRenderHints(QPainter::Antialiasing
		| QPainter::SmoothPixmapTransform
		| QPainter::TextAntialiasing);
}

bool Finite(QPointF point) {
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool Finite(const QTransform &transform) {
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

QBrush GradientBrush(
		QGradient &gradient,
		const std::vector<twlite::Stop> &stops,
		bool reversed) {
	return StopsBrush(gradient, StrictStops(stops, reversed));
}

QBrush LinearBrush(
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

QBrush ConicBrush(
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
	return GradientBrush(gradient, stops, true);
}

QPainterPath VectorPath(
		const std::vector<twlite::Path::Op> &ops,
		const std::vector<float> &points,
		bool evenodd) {
	auto result = QPainterPath();
	result.setFillRule(evenodd ? Qt::OddEvenFill : Qt::WindingFill);
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	const auto point = [&](size_t at) {
		return QPointF(points[at], points[at + 1]);
	};
	auto index = size_t();
	auto started = false;
	for (const auto op : ops) {
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

Qt::PenCapStyle PathCap(int cap) {
	constexpr auto styles = std::array{
		Qt::FlatCap,
		Qt::RoundCap,
		Qt::SquareCap,
	};
	return (cap >= 0 && cap < int(styles.size()))
		? styles[cap]
		: styles.front();
}

Qt::PenJoinStyle PathJoin(int join) {
	constexpr auto styles = std::array{
		Qt::SvgMiterJoin,
		Qt::RoundJoin,
		Qt::BevelJoin,
	};
	return (join >= 0 && join < int(styles.size()))
		? styles[join]
		: styles.front();
}

std::vector<float64> DashPattern(const std::vector<float> &dashes) {
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

float64 StrokeReach(float64 width, float64 miterLimit) {
	return width * std::max(miterLimit, 2.) / 2.;
}

int StackRadius(float64 sigma) {
	return int(base::SafeRound(std::sqrt(1. + 6. * sigma * sigma) - 1.));
}

QImage ShadowFromCoverage(QImage mask, int radius, QRgb color) {
	return ColorizeMask(Images::BlurLargeImage(std::move(mask), radius), color);
}

PaintedFrame PaintFrame(
		const twlite::DisplayList &list,
		const QtBackend &backend,
		QSize deviceSize,
		float64 cssToDevice) {
	if (!ValidFrame(deviceSize, cssToDevice)) {
		return {};
	}
	const auto pixels = int64(deviceSize.width()) * deviceSize.height();
	if (pixels > kMaxFrameBytes / kPixelBytes) {
		return {
			.limit = PaintLimit(
				u"a frame of %1 pixels"_q.arg(pixels),
				kMaxFrameBytes / kPixelBytes),
		};
	}
	auto result = QImage(deviceSize, QImage::Format_ARGB32_Premultiplied);
	if (result.isNull()) {
		return {};
	}
	result.fill(Qt::transparent);
	auto painter = QPainter();
	if (!painter.begin(&result)) {
		return {};
	}
	auto executor = Executor(painter, backend, cssToDevice);
	const auto painted = executor.run(list);
	painter.end();
	if (!painted) {
		return { .limit = executor.exceeded() };
	}
	return { .image = std::move(result) };
}

std::optional<QString> RepaintFrame(
		const twlite::DisplayList &list,
		const QtBackend &backend,
		QImage &frame,
		QRect area,
		float64 cssToDevice) {
	area &= QRect(QPoint(), frame.size());
	if (!ValidFrame(frame.size(), cssToDevice)
		|| (frame.format() != QImage::Format_ARGB32_Premultiplied)) {
		return std::nullopt;
	} else if (area.isEmpty()) {
		return QString();
	}
	auto painter = QPainter();
	if (!painter.begin(&frame)) {
		return std::nullopt;
	}
	painter.setClipRect(area);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.fillRect(area, Qt::transparent);
	painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
	auto executor = Executor(painter, backend, cssToDevice);
	const auto painted = executor.run(list);
	painter.end();
	if (painted) {
		return QString();
	} else if (executor.exceeded().isEmpty()) {
		return std::nullopt;
	}
	return executor.exceeded();
}

} // namespace Tgw
