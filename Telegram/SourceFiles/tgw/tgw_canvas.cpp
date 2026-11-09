/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_canvas.h"

#include <QtGui/QRawFont>
#include <QtGui/QTextLayout>

namespace Tgw {
namespace {

constexpr auto kPixelBytes = 4;
constexpr auto kMaxSide = 8192;
constexpr auto kMaxBackendBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxTotalBytes = int64(128 * 1024 * 1024);
constexpr auto kMaxLayerBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxReadBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxPixelWork = int64(1) << 25;
constexpr auto kLayerPasses = int64(1);
constexpr auto kMaskPasses = int64(1);
constexpr auto kShadowPasses = int64(8);
constexpr auto kImagePasses = int64(2);
constexpr auto kSnapshotPasses = int64(1);
constexpr auto kLayerWork = int64(1024);
constexpr auto kOperationWork = int64(1024);
constexpr auto kPainterWork = int64(1024);
constexpr auto kGlyphWork = int64(1024);
constexpr auto kOutlineWork = int64(4 * 1024);
constexpr auto kFirstGlyphWork = int64(48 * 1024);
constexpr auto kFirstGlyphArea = 32.;
constexpr auto kGlyphPasses = int64(16);
constexpr auto kGlyphSlack = 2.;
constexpr auto kGlyphPadding = int64(6);
constexpr auto kUniformStretch = 0.999;
constexpr auto kGlyphCacheWidth = int64(256);
constexpr auto kSubpixelVariants = int64(12);
constexpr auto kMaxGlyphSize = 64.;
constexpr auto kInkMargin = 2.;
constexpr auto kMaskMargin = 2;
constexpr auto kMaxShadowBlur = 100.;
constexpr auto kMiterLimit = 10.;
constexpr auto kQuarterTurn = 90.;
constexpr auto kDegrees = 180. / M_PI;
constexpr auto kLimitKind = std::string_view("canvas-limit");
constexpr auto kModeKind = std::string_view("canvas-composite: ");
constexpr auto kWorkDetail = std::string_view(
	"canvas drawing exceeded the work allowance of a message,"
	" the rest of its drawing was skipped");
constexpr auto kMemoryDetail = std::string_view(
	"a canvas exceeded the canvas memory allowance and stays transparent");
constexpr auto kModeDetail = std::string_view(
	" is not supported, drawn as source-over");

struct UnsupportedMode {
	twlite::Composite composite = twlite::Composite::SourceOver;
	std::string_view name;
};

struct Shadow {
	QPointF offset;
	QRgb color = 0;
	int radius = 0;
	int padding = 0;
};

struct ImageRects {
	QRectF target;
	QRectF source;
};

struct TextRuns {
	std::vector<QGlyphRun> runs;
	QPointF origin;
};

struct ColorRuns {
	QPainterPath outline;
	std::vector<QGlyphRun> colored;
	QRectF bounds;
	QPointF origin;
};

struct FontGlyphs {
	QRawFont font;
	std::vector<quint32> indexes;
};

struct GlyphCell {
	int64 width = 0;
	int64 height = 0;
	int64 draws = 0;
};

struct GlyphCost {
	int64 work = 0;
	int64 bytes = 0;
};

struct GlyphProbe {
	QRawFont font;
	int64 largest = 0;
};

struct ColorLayer {
	QImage image;
	QRectF rect;
};

constexpr auto kColorTables = std::array{ "CPAL", "CBDT", "sbix" };

constexpr auto kUnsupportedModes = std::array{
	UnsupportedMode{ twlite::Composite::Hue, "hue" },
	UnsupportedMode{ twlite::Composite::Saturation, "saturation" },
	UnsupportedMode{ twlite::Composite::Color, "color" },
	UnsupportedMode{ twlite::Composite::Luminosity, "luminosity" },
};

constexpr auto kModes = std::array{
	QPainter::CompositionMode_SourceOver,
	QPainter::CompositionMode_SourceIn,
	QPainter::CompositionMode_SourceOut,
	QPainter::CompositionMode_SourceAtop,
	QPainter::CompositionMode_DestinationOver,
	QPainter::CompositionMode_DestinationIn,
	QPainter::CompositionMode_DestinationOut,
	QPainter::CompositionMode_DestinationAtop,
	QPainter::CompositionMode_Plus,
	QPainter::CompositionMode_Source,
	QPainter::CompositionMode_Xor,
	QPainter::CompositionMode_Multiply,
	QPainter::CompositionMode_Screen,
	QPainter::CompositionMode_Overlay,
	QPainter::CompositionMode_Darken,
	QPainter::CompositionMode_Lighten,
	QPainter::CompositionMode_ColorDodge,
	QPainter::CompositionMode_ColorBurn,
	QPainter::CompositionMode_HardLight,
	QPainter::CompositionMode_SoftLight,
	QPainter::CompositionMode_Difference,
	QPainter::CompositionMode_Exclusion,
};

static_assert(kModes.size() == std::size_t(twlite::Composite::Hue));

[[nodiscard]] int64 &TotalBytes() {
	static auto result = int64();
	return result;
}

[[nodiscard]] int64 Pixels(QSize size) {
	return int64(size.width()) * size.height();
}

[[nodiscard]] bool ValidSize(int width, int height) {
	return (width > 0)
		&& (height > 0)
		&& (width <= kMaxSide)
		&& (height <= kMaxSide)
		&& (int64(width) * height * kPixelBytes <= kMaxBackendBytes);
}

[[nodiscard]] QImage Transparent(QSize size) {
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	if (!result.isNull()) {
		result.fill(Qt::transparent);
	}
	return result;
}

[[nodiscard]] bool FiniteRect(const QRectF &rect) {
	return Finite(rect.topLeft()) && Finite(rect.bottomRight());
}

[[nodiscard]] QRect InkArea(const QRectF &ink, const QRect &bounds) {
	if (!FiniteRect(ink)) {
		return bounds;
	}
	const auto spread = ink.adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin);
	return QRectF(bounds).intersected(spread).toAlignedRect().intersected(
		bounds);
}

[[nodiscard]] QTransform Matrix(const twlite::CanvasOp &op) {
	const auto &m = op.matrix;
	return QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
}

[[nodiscard]] float64 Alpha(const twlite::CanvasOp &op) {
	return std::isfinite(op.alpha)
		? std::clamp(float64(op.alpha), 0., 1.)
		: 1.;
}

[[nodiscard]] bool ValidWidth(float width) {
	return std::isfinite(width) && (width > 0.f);
}

[[nodiscard]] float64 MiterLimit(float limit) {
	return (std::isfinite(limit) && (limit > 0.f))
		? float64(limit)
		: kMiterLimit;
}

[[nodiscard]] float64 DashOffset(const twlite::CanvasOp &op) {
	return std::isfinite(op.dash_offset) ? float64(op.dash_offset) : 0.;
}

[[nodiscard]] QPen StrokePen(const twlite::CanvasOp &op, QBrush brush) {
	auto result = QPen(
		std::move(brush),
		op.line_width,
		Qt::SolidLine,
		PathCap(op.line_cap),
		PathJoin(op.line_join));
	result.setMiterLimit(MiterLimit(op.miter_limit));
	return result;
}

[[nodiscard]] bool Unbounded(QPainter::CompositionMode mode) {
	constexpr auto modes = std::array{
		QPainter::CompositionMode_SourceIn,
		QPainter::CompositionMode_SourceOut,
		QPainter::CompositionMode_DestinationIn,
		QPainter::CompositionMode_DestinationAtop,
		QPainter::CompositionMode_Source,
	};
	return ranges::contains(modes, mode);
}

[[nodiscard]] std::optional<Shadow> ShadowOf(const twlite::CanvasOp &op) {
	const auto offset = QPointF(op.shadow_x, op.shadow_y);
	const auto blur = float64(op.shadow_blur);
	if (!op.has_shadow()
		|| !Finite(offset)
		|| !std::isfinite(blur)
		|| !(blur >= 0.)) {
		return std::nullopt;
	}
	const auto &color = op.shadow_color;
	const auto radius = StackRadius(std::min(blur, kMaxShadowBlur) / 2.);
	return Shadow{
		.offset = offset,
		.color = qRgba(color.r, color.g, color.b, color.a),
		.radius = radius,
		.padding = radius ? (radius + kMaskMargin) : 0,
	};
}

[[nodiscard]] int64 DistinctUnits(const QString &text) {
	auto seen = std::vector<bool>(std::size_t(1) << 16);
	auto result = int64();
	for (const auto unit : text) {
		const auto index = unit.unicode();
		if (!seen[index]) {
			seen[index] = true;
			++result;
		}
	}
	return result;
}

[[nodiscard]] QTransform TextPlacement(
		const twlite::CanvasOp &op,
		float64 scale) {
	return QTransform::fromScale(scale, scale)
		* QTransform::fromTranslate(
			op.text_dx + float64(op.x) / op.scale_x,
			op.y);
}

[[nodiscard]] bool ColorGlyphs(const QRawFont &font) {
	for (const auto table : kColorTables) {
		if (!font.fontTable(table).isEmpty()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] float64 MaxStretch(const QTransform &transform) {
	const auto a = transform.m11();
	const auto b = transform.m12();
	const auto c = transform.m21();
	const auto d = transform.m22();
	return (std::hypot(a + d, c - b) + std::hypot(a - d, b + c)) / 2.;
}

[[nodiscard]] TextRuns LayoutRuns(const QFont &font, QString text) {
	text.replace(QChar::LineSeparator, QChar::Space);
	auto layout = QTextLayout(text, font);
	auto option = layout.textOption();
	option.setUseDesignMetrics(true);
	layout.setTextOption(option);
	layout.beginLayout();
	const auto line = layout.createLine();
	layout.endLayout();
	auto result = TextRuns();
	if (!line.isValid()) {
		return result;
	}
	const auto runs = line.glyphRuns();
	result.runs = std::vector<QGlyphRun>(runs.begin(), runs.end());
	result.origin = QPointF(0., -line.ascent());
	return result;
}

[[nodiscard]] ColorRuns SplitColors(const TextRuns &text) {
	auto result = ColorRuns{ .origin = text.origin };
	for (const auto &run : text.runs) {
		const auto raw = run.rawFont();
		const auto colored = ColorGlyphs(raw);
		const auto indexes = run.glyphIndexes();
		const auto positions = run.positions();
		for (auto i = 0, count = int(indexes.size()); i != count; ++i) {
			const auto at = positions[i] + text.origin;
			if (colored) {
				result.bounds |= raw.boundingRect(indexes[i]).translated(at);
			} else {
				result.outline.addPath(
					raw.pathForGlyph(indexes[i]).translated(at));
			}
		}
		if (colored) {
			result.colored.push_back(run);
		}
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<GlyphCell>> GlyphCells(
		const QRawFont &font,
		std::vector<quint32> indexes,
		const QTransform &raster,
		int64 draws) {
	ranges::sort(indexes);
	auto result = std::vector<GlyphCell>();
	for (auto i = begin(indexes); i != end(indexes);) {
		const auto next = std::upper_bound(i, end(indexes), *i);
		const auto rect = raster.mapRect(font.boundingRect(*i).adjusted(
			-kGlyphSlack,
			-kGlyphSlack,
			kGlyphSlack,
			kGlyphSlack));
		if (!FiniteRect(rect)
			|| !(rect.width() <= kMaxSide)
			|| !(rect.height() <= kMaxSide)) {
			return std::nullopt;
		}
		result.push_back({
			.width = int64(std::ceil(rect.width())) + 2 * kGlyphPadding,
			.height = int64(std::ceil(rect.height())) + 2 * kGlyphPadding,
			.draws = int64(next - i) * draws,
		});
		i = next;
	}
	return result;
}

[[nodiscard]] std::vector<FontGlyphs> GroupGlyphs(
		const std::vector<QGlyphRun> &runs) {
	auto result = std::vector<FontGlyphs>();
	for (const auto &run : runs) {
		const auto font = run.rawFont();
		auto i = ranges::find(result, font, &FontGlyphs::font);
		if (i == end(result)) {
			i = result.insert(end(result), FontGlyphs{ .font = font });
		}
		const auto indexes = run.glyphIndexes();
		i->indexes.insert(end(i->indexes), indexes.begin(), indexes.end());
	}
	return result;
}

[[nodiscard]] std::optional<GlyphCost> CachedGlyphCost(
		const std::vector<QGlyphRun> &runs,
		const QTransform &raster,
		int64 draws,
		std::vector<GlyphProbe> &probes) {
	auto result = GlyphCost();
	for (auto &[font, indexes] : GroupGlyphs(runs)) {
		const auto cells = GlyphCells(font, std::move(indexes), raster, draws);
		const auto advance = font.maxCharWidth();
		if (!cells || !(advance <= kMaxSide)) {
			return std::nullopt;
		}
		auto widest = int64();
		auto tallest = int64();
		auto largest = int64();
		for (const auto &cell : *cells) {
			widest = std::max(widest, cell.width);
			tallest = std::max(tallest, cell.height);
			largest = std::max(largest, cell.width * cell.height);
		}
		auto length = 2 * std::max({
			kGlyphCacheWidth,
			int64(std::ceil(std::max(advance, 0.))),
			widest,
		});
		for (const auto &cell : *cells) {
			const auto variants = std::min(cell.draws, kSubpixelVariants);
			const auto area = cell.width * cell.height;
			result.work += cell.draws * area
				+ variants * (kGlyphWork + area * kGlyphPasses);
			length += variants * (cell.width + widest);
		}
		const auto atlas = 2 * length * tallest;
		auto probe = ranges::find(probes, font, &GlyphProbe::font);
		if (probe == end(probes)) {
			probe = probes.insert(end(probes), GlyphProbe{ .font = font });
			result.work += kSubpixelVariants * kGlyphWork;
		}
		if (largest > probe->largest) {
			result.work += kSubpixelVariants
				* (largest - probe->largest)
				* kGlyphPasses;
			probe->largest = largest;
		}
		result.work += atlas;
		result.bytes += (atlas + (kSubpixelVariants + 2) * largest)
			* kPixelBytes;
	}
	return result;
}

[[nodiscard]] std::optional<ImageRects> PlaceImage(
		const twlite::CanvasOp &op,
		QSizeF intrinsic) {
	const auto whole = QRectF(QPointF(), intrinsic);
	const auto source = (op.sw < 0.f)
		? whole
		: QRectF(op.sx, op.sy, op.sw, op.sh).normalized();
	const auto target = QRectF(op.dx, op.dy, op.dw, op.dh).normalized();
	const auto clipped = source.intersected(whole);
	if (!FiniteRect(source)
		|| !FiniteRect(target)
		|| source.isEmpty()
		|| target.isEmpty()
		|| clipped.isEmpty()) {
		return std::nullopt;
	}
	const auto scaleX = target.width() / source.width();
	const auto scaleY = target.height() / source.height();
	return ImageRects{
		.target = QRectF(
			target.x() + (clipped.x() - source.x()) * scaleX,
			target.y() + (clipped.y() - source.y()) * scaleY,
			clipped.width() * scaleX,
			clipped.height() * scaleY),
		.source = clipped,
	};
}

[[nodiscard]] QRectF PixelRect(
		const QRectF &source,
		QSizeF intrinsic,
		QSize pixels) {
	const auto scaleX = pixels.width() / intrinsic.width();
	const auto scaleY = pixels.height() / intrinsic.height();
	return QRectF(
		source.x() * scaleX,
		source.y() * scaleY,
		source.width() * scaleX,
		source.height() * scaleY);
}

[[nodiscard]] QBrush LinearPaint(const twlite::CanvasPaint &paint) {
	const auto &a = paint.args;
	if (a[0] == a[2] && a[1] == a[3]) {
		return QBrush();
	}
	return LinearBrush(QPointF(a[0], a[1]), QPointF(a[2], a[3]), paint.stops);
}

[[nodiscard]] QBrush RadialPaint(const twlite::CanvasPaint &paint) {
	const auto &a = paint.args;
	if (!(a[2] >= 0.f)
		|| !(a[5] >= 0.f)
		|| (a[0] == a[3] && a[1] == a[4] && a[2] == a[5])) {
		return QBrush();
	}
	auto gradient = QRadialGradient(
		QPointF(a[3], a[4]),
		a[5],
		QPointF(a[0], a[1]),
		a[2]);
	return GradientBrush(gradient, paint.stops, false);
}

[[nodiscard]] QBrush Unpremultiplied(QBrush brush) {
	const auto gradient = brush.gradient();
	if (!gradient) {
		return brush;
	}
	auto copy = *gradient;
	copy.setInterpolationMode(QGradient::ComponentInterpolation);
	auto result = QBrush(copy);
	result.setTransform(brush.transform());
	return result;
}

[[nodiscard]] QBrush PaintBrush(const twlite::CanvasPaint &paint) {
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	if (!ranges::all_of(paint.args, finite)) {
		return QBrush();
	}
	const auto &a = paint.args;
	const auto &color = paint.color;
	switch (paint.kind) {
	case twlite::CanvasPaint::Solid:
		return color.a
			? QBrush(QColor(color.r, color.g, color.b, color.a))
			: QBrush();
	case twlite::CanvasPaint::Linear:
		return Unpremultiplied(LinearPaint(paint));
	case twlite::CanvasPaint::Radial:
		return Unpremultiplied(RadialPaint(paint));
	case twlite::CanvasPaint::Conic:
		return Unpremultiplied(ConicBrush(
			QPointF(a[1], a[2]),
			kQuarterTurn + a[0] * kDegrees,
			paint.stops));
	}
	return QBrush();
}

[[nodiscard]] bool Fractional(float64 value) {
	return (value != std::floor(value));
}

[[nodiscard]] bool FractionalEdge(std::initializer_list<QPointF> points) {
	const auto first = *points.begin();
	auto horizontal = true;
	auto vertical = true;
	for (const auto point : points) {
		horizontal = horizontal && (point.y() == first.y());
		vertical = vertical && (point.x() == first.x());
	}
	return (horizontal != vertical)
		&& (horizontal ? Fractional(first.y()) : Fractional(first.x()));
}

[[nodiscard]] bool NeedsMask(const QPainterPath &path) {
	auto start = QPointF();
	auto last = QPointF();
	for (auto i = 0, count = path.elementCount(); i != count; ++i) {
		const auto &element = path.elementAt(i);
		const auto point = QPointF(element);
		switch (element.type) {
		case QPainterPath::MoveToElement:
			if (i && FractionalEdge({ last, start })) {
				return true;
			}
			start = last = point;
			break;
		case QPainterPath::LineToElement:
			if (FractionalEdge({ last, point })) {
				return true;
			}
			last = point;
			break;
		case QPainterPath::CurveToElement: {
			const auto control = QPointF(path.elementAt(i + 1));
			const auto end = QPointF(path.elementAt(i + 2));
			if (FractionalEdge({ last, point, control, end })) {
				return true;
			}
			last = end;
			i += 2;
		} break;
		case QPainterPath::CurveToDataElement: break;
		}
	}
	return FractionalEdge({ last, start });
}

void CoverageFromAlpha(QImage &image) {
	for (auto y = 0; y != image.height(); ++y) {
		const auto row = reinterpret_cast<QRgb*>(image.scanLine(y));
		std::transform(row, row + image.width(), row, [](QRgb pixel) {
			return QRgb(qAlpha(pixel));
		});
	}
}

} // namespace

class Canvases::Executor final {
public:
	Executor(
		Canvases &canvases,
		const QtBackend &backend,
		int id,
		QSize size);
	~Executor();

	void execute(const twlite::CanvasOp &op);

private:
	using Mode = QPainter::CompositionMode;
	using Paint = Fn<bool(QPainter &p)>;

	void reset();
	void fill(const twlite::CanvasOp &op);
	void stroke(const twlite::CanvasOp &op);
	void text(const twlite::CanvasOp &op);
	void image(const twlite::CanvasOp &op);
	void clear(const twlite::CanvasOp &op);
	void putImage(const twlite::CanvasOp &op);
	void fillGlyphs(
		const twlite::CanvasOp &op,
		Mode mode,
		const QtFont &font,
		const QTransform &device,
		const QString &text,
		const QBrush &brush,
		bool transparent);
	void fillOutlines(
		const twlite::CanvasOp &op,
		Mode mode,
		const QtFont &font,
		const QTransform &device,
		const QString &text,
		const QBrush &brush,
		bool transparent);
	[[nodiscard]] std::optional<int64> chargeGlyphs(
		const twlite::CanvasOp &op,
		const std::vector<QGlyphRun> &runs,
		const QTransform &raster);
	[[nodiscard]] std::optional<ColorLayer> colorLayer(
		QPainter &p,
		const ColorRuns &runs,
		const QTransform &device,
		const QBrush &brush,
		float64 scale);
	void strokeOutline(
		const twlite::CanvasOp &op,
		Mode mode,
		const QTransform &matrix,
		QPainterPath user,
		QPen pen,
		bool transparent);
	void draw(
		const twlite::CanvasOp &op,
		Mode mode,
		const QRectF &ink,
		bool transparent,
		const Paint &paint);
	void compose(
		const twlite::CanvasOp &op,
		Mode mode,
		float64 opacity,
		const QRectF &ink,
		const Paint &paint,
		const std::optional<Shadow> &shadow);
	[[nodiscard]] bool reserve(
		const QRect &content,
		const QRect &mask,
		const QRect &cast,
		bool covered,
		bool unbounded);
	[[nodiscard]] QImage paintLayer(
		const QRect &area,
		QPointF shift,
		float64 opacity,
		const Paint &paint);
	[[nodiscard]] QImage castShadow(
		const Shadow &shadow,
		const QRect &area,
		float64 opacity,
		const Paint &paint);
	void composite(
		QImage &image,
		const QRect &source,
		QPoint at,
		Mode mode,
		const QImage &mask);
	[[nodiscard]] Paint filling(QPainterPath path, QBrush brush);
	[[nodiscard]] std::optional<Mode> drawing(
		const twlite::CanvasOp &op,
		bool transparent);
	[[nodiscard]] std::optional<QPainterPath> shape(
		const twlite::Geometry &geometry,
		bool evenodd);
	[[nodiscard]] bool prepareClip(const twlite::CanvasOp &op);
	[[nodiscard]] bool installClip();
	[[nodiscard]] QImage coverage(const QRect &area);
	[[nodiscard]] QImage *target();
	[[nodiscard]] bool prepare(Mode mode, float64 opacity, bool clipped);
	[[nodiscard]] bool startPainting();
	void stopPainting();

	Canvases &_canvases;
	const QtBackend &_backend;
	const int _id = 0;
	const QSize _size;
	QImage *_bitmap = nullptr;
	QPainter _painter;
	std::shared_ptr<const std::vector<twlite::CanvasClip>> _clips;
	std::vector<QPainterPath> _clipPaths;
	std::vector<GlyphProbe> _probes;
	QRect _clipBounds;
	int64 _heldBytes = 0;
	int64 _layerBytes = 0;
	bool _clipPrepared = false;
	bool _clipMasked = false;
	bool _clipInstalled = false;

};

Canvases::Executor::Executor(
		Canvases &canvases,
		const QtBackend &backend,
		int id,
		QSize size)
: _canvases(canvases)
, _backend(backend)
, _id(id)
, _size(size) {
	const auto i = _canvases._bitmaps.find(id);
	if (i == _canvases._bitmaps.end()) {
		return;
	} else if (i->second.size() == size) {
		_bitmap = &i->second;
	} else {
		_canvases.discard(id);
	}
}

Canvases::Executor::~Executor() {
	stopPainting();
}

void Canvases::Executor::execute(const twlite::CanvasOp &op) {
	using Kind = twlite::CanvasOp::Kind;
	if (op.kind == Kind::Reset) {
		reset();
		return;
	} else if (_canvases._exhausted
		|| !_canvases.chargePixels(kOperationWork)) {
		return;
	}
	switch (op.kind) {
	case Kind::Fill: fill(op); break;
	case Kind::Stroke: stroke(op); break;
	case Kind::Clear: clear(op); break;
	case Kind::Text: text(op); break;
	case Kind::Image: image(op); break;
	case Kind::PutImage: putImage(op); break;
	case Kind::Reset: break;
	}
}

void Canvases::Executor::reset() {
	stopPainting();
	_bitmap = nullptr;
	_canvases.discard(_id);
}

void Canvases::Executor::fill(const twlite::CanvasOp &op) {
	const auto matrix = Matrix(op);
	auto brush = PaintBrush(op.paint);
	const auto transparent = (brush.style() == Qt::NoBrush);
	const auto mode = drawing(op, transparent);
	if (!mode) {
		return;
	} else if (!transparent && brush.style() != Qt::SolidPattern) {
		if (!Finite(matrix) || !matrix.isInvertible()) {
			return;
		}
		brush.setTransform(matrix);
	}
	if (const auto path = shape(op.path, op.evenodd)) {
		draw(
			op,
			*mode,
			path->controlPointRect(),
			transparent,
			filling(*path, brush));
	}
}

void Canvases::Executor::stroke(const twlite::CanvasOp &op) {
	const auto matrix = Matrix(op);
	const auto brush = PaintBrush(op.paint);
	const auto transparent = (brush.style() == Qt::NoBrush);
	const auto mode = drawing(op, transparent);
	if (!mode
		|| !ValidWidth(op.line_width)
		|| !Finite(matrix)
		|| !matrix.isInvertible()) {
		return;
	}
	if (const auto path = shape(op.path, false)) {
		strokeOutline(
			op,
			*mode,
			matrix,
			matrix.inverted().map(*path),
			StrokePen(op, brush),
			transparent);
	}
}

void Canvases::Executor::text(const twlite::CanvasOp &op) {
	const auto font = _backend.font(op.font);
	const auto matrix = Matrix(op);
	const auto brush = PaintBrush(op.paint);
	const auto transparent = (brush.style() == Qt::NoBrush);
	const auto mode = drawing(op, transparent);
	if (!mode
		|| !font
		|| !(font->scale > 0.)
		|| op.text.empty()
		|| !(op.scale_x > 0.f)
		|| (op.stroke_text && !ValidWidth(op.line_width))
		|| !Finite(matrix)
		|| !matrix.isInvertible()) {
		return;
	}
	const auto place = TextPlacement(op, font->scale);
	const auto space = QTransform::fromScale(op.scale_x, 1.) * matrix;
	const auto device = place * space;
	const auto stretch = MaxStretch(device);
	const auto size = font->font.pixelSize() * stretch;
	if (!Finite(place) || !Finite(device) || !std::isfinite(size)) {
		return;
	}
	const auto text = QString::fromUtf8(op.text.data(), int(op.text.size()));
	const auto uniform = (std::abs(device.determinant())
		>= stretch * stretch * kUniformStretch);
	const auto cached = !op.stroke_text
		&& uniform
		&& (size <= kMaxGlyphSize);
	const auto unit = cached ? kGlyphWork : kOutlineWork;
	const auto area = cached ? 0. : std::min(size, kMaxGlyphSize);
	const auto first = kFirstGlyphWork
		+ int64(std::ceil(area * area * kFirstGlyphArea));
	if (!_canvases.chargePixels(int64(text.size()) * unit
			+ DistinctUnits(text) * first)) {
		return;
	} else if (!op.stroke_text) {
		auto paint = brush;
		if (paint.gradient()) {
			paint.setTransform(space);
		}
		if (cached) {
			fillGlyphs(op, *mode, *font, device, text, paint, transparent);
		} else {
			fillOutlines(op, *mode, *font, device, text, paint, transparent);
		}
		return;
	}
	auto glyphs = QPainterPath();
	glyphs.addText(QPointF(), font->font, text);
	if (!_canvases._allowance.chargeElements(int64(glyphs.elementCount()))) {
		_canvases.exhaust();
		return;
	}
	strokeOutline(
		op,
		*mode,
		space,
		place.map(glyphs),
		StrokePen(op, brush),
		transparent);
}

void Canvases::Executor::fillGlyphs(
		const twlite::CanvasOp &op,
		Mode mode,
		const QtFont &font,
		const QTransform &device,
		const QString &text,
		const QBrush &brush,
		bool transparent) {
	const auto held = chargeGlyphs(
		op,
		LayoutRuns(font.font, text).runs,
		device);
	if (!held) {
		return;
	}
	const auto box = font.metrics.boundingRect(text).adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin);
	const auto ink = device.mapRect(box);
	const auto canvas = QRect(QPoint(), _size);
	if (!_canvases.chargePixels(Pixels(InkArea(ink, canvas).size()))) {
		return;
	}
	auto placed = brush;
	placed.setTransform(brush.transform() * device.inverted());
	_heldBytes = *held;
	draw(op, mode, ink, transparent, [&](QPainter &p) {
		p.setWorldTransform(device, true);
		p.setFont(font.font);
		p.setPen(QPen(placed, 0.));
		p.drawText(QPointF(), text);
		return true;
	});
	_heldBytes = 0;
}

void Canvases::Executor::fillOutlines(
		const twlite::CanvasOp &op,
		Mode mode,
		const QtFont &font,
		const QTransform &device,
		const QString &text,
		const QBrush &brush,
		bool transparent) {
	const auto runs = SplitColors(LayoutRuns(font.font, text));
	if (!_canvases._allowance.chargeElements(
			int64(runs.outline.elementCount()))) {
		_canvases.exhaust();
		return;
	}
	const auto scale = kMaxGlyphSize / font.font.pixelSize();
	const auto held = chargeGlyphs(
		op,
		runs.colored,
		QTransform::fromScale(scale, scale));
	if (!held) {
		return;
	}
	const auto path = device.map(runs.outline);
	const auto ink = path.controlPointRect().united(
		device.mapRect(runs.bounds));
	_heldBytes = *held;
	draw(op, mode, ink, transparent, [&](QPainter &p) {
		const auto colored = runs.colored.empty()
			? std::make_optional(ColorLayer())
			: colorLayer(p, runs, device, brush, scale);
		if (!colored
			|| (!path.isEmpty()
				&& !_canvases._allowance.chargeFill(p, path, brush))) {
			return false;
		} else if (!path.isEmpty()) {
			p.fillPath(path, brush);
		}
		if (!colored->image.isNull()) {
			p.setWorldTransform(device, true);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(colored->rect, colored->image);
		}
		return true;
	});
	_heldBytes = 0;
}

std::optional<int64> Canvases::Executor::chargeGlyphs(
		const twlite::CanvasOp &op,
		const std::vector<QGlyphRun> &runs,
		const QTransform &raster) {
	const auto draws = ShadowOf(op) ? 2 : 1;
	const auto cost = CachedGlyphCost(runs, raster, draws, _probes);
	if (!cost || cost->bytes > kMaxLayerBytes) {
		_canvases.exhaust();
		return std::nullopt;
	} else if (!_canvases.chargePixels(cost->work)) {
		return std::nullopt;
	}
	return cost->bytes;
}

auto Canvases::Executor::colorLayer(
		QPainter &p,
		const ColorRuns &runs,
		const QTransform &device,
		const QBrush &brush,
		float64 scale)
-> std::optional<ColorLayer> {
	const auto target = p.device();
	auto visible = p.worldTransform().inverted().mapRect(
		QRectF(0., 0., target->width(), target->height()));
	if (p.hasClipping()) {
		visible = visible.intersected(p.clipBoundingRect());
	}
	const auto raster = QTransform::fromScale(scale, scale);
	const auto area = InkArea(
		raster.mapRect(device.inverted().mapRect(visible)),
		raster.mapRect(runs.bounds).toAlignedRect());
	if (area.isEmpty()) {
		return ColorLayer();
	}
	const auto rect = QRectF(
		QPointF(area.topLeft()) / scale,
		QSizeF(area.size()) / scale);
	const auto shown = InkArea(device.mapRect(rect), visible.toAlignedRect());
	const auto bytes = Pixels(area.size()) * kPixelBytes;
	if (bytes > kMaxLayerBytes - _heldBytes - _layerBytes
		|| !_canvases.chargePixels(Pixels(area.size()) * kLayerPasses
			+ Pixels(shown.size()) * kImagePasses)) {
		return std::nullopt;
	}
	auto result = ColorLayer{
		.image = Transparent(area.size()),
		.rect = rect,
	};
	if (result.image.isNull()) {
		return std::nullopt;
	}
	auto q = QPainter(&result.image);
	PrepareHints(q);
	q.setWorldTransform(raster
		* QTransform::fromTranslate(-area.x(), -area.y()));
	q.setPen(QPen(brush, 0.));
	for (const auto &run : runs.colored) {
		q.drawGlyphRun(runs.origin, run);
	}
	q.end();
	return result;
}

void Canvases::Executor::image(const twlite::CanvasOp &op) {
	const auto matrix = Matrix(op);
	if (!Finite(matrix) || !matrix.isInvertible()) {
		return;
	}
	const auto self = (op.canvas == _id);
	const auto find = [&]() -> const QImage* {
		return !op.canvas
			? _backend.image(op.image)
			: self
			? _bitmap
			: _canvases.bitmap(op.canvas);
	};
	const auto found = find();
	if (!op.canvas && (!found || found->isNull())) {
		return;
	}
	const auto transparent = !found;
	const auto mode = drawing(op, transparent);
	const auto intrinsic = !op.canvas
		? _backend.imageSize(op.image)
		: QSizeF(self ? _size : _canvases.size(op.canvas));
	const auto rects = intrinsic.isEmpty()
		? std::nullopt
		: PlaceImage(op, intrinsic);
	if (!mode || !rects) {
		return;
	} else if (transparent) {
		draw(op, *mode, QRectF(), true, Paint());
		return;
	}
	const auto ink = matrix.mapRect(rects->target);
	const auto canvas = QRect(QPoint(), _size);
	if (!_canvases.chargePixels(
			Pixels(InkArea(ink, canvas).size()) * kImagePasses)
		|| !target()) {
		return;
	}
	auto source = find();
	auto pixels = PixelRect(rects->source, intrinsic, source->size());
	auto snapshot = QImage();
	if (self) {
		const auto aligned = pixels.toAlignedRect();
		const auto region = aligned.adjusted(-1, -1, 1, 1).intersected(
			source->rect());
		const auto bytes = Pixels(region.size()) * kPixelBytes;
		if (bytes > kMaxLayerBytes
			|| !_canvases.chargePixels(
				Pixels(region.size()) * kSnapshotPasses)) {
			_canvases.exhaust();
			return;
		}
		snapshot = source->copy(region);
		if (snapshot.isNull()) {
			_canvases.exhaust();
			return;
		}
		pixels.translate(-QPointF(region.topLeft()));
		source = &snapshot;
		_heldBytes = bytes;
	}
	draw(op, *mode, ink, false, [&](QPainter &p) {
		p.setWorldTransform(matrix, true);
		p.setRenderHint(QPainter::SmoothPixmapTransform, op.smoothing);
		p.drawImage(rects->target, *source, pixels);
		return true;
	});
	_heldBytes = 0;
}

void Canvases::Executor::clear(const twlite::CanvasOp &op) {
	if (!_bitmap) {
		return;
	} else if (const auto path = shape(op.path, false)) {
		compose(
			op,
			QPainter::CompositionMode_DestinationOut,
			1.,
			path->controlPointRect(),
			filling(*path, QBrush(Qt::black)),
			std::nullopt);
	}
}

void Canvases::Executor::putImage(const twlite::CanvasOp &op) {
	const auto &rgba = op.rgba;
	const auto width = op.w;
	const auto height = op.h;
	if (!rgba
		|| width <= 0
		|| height <= 0
		|| width > kMaxSide
		|| height > kMaxSide
		|| int64(rgba->size()) < int64(width) * height * kPixelBytes
		|| !std::isfinite(op.dx)
		|| !std::isfinite(op.dy)) {
		return;
	}
	const auto x = std::trunc(float64(op.dx));
	const auto y = std::trunc(float64(op.dy));
	if (!(x < _size.width())
		|| !(y < _size.height())
		|| !(x + width > 0.)
		|| !(y + height > 0.)) {
		return;
	}
	const auto originX = int64(x);
	const auto originY = int64(y);
	const auto left = int(std::max(originX, int64(0)));
	const auto top = int(std::max(originY, int64(0)));
	const auto right = int(std::min(originX + width, int64(_size.width())));
	const auto bottom = int(
		std::min(originY + height, int64(_size.height())));
	const auto area = int64(right - left) * (bottom - top);
	if (!_canvases.chargePixels(area) || !target()) {
		return;
	}
	stopPainting();
	const auto stride = int64(width) * kPixelBytes;
	for (auto row = top; row != bottom; ++row) {
		auto from = rgba->data()
			+ (row - originY) * stride
			+ (left - originX) * kPixelBytes;
		const auto to = reinterpret_cast<QRgb*>(_bitmap->scanLine(row));
		for (auto column = left; column != right; ++column) {
			to[column] = qPremultiply(
				qRgba(from[0], from[1], from[2], from[3]));
			from += kPixelBytes;
		}
	}
}

void Canvases::Executor::strokeOutline(
		const twlite::CanvasOp &op,
		Mode mode,
		const QTransform &matrix,
		QPainterPath user,
		QPen pen,
		bool transparent) {
	const auto bounds = user.controlPointRect();
	if (!FiniteRect(bounds)) {
		return;
	}
	const auto reach = StrokeReach(pen.widthF(), pen.miterLimit());
	const auto dashes = DashPattern(op.dashes);
	const auto offset = DashOffset(op);
	const auto ink = matrix.mapRect(
		bounds.adjusted(-reach, -reach, reach, reach));
	draw(op, mode, ink, transparent, [&](QPainter &p) {
		auto &allowance = _canvases._allowance;
		p.setWorldTransform(matrix, true);
		if (!dashes.empty()) {
			return allowance.strokeDashes(p, user, pen, dashes, offset);
		} else if (!allowance.chargeStroke(p, user, pen)) {
			return false;
		}
		p.strokePath(user, pen);
		return true;
	});
}

void Canvases::Executor::draw(
		const twlite::CanvasOp &op,
		Mode mode,
		const QRectF &ink,
		bool transparent,
		const Paint &paint) {
	const auto opacity = Alpha(op);
	const auto empty = transparent || !(opacity > 0.);
	compose(
		op,
		mode,
		opacity,
		ink,
		empty ? Paint() : paint,
		empty ? std::nullopt : ShadowOf(op));
}

void Canvases::Executor::compose(
		const twlite::CanvasOp &op,
		Mode mode,
		float64 opacity,
		const QRectF &ink,
		const Paint &paint,
		const std::optional<Shadow> &shadow) {
	_layerBytes = 0;
	if (!prepareClip(op) || !target()) {
		return;
	}
	const auto unbounded = Unbounded(mode);
	const auto plus = (mode == QPainter::CompositionMode_Plus);
	if (!shadow && !unbounded && !plus && !_clipMasked) {
		if (prepare(mode, opacity, true) && !paint(_painter)) {
			_canvases.exhaust();
		}
		return;
	}
	const auto content = unbounded ? _clipBounds : InkArea(ink, _clipBounds);
	const auto padding = shadow ? shadow->padding : 0;
	const auto cast = !shadow
		? QRect()
		: unbounded
		? _clipBounds
		: InkArea(
			ink.translated(shadow->offset).adjusted(
				-padding,
				-padding,
				padding,
				padding),
			_clipBounds);
	const auto mask = cast.isEmpty()
		? QRect()
		: cast.adjusted(-padding, -padding, padding, padding);
	const auto covered = _clipMasked || (plus && !_clipPaths.empty());
	if (!reserve(content, mask, cast, covered, unbounded)) {
		return;
	}
	auto shadowImage = mask.isEmpty()
		? QImage()
		: castShadow(*shadow, mask, opacity, paint);
	auto layer = content.isEmpty()
		? QImage()
		: paintLayer(content, QPointF(), opacity, paint);
	if ((!mask.isEmpty() && shadowImage.isNull())
		|| (!content.isEmpty() && layer.isNull())) {
		return;
	}
	const auto castCoverage = (covered && !shadowImage.isNull())
		? coverage(cast)
		: QImage();
	const auto contentCoverage = (covered && !layer.isNull())
		? coverage(content)
		: QImage();
	if ((covered && !shadowImage.isNull() && castCoverage.isNull())
		|| (covered && !layer.isNull() && contentCoverage.isNull())
		|| !prepare(mode, 1., !covered)) {
		return;
	}
	if (!shadowImage.isNull()) {
		composite(
			shadowImage,
			cast.translated(-mask.topLeft()),
			cast.topLeft(),
			mode,
			castCoverage);
	}
	if (!layer.isNull()) {
		composite(
			layer,
			layer.rect(),
			content.topLeft(),
			mode,
			contentCoverage);
	}
}

bool Canvases::Executor::reserve(
		const QRect &content,
		const QRect &mask,
		const QRect &cast,
		bool covered,
		bool unbounded) {
	const auto paths = int64(_clipPaths.size());
	const auto contentPixels = Pixels(content.size());
	const auto maskPixels = Pixels(mask.size());
	const auto castPixels = Pixels(cast.size());
	auto bytes = (contentPixels + 2 * maskPixels) * kPixelBytes;
	auto work = contentPixels * kLayerPasses
		+ maskPixels * kShadowPasses
		+ 2 * kLayerWork;
	if (covered) {
		const auto largest = std::max(contentPixels, castPixels);
		const auto scratch = ((paths > 1) ? 1 : 0) + (unbounded ? 1 : 0);
		bytes += (contentPixels + castPixels + largest * scratch)
			* kPixelBytes;
		work += (contentPixels + castPixels)
			* (kLayerPasses + kMaskPasses * paths)
			+ 2 * kLayerWork * (paths + 1);
	}
	if (bytes > kMaxLayerBytes - _heldBytes) {
		_canvases.exhaust();
		return false;
	}
	_layerBytes = bytes;
	return _canvases.chargePixels(work);
}

QImage Canvases::Executor::paintLayer(
		const QRect &area,
		QPointF shift,
		float64 opacity,
		const Paint &paint) {
	auto result = Transparent(area.size());
	if (result.isNull()) {
		_canvases.exhaust();
		return QImage();
	} else if (!paint) {
		return result;
	}
	auto p = QPainter(&result);
	PrepareHints(p);
	p.translate(shift - QPointF(area.topLeft()));
	p.setOpacity(opacity);
	const auto painted = paint(p);
	p.end();
	if (!painted) {
		_canvases.exhaust();
		return QImage();
	}
	return result;
}

QImage Canvases::Executor::castShadow(
		const Shadow &shadow,
		const QRect &area,
		float64 opacity,
		const Paint &paint) {
	auto image = paintLayer(area, shadow.offset, opacity, paint);
	if (image.isNull()) {
		return QImage();
	}
	CoverageFromAlpha(image);
	auto result = ShadowFromCoverage(
		std::move(image),
		shadow.radius,
		shadow.color);
	if (result.isNull()) {
		_canvases.exhaust();
	}
	return result;
}

void Canvases::Executor::composite(
		QImage &image,
		const QRect &source,
		QPoint at,
		Mode mode,
		const QImage &mask) {
	if (mask.isNull() || !Unbounded(mode)) {
		if (!mask.isNull()) {
			auto p = QPainter(&image);
			p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			p.drawImage(source.topLeft(), mask);
		}
		_painter.setCompositionMode(mode);
		_painter.drawImage(at, image, source);
		return;
	}
	auto result = _bitmap->copy(QRect(at, source.size()));
	if (result.isNull()) {
		_canvases.exhaust();
		return;
	}
	auto p = QPainter(&result);
	p.setCompositionMode(mode);
	p.drawImage(QPoint(), image, source);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(QPoint(), mask);
	p.end();
	_painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	_painter.drawImage(at, mask);
	_painter.setCompositionMode(QPainter::CompositionMode_Plus);
	_painter.drawImage(at, result);
}

auto Canvases::Executor::filling(QPainterPath path, QBrush brush)
-> Paint {
	return [=](QPainter &p) {
		if (!_canvases._allowance.chargeFill(p, path, brush)) {
			return false;
		}
		p.fillPath(path, brush);
		return true;
	};
}

auto Canvases::Executor::drawing(
		const twlite::CanvasOp &op,
		bool transparent)
-> std::optional<Mode> {
	auto mode = QPainter::CompositionMode_SourceOver;
	const auto index = std::size_t(op.composite);
	if (index < kModes.size()) {
		mode = kModes[index];
	} else {
		const auto i = ranges::find(
			kUnsupportedModes,
			op.composite,
			&UnsupportedMode::composite);
		if (i != kUnsupportedModes.end()) {
			_canvases.noteMode(int(i - kUnsupportedModes.begin()));
		}
	}
	return (Unbounded(mode) || (!transparent && Alpha(op) > 0.))
		? std::make_optional(mode)
		: std::nullopt;
}

std::optional<QPainterPath> Canvases::Executor::shape(
		const twlite::Geometry &geometry,
		bool evenodd) {
	if (!_canvases._allowance.chargeElements(int64(geometry.ops.size()))) {
		_canvases.exhaust();
		return std::nullopt;
	}
	auto result = VectorPath(geometry.ops, geometry.points, evenodd);
	return result.isEmpty()
		? std::nullopt
		: std::make_optional(std::move(result));
}

bool Canvases::Executor::prepareClip(const twlite::CanvasOp &op) {
	if (_clipPrepared && (op.clips == _clips)) {
		return !_clipBounds.isEmpty();
	}
	_clips = op.clips;
	_clipPaths.clear();
	_clipPrepared = _clipMasked = _clipInstalled = false;
	auto bounds = QRectF(QPointF(), QSizeF(_size));
	if (_clips) {
		auto &allowance = _canvases._allowance;
		for (const auto &clip : *_clips) {
			const auto &geometry = clip.path;
			if (!allowance.chargeElements(int64(geometry.ops.size()))) {
				_canvases.exhaust();
				return false;
			}
			auto path = VectorPath(
				geometry.ops,
				geometry.points,
				clip.evenodd);
			bounds = bounds.intersected(path.controlPointRect());
			_clipMasked = _clipMasked || NeedsMask(path);
			_clipPaths.push_back(std::move(path));
		}
	}
	_clipBounds = bounds.toAlignedRect();
	_clipPrepared = true;
	return !_clipBounds.isEmpty();
}

bool Canvases::Executor::installClip() {
	if (_clipInstalled) {
		return true;
	}
	_painter.setWorldTransform(QTransform());
	if (_clipMasked || _clipPaths.empty()) {
		_painter.setClipping(false);
	} else {
		auto operation = Qt::ReplaceClip;
		for (const auto &path : _clipPaths) {
			if (!_canvases._allowance.chargeFill(_painter, path, Qt::white)) {
				_canvases.exhaust();
				return false;
			}
			_painter.setClipPath(path, operation);
			operation = Qt::IntersectClip;
		}
	}
	_clipInstalled = true;
	return true;
}

QImage Canvases::Executor::coverage(const QRect &area) {
	auto result = Transparent(area.size());
	auto scratch = (_clipPaths.size() > 1)
		? Transparent(area.size())
		: QImage();
	for (auto i = std::size_t(); i != _clipPaths.size(); ++i) {
		auto &image = i ? scratch : result;
		if (image.isNull()) {
			_canvases.exhaust();
			return QImage();
		} else if (i > 1) {
			image.fill(Qt::transparent);
		}
		const auto &path = _clipPaths[i];
		auto p = QPainter(&image);
		PrepareHints(p);
		p.translate(-area.topLeft());
		if (!_canvases._allowance.chargeFill(p, path, Qt::white)) {
			_canvases.exhaust();
			return QImage();
		}
		p.fillPath(path, Qt::white);
		p.end();
		if (i) {
			auto merge = QPainter(&result);
			merge.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			merge.drawImage(0, 0, scratch);
		}
	}
	return result;
}

QImage *Canvases::Executor::target() {
	if (!_bitmap) {
		_bitmap = _canvases.allocate(_id, _size);
	}
	return _bitmap;
}

bool Canvases::Executor::prepare(Mode mode, float64 opacity, bool clipped) {
	if (!startPainting()) {
		return false;
	} else if (!clipped) {
		_painter.setClipping(false);
		_clipInstalled = false;
	} else if (!installClip()) {
		return false;
	}
	PrepareHints(_painter);
	_painter.setWorldTransform(QTransform());
	_painter.setOpacity(opacity);
	_painter.setCompositionMode(mode);
	return true;
}

bool Canvases::Executor::startPainting() {
	if (_painter.isActive()) {
		return true;
	} else if (!_bitmap
		|| !_canvases.chargePixels(kPainterWork)
		|| !_painter.begin(_bitmap)) {
		return false;
	}
	PrepareHints(_painter);
	_clipInstalled = false;
	return true;
}

void Canvases::Executor::stopPainting() {
	if (_painter.isActive()) {
		_painter.end();
	}
	_clipInstalled = false;
}

Canvases::Canvases(QtBackend::Diagnostic diagnostic)
: _diagnostic(std::move(diagnostic))
, _pixels(kMaxPixelWork) {
}

Canvases::~Canvases() {
	TotalBytes() -= _bytes;
}

void Canvases::run(
		int id,
		int width,
		int height,
		const std::vector<twlite::CanvasOp> &ops,
		const QtBackend &backend) {
	const auto reset = [](const twlite::CanvasOp &op) {
		return (op.kind == twlite::CanvasOp::Reset);
	};
	if (!ValidSize(width, height)) {
		release(id);
		return;
	}
	_sizes[id] = QSize(width, height);
	if (_exhausted) {
		const auto i = _bitmaps.find(id);
		if (i != end(_bitmaps)
			&& (i->second.size() != QSize(width, height)
				|| ranges::any_of(ops, reset))) {
			discard(id);
		}
		return;
	}
	auto executor = Executor(*this, backend, id, QSize(width, height));
	for (const auto &op : ops) {
		executor.execute(op);
	}
}

bool Canvases::read(
		int id,
		int x,
		int y,
		int width,
		int height,
		std::vector<uint8_t> &rgba) {
	const auto pixels = int64(width) * height;
	if (width <= 0
		|| height <= 0
		|| pixels * kPixelBytes > kMaxReadBytes
		|| !chargePixels(pixels)) {
		return false;
	}
	rgba.assign(std::size_t(pixels * kPixelBytes), uint8_t(0));
	const auto i = _bitmaps.find(id);
	if (i == end(_bitmaps)) {
		return true;
	}
	const auto &bitmap = i->second;
	const auto left = std::max(int64(x), int64(0));
	const auto top = std::max(int64(y), int64(0));
	const auto right = std::min(int64(x) + width, int64(bitmap.width()));
	const auto bottom = std::min(int64(y) + height, int64(bitmap.height()));
	for (auto row = top; row < bottom; ++row) {
		const auto from = reinterpret_cast<const QRgb*>(
			bitmap.constScanLine(int(row)));
		auto to = rgba.data()
			+ ((row - y) * width + (left - x)) * kPixelBytes;
		for (auto column = left; column < right; ++column) {
			const auto pixel = qUnpremultiply(from[column]);
			to[0] = uint8_t(qRed(pixel));
			to[1] = uint8_t(qGreen(pixel));
			to[2] = uint8_t(qBlue(pixel));
			to[3] = uint8_t(qAlpha(pixel));
			to += kPixelBytes;
		}
	}
	return true;
}

void Canvases::release(int id) {
	discard(id);
	_sizes.remove(id);
}

void Canvases::refill() {
	_allowance = PaintAllowance();
	_pixels = kMaxPixelWork;
	_notedLimits = 0;
	_exhausted = false;
}

const QImage *Canvases::bitmap(int id) const {
	const auto i = _bitmaps.find(id);
	return (i != end(_bitmaps)) ? &i->second : nullptr;
}

QImage *Canvases::allocate(int id, QSize size) {
	discard(id);
	const auto bytes = Pixels(size) * kPixelBytes;
	auto &total = TotalBytes();
	if (bytes > kMaxBackendBytes - _bytes || bytes > kMaxTotalBytes - total) {
		noteLimit(Limit::Memory);
		return nullptr;
	} else if (!chargePixels(Pixels(size))) {
		return nullptr;
	}
	auto image = Transparent(size);
	if (image.isNull()) {
		noteLimit(Limit::Memory);
		return nullptr;
	}
	_bytes += bytes;
	total += bytes;
	return &_bitmaps.emplace(id, std::move(image)).first->second;
}

void Canvases::discard(int id) {
	if (const auto bitmap = _bitmaps.take(id)) {
		const auto bytes = Pixels(bitmap->size()) * kPixelBytes;
		_bytes -= bytes;
		TotalBytes() -= bytes;
	}
}

QSize Canvases::size(int id) const {
	const auto i = _sizes.find(id);
	return (i != end(_sizes)) ? i->second : QSize();
}

bool Canvases::chargePixels(int64 pixels) {
	if (pixels < 0 || pixels > _pixels) {
		exhaust();
		return false;
	}
	_pixels -= pixels;
	return true;
}

void Canvases::exhaust() {
	_exhausted = true;
	noteLimit(Limit::Work);
}

void Canvases::noteLimit(Limit limit) {
	const auto bit = uchar(1 << int(limit));
	if (_notedLimits & bit) {
		return;
	}
	_notedLimits |= bit;
	diagnose(
		kLimitKind,
		(limit == Limit::Work) ? kWorkDetail : kMemoryDetail);
}

void Canvases::noteMode(int index) {
	const auto bit = uchar(1 << index);
	if (_notedModes & bit) {
		return;
	}
	_notedModes |= bit;
	const auto name = std::string(kUnsupportedModes[index].name);
	diagnose(std::string(kModeKind) + name, name + std::string(kModeDetail));
}

void Canvases::diagnose(std::string_view kind, std::string_view detail) const {
	if (_diagnostic) {
		_diagnostic(std::string(kind), std::string(detail));
	}
}

} // namespace Tgw
