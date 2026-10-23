/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#if !defined(__APPLE__)
#error "test_paint_stream.h supports macOS only, like its replay component"
#endif // !__APPLE__

#if !__has_include(<QtGui/private/qpicture_p.h>)
#error "test_paint_stream.h needs Qt's private QPicture header (GuiPrivate)"
#endif // !__has_include(<QtGui/private/qpicture_p.h>)

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLibraryInfo>
#include <QtCore/QRegularExpression>
#include <QtGui/private/qpicture_p.h>
#include <QtGui/QColorSpace>
#include <QtGui/QFontInfo>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPaintEngine>
#include <QtGui/QPainter>
#include <QtGui/QPicture>
#include <QtGui/QPixmap>
#include <QtGui/QStyleHints>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <memory>
#include <optional>
#include <set>
#include <typeinfo>
#include <utility>

// Whole-owner paint streams: the recording side an overlay runs inside
// Debug Telegram, and the loading side of the optimized-Qt replayer. Debug
// Telegram links Debug Qt, so a paint timed there is not a paint cost: the
// overlay records the real owner into an in-memory QPicture inside its
// timed window, saves each stream with format-preserving sidecars after
// the window, and the standalone replayer under
// .agents/shared/evidence/paint_replay_component/ loads and replays the
// streams in optimized Qt. That replayer compiles this header too, so it
// uses Qt only (no lib_base, no _q literals). Recipe, row keys, refusals
// and limits: docs/helpers/test_paint_stream.md.
//
// The Qt private format. QPicturePrivate is private API that may change
// between Qt versions. In in_memory_only mode a stream stores only indices
// into the picture's image, pixmap, brush and pen lists, so a stream whose
// sidecar is missing replays as "successful" and wrong in optimized Qt
// (Debug Qt alone asserts on the index): completeness is decided by the
// recorded list counts and by pixel digests, never by QPicture::play().
// QPicture::load() and setData() detach through a private copy that drops
// in_memory_only, the format version and the four lists, so a recording,
// or a copy sharing it, is never load()-ed or setData()-ed; copies used for
// save() and play() share the data and are safe.

namespace Test {

inline constexpr auto kPaintStreamSchema = 1;
inline constexpr auto kPaintSidecarVersion = 1;

// The two fills every replay is drawn over: index 0 is the reference
// target, index 1 the second fill of the coverage oracle (a device pixel is
// covered when replays over both fills agree).
[[nodiscard]] inline QColor PaintSentinel(int index) {
	return index ? QColor(250, 10, 160) : QColor(70, 100, 120);
}

// The owner a campaign declares: |rootType| is the demangled type name the
// author writes down (never read from the recorded widget, so a wrong root
// is refused instead of adopted), |size| its logical size and |dpr| the
// device pixel ratio of every replay target.
struct PaintSurface {
	QString rootType;
	QSize size;
	double dpr = 1.;
};

// One recorded stream. |picture| is in in_memory_only mode and owns its
// lists. |recordNs| is the CPU time of the painter session around
// QWidget::render() only. |rootType|, |rootSize|, |region| and |flags|
// describe what was recorded; |directDifference| is -1 until the caller
// stores PaintDirectDifference() in it. |fields| are the caller's row keys
// (window, at, pressed, moving, position, pairedWith, fieldRect, ...).
struct PaintRecording {
	QString name;
	QString role;
	QPicture picture;
	qint64 recordNs = 0;
	QString rootType;
	QSize rootSize;
	QRect region;
	int flags = 0;
	qint64 directDifference = -1;
	QJsonObject fields;
};

// What LoadPaintStream() read. |error| is empty on success; |identity|
// carries {qpic, tables} {bytes, sha256} of every file actually read, also
// when |error| is set.
struct LoadedPaintStream {
	QPicture picture;
	QJsonObject identity;
	QString error;
};

} // namespace Test

namespace Test::details {

inline constexpr auto kPaintSidecarMagic = "PSTB";
inline constexpr auto kPaintInventoryExtent = 1 << 14;
inline constexpr auto kPaintInventoryDpi = 72;

struct FreeDeleter {
	void operator()(char *pointer) const;
};

struct PaintSidecar {
	QList<QImage> images;
	QList<QPixmap> pixmaps;
	QList<QBrush> brushes;
	QList<QPen> pens;
};

class PaintInventoryEngine final : public QPaintEngine {
public:
	PaintInventoryEngine();

	bool begin(QPaintDevice *device) override;
	bool end() override;
	void updateState(const QPaintEngineState &state) override;
	void drawPixmap(
		const QRectF &rect,
		const QPixmap &pixmap,
		const QRectF &source) override;
	void drawImage(
		const QRectF &rect,
		const QImage &image,
		const QRectF &source,
		Qt::ImageConversionFlags flags) override;
	void drawTextItem(const QPointF &point, const QTextItem &item) override;
	void drawPath(const QPainterPath &path) override;
	void drawPolygon(
		const QPointF *points,
		int count,
		PolygonDrawMode mode) override;
	Type type() const override;

	[[nodiscard]] QJsonArray fonts() const;

private:
	std::set<std::pair<QString, QString>> _fonts;

};

class PaintInventoryDevice final : public QPaintDevice {
public:
	QPaintEngine *paintEngine() const override;

	[[nodiscard]] QJsonArray fonts() const;

protected:
	int metric(PaintDeviceMetric id) const override;

private:
	mutable PaintInventoryEngine _engine;

};

inline void FreeDeleter::operator()(char *pointer) const {
	std::free(pointer);
}

inline void FreeImageBuffer(void *buffer) {
	std::free(buffer);
}

inline PaintInventoryEngine::PaintInventoryEngine()
: QPaintEngine(QPaintEngine::AllFeatures) {
}

inline bool PaintInventoryEngine::begin(QPaintDevice *) {
	return true;
}

inline bool PaintInventoryEngine::end() {
	return true;
}

inline void PaintInventoryEngine::updateState(const QPaintEngineState &) {
}

inline void PaintInventoryEngine::drawPixmap(
		const QRectF &,
		const QPixmap &,
		const QRectF &) {
}

inline void PaintInventoryEngine::drawImage(
		const QRectF &,
		const QImage &,
		const QRectF &,
		Qt::ImageConversionFlags) {
}

inline void PaintInventoryEngine::drawTextItem(
		const QPointF &,
		const QTextItem &item) {
	const auto font = item.font();
	_fonts.emplace(font.family(), QFontInfo(font).family());
}

inline void PaintInventoryEngine::drawPath(const QPainterPath &) {
}

inline void PaintInventoryEngine::drawPolygon(
		const QPointF *,
		int,
		PolygonDrawMode) {
}

inline QPaintEngine::Type PaintInventoryEngine::type() const {
	return QPaintEngine::User;
}

inline QJsonArray PaintInventoryEngine::fonts() const {
	auto result = QJsonArray();
	for (const auto &[family, resolved] : _fonts) {
		result.push_back(QJsonObject{
			{ QStringLiteral("family"), family },
			{ QStringLiteral("resolved"), resolved },
		});
	}
	return result;
}

inline QPaintEngine *PaintInventoryDevice::paintEngine() const {
	return &_engine;
}

inline QJsonArray PaintInventoryDevice::fonts() const {
	return _engine.fonts();
}

inline int PaintInventoryDevice::metric(PaintDeviceMetric id) const {
	switch (id) {
	case PdmWidth:
	case PdmHeight:
		return kPaintInventoryExtent;
	case PdmWidthMM:
	case PdmHeightMM:
		return kPaintInventoryExtent * 254 / (kPaintInventoryDpi * 10);
	case PdmDpiX:
	case PdmDpiY:
	case PdmPhysicalDpiX:
	case PdmPhysicalDpiY:
		return kPaintInventoryDpi;
	case PdmDevicePixelRatio:
		return 1;
	case PdmDevicePixelRatioScaled:
		return int(devicePixelRatioFScale());
	case PdmDepth:
		return 32;
	default:
		return 0;
	}
}

[[nodiscard]] inline QString BytesSha256(const QByteArray &bytes) {
	return QString::fromLatin1(
		QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

[[nodiscard]] inline QJsonObject BytesIdentity(const QByteArray &bytes) {
	return QJsonObject{
		{ QStringLiteral("bytes"), qint64(bytes.size()) },
		{ QStringLiteral("sha256"), BytesSha256(bytes) },
	};
}

[[nodiscard]] inline bool ValidStreamName(const QString &name) {
	static const auto pattern = QRegularExpression(
		QStringLiteral("^[A-Za-z0-9_.-]+$"));
	return pattern.match(name).hasMatch();
}

// Keys SavePaintRecording() writes itself; a caller field may not use them.
[[nodiscard]] inline const QStringList &ReservedRowKeys() {
	static const auto keys = QStringList{
		QStringLiteral("name"),
		QStringLiteral("role"),
		QStringLiteral("recordMs"),
		QStringLiteral("root"),
		QStringLiteral("qpic"),
		QStringLiteral("tables"),
		QStringLiteral("counts"),
		QStringLiteral("fonts"),
		QStringLiteral("referenceSha256"),
		QStringLiteral("directDifference"),
	};
	return keys;
}

[[nodiscard]] inline std::optional<QByteArray> ReadFileBytes(
		const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	return file.readAll();
}

[[nodiscard]] inline bool WriteFileBytes(
		const QString &path,
		const QByteArray &bytes) {
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| file.write(bytes) != bytes.size()
		|| !file.flush()) {
		return false;
	}
	file.close();
	return (file.error() == QFileDevice::NoError);
}

[[nodiscard]] inline qint64 RemainingBytes(const QDataStream &stream) {
	const auto device = stream.device();
	return device ? device->bytesAvailable() : 0;
}

// A count written by the sidecar writer, or -1 when it is negative or
// larger than the bytes left (so a corrupt count never drives a huge
// allocation).
[[nodiscard]] inline qint32 ReadCount(QDataStream &stream) {
	auto count = qint32(0);
	stream >> count;
	return (stream.status() == QDataStream::Ok
		&& count >= 0
		&& count <= RemainingBytes(stream))
		? count
		: -1;
}

// qint32 format, width, height and bytesPerLine, double device pixel ratio,
// the color table (qint32 count, then quint32 colors), the color space's
// ICC profile (empty for none), then |height| raw scanlines of bytesPerLine
// bytes, padding included: an image over an external buffer may have longer
// rows than Qt's default stride (FFmpeg frame storage aligns rows to 64
// bytes). A null image writes format 0 and zero sizes. Raw scanlines keep
// the exact format, ratio and stride: a PNG round trip reloads
// ARGB32_Premultiplied as ARGB32 with ratio 1, and its replay takes another
// raster path.
inline void WriteRawImage(QDataStream &stream, const QImage &image) {
	const auto table = image.colorTable();
	stream
		<< qint32(image.format())
		<< qint32(image.width())
		<< qint32(image.height())
		<< qint32(image.bytesPerLine())
		<< double(image.devicePixelRatio())
		<< qint32(table.size());
	for (const auto color : table) {
		stream << quint32(color);
	}
	stream << image.colorSpace().iccProfile();
	for (auto y = 0; y != image.height(); ++y) {
		stream.writeRawData(
			reinterpret_cast<const char*>(image.constScanLine(y)),
			qint64(image.bytesPerLine()));
	}
}

// The image WriteRawImage() wrote, over an owned buffer of exactly
// bytesPerLine * height bytes that keeps the recorded stride, so a padded
// image replays with the layout it was painted from. |*ok| is false for a
// truncated or malformed record, and for a layout QImage cannot hold (a
// bytesPerLine shorter than |width| packed pixels of the format).
[[nodiscard]] inline QImage ReadRawImage(QDataStream &stream, bool *ok) {
	*ok = false;
	auto format = qint32(0);
	auto width = qint32(0);
	auto height = qint32(0);
	auto bytesPerLine = qint32(0);
	auto ratio = 0.;
	stream >> format >> width >> height >> bytesPerLine >> ratio;
	const auto colors = ReadCount(stream);
	if (colors < 0) {
		return QImage();
	}
	auto table = QList<QRgb>();
	table.reserve(colors);
	for (auto i = 0; i != colors; ++i) {
		auto color = quint32(0);
		stream >> color;
		table.push_back(QRgb(color));
	}
	auto profile = QByteArray();
	stream >> profile;
	const auto space = profile.isEmpty()
		? QColorSpace()
		: QColorSpace::fromIccProfile(profile);
	if (stream.status() != QDataStream::Ok
		|| (!profile.isEmpty() && !space.isValid())) {
		return QImage();
	} else if (format == QImage::Format_Invalid
		&& !width
		&& !height
		&& !bytesPerLine) {
		*ok = true;
		return QImage();
	} else if (format <= QImage::Format_Invalid
		|| format >= QImage::NImageFormats
		|| width <= 0
		|| height <= 0
		|| bytesPerLine <= 0
		|| !(ratio > 0.)
		|| qint64(bytesPerLine) * height > RemainingBytes(stream)) {
		return QImage();
	}
	const auto bytes = qint64(bytesPerLine) * height;
	auto buffer = std::unique_ptr<char, FreeDeleter>(
		static_cast<char*>(std::malloc(std::size_t(bytes))));
	if (!buffer || stream.readRawData(buffer.get(), bytes) != bytes) {
		return QImage();
	}
	// A non-owning view first: QImage refuses a layout it cannot hold
	// without taking the buffer, so the buffer moves into the image only
	// once the same layout is known to be valid.
	const auto data = reinterpret_cast<uchar*>(buffer.get());
	const auto qtFormat = QImage::Format(format);
	if (QImage(data, width, height, bytesPerLine, qtFormat).isNull()) {
		return QImage();
	}
	auto image = QImage(
		data,
		width,
		height,
		bytesPerLine,
		qtFormat,
		FreeImageBuffer,
		buffer.release());
	if (!table.isEmpty()) {
		image.setColorTable(table);
	}
	image.setDevicePixelRatio(ratio);
	image.setColorSpace(space);
	*ok = true;
	return image;
}

// Magic "PSTB", quint32 kPaintSidecarVersion, qint32 QDataStream version,
// then the image, pixmap, brush and pen lists, each as a qint32 count and
// its items: images and pixmaps (as toImage()) raw, brushes and pens with
// their QDataStream operators. The stream version is always
// QDataStream::Qt_DefaultCompiledVersion.
[[nodiscard]] inline QByteArray WriteSidecar(
		const QPicturePrivate &data,
		bool *ok) {
	auto bytes = QByteArray();
	{
		auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_DefaultCompiledVersion);
		stream.writeRawData(kPaintSidecarMagic, qint64(4));
		stream
			<< quint32(kPaintSidecarVersion)
			<< qint32(stream.version())
			<< qint32(data.image_list.size());
		for (const auto &image : data.image_list) {
			WriteRawImage(stream, image);
		}
		stream << qint32(data.pixmap_list.size());
		for (const auto &pixmap : data.pixmap_list) {
			WriteRawImage(stream, pixmap.toImage());
		}
		stream << qint32(data.brush_list.size());
		for (const auto &brush : data.brush_list) {
			stream << brush;
		}
		stream << qint32(data.pen_list.size());
		for (const auto &pen : data.pen_list) {
			stream << pen;
		}
		*ok = (stream.status() == QDataStream::Ok);
	}
	return bytes;
}

// Parses WriteSidecar() bytes into |sidecar|. Returns an empty string or
// "sidecar-unreadable" / "sidecar-version-mismatch". Pixmaps are rebuilt
// with Qt::NoFormatConversion, so they keep the recorded pixel format.
[[nodiscard]] inline QString ReadSidecar(
		const QByteArray &bytes,
		PaintSidecar *sidecar) {
	const auto unreadable = QStringLiteral("sidecar-unreadable");
	auto buffer = QBuffer();
	buffer.setData(bytes);
	if (!buffer.open(QIODevice::ReadOnly)) {
		return unreadable;
	}
	auto stream = QDataStream(&buffer);
	stream.setVersion(QDataStream::Qt_DefaultCompiledVersion);
	char magic[4] = {};
	if (stream.readRawData(magic, qint64(4)) != 4
		|| QByteArrayView(magic, 4) != QByteArrayView(kPaintSidecarMagic)) {
		return unreadable;
	}
	auto version = quint32(0);
	auto streamVersion = qint32(0);
	stream >> version >> streamVersion;
	if (stream.status() != QDataStream::Ok) {
		return unreadable;
	} else if (version != quint32(kPaintSidecarVersion)
		|| streamVersion != QDataStream::Qt_DefaultCompiledVersion) {
		return QStringLiteral("sidecar-version-mismatch");
	}
	auto ok = true;
	const auto images = ReadCount(stream);
	for (auto i = 0; ok && i < images; ++i) {
		sidecar->images.push_back(ReadRawImage(stream, &ok));
	}
	const auto pixmaps = (ok && images >= 0) ? ReadCount(stream) : -1;
	for (auto i = 0; ok && i < pixmaps; ++i) {
		sidecar->pixmaps.push_back(QPixmap::fromImage(
			ReadRawImage(stream, &ok),
			Qt::NoFormatConversion));
	}
	const auto brushes = (ok && pixmaps >= 0) ? ReadCount(stream) : -1;
	for (auto i = 0; i < brushes; ++i) {
		auto brush = QBrush();
		stream >> brush;
		sidecar->brushes.push_back(brush);
	}
	const auto pens = (brushes >= 0) ? ReadCount(stream) : -1;
	for (auto i = 0; i < pens; ++i) {
		auto pen = QPen();
		stream >> pen;
		sidecar->pens.push_back(pen);
	}
	if (!ok
		|| pens < 0
		|| stream.status() != QDataStream::Ok
		|| !stream.atEnd()) {
		return unreadable;
	}
	return QString();
}

} // namespace Test::details

namespace Test {

// The demangled dynamic type of |widget|, as "Namespace::Class".
[[nodiscard]] inline QString PaintTypeName(const QWidget *widget) {
	if (!widget) {
		return QString();
	}
	const auto raw = typeid(*widget).name();
	auto status = 0;
	const auto demangled = std::unique_ptr<char, details::FreeDeleter>(
		abi::__cxa_demangle(raw, nullptr, nullptr, &status));
	return QString::fromLatin1((!status && demangled) ? demangled.get() : raw);
}

// Records |owner| into a new in-memory QPicture, as the owner paints right
// now: QWidget::render() with |flags| over |region| in owner coordinates
// (empty: the whole owner).
//
// The default flags, QWidget::RenderFlags(), record what the backing store
// paints for a child owner: its paintEvent(), plus its autoFillBackground()
// fill or styled background, without children. Pass DrawWindowBackground
// only for a top-level window owner: Qt then fills the palette's Window
// brush under an owner that sets neither WA_OpaquePaintEvent nor
// WA_NoSystemBackground, as the backing store does for a window. For a child
// that fill is a full-surface paint its on-screen paint never runs: it would
// enter the timed cost and make a translucent child cover the whole surface.
// Under the default a translucent child leaves device pixels uncovered, and
// the assessor refuses it as not-whole-owner. Pass DrawChildren for an
// owner that paints through children.
//
// The region is recorded as its bounding rectangle clipped to the owner, at
// its own position, so a replay onto a PaintTarget() keeps owner
// coordinates. A QPicture ignores the system clip render() sets, so a
// rectangle smaller than the owner is also set as the picture painter's
// clip: without it a region recording replays everything the owner painted.
// Only the painter session is timed: no file, encoding or digest work
// happens here, so it is cheap enough to call inside a timed window.
[[nodiscard]] inline PaintRecording RecordPaint(
		QWidget *owner,
		const QString &name,
		const QString &role,
		const QJsonObject &fields,
		const QRegion &region = QRegion(),
		QWidget::RenderFlags flags = QWidget::RenderFlags()) {
	auto result = PaintRecording();
	result.name = name;
	result.role = role;
	result.fields = fields;
	result.rootType = PaintTypeName(owner);
	result.rootSize = owner->size();
	result.region = (region.isEmpty() ? owner->rect() : region.boundingRect())
		& owner->rect();
	result.flags = flags.toInt();
	result.picture.data_ptr()->in_memory_only = true;
	const auto source = QRegion(result.region);
	const auto offset = result.region.topLeft();
	auto timer = QElapsedTimer();
	timer.start();
	{
		auto painter = QPainter(&result.picture);
		if (result.region != owner->rect()) {
			painter.setClipRect(result.region);
		}
		owner->render(&painter, offset, source, flags);
	}
	result.recordNs = timer.nsecsElapsed();
	return result;
}

// The "empty" control: an in-memory picture with an empty painter session,
// which must change no pixel when replayed.
[[nodiscard]] inline PaintRecording EmptyPaintRecording(const QString &name) {
	auto result = PaintRecording();
	result.name = name;
	result.role = QStringLiteral("empty");
	result.picture.data_ptr()->in_memory_only = true;
	auto painter = QPainter(&result.picture);
	painter.end();
	return result;
}

// An ARGB32_Premultiplied replay target of |surface| in device pixels, at
// its device pixel ratio, filled with |fill|.
[[nodiscard]] inline QImage PaintTarget(
		const PaintSurface &surface,
		const QColor &fill) {
	auto result = QImage(
		(QSizeF(surface.size) * surface.dpr).toSize(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(surface.dpr);
	result.fill(fill);
	return result;
}

// The one replay procedure: plays |picture| onto PaintTarget(surface, fill)
// and returns the target. |*played| (when given) is QPicture::play()'s
// result, which is true for an empty stream and for missing list entries
// in optimized Qt.
[[nodiscard]] inline QImage PaintReplay(
		QPicture &picture,
		const PaintSurface &surface,
		const QColor &fill,
		bool *played) {
	auto result = PaintTarget(surface, fill);
	auto painter = QPainter(&result);
	const auto ok = picture.play(&painter);
	painter.end();
	if (played) {
		*played = ok;
	}
	return result;
}

// SHA-256 hex over the bytesPerLine() bytes of every scanline.
[[nodiscard]] inline QString PaintImageDigest(const QImage &image) {
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	for (auto y = 0; y != image.height(); ++y) {
		hash.addData(QByteArrayView(
			reinterpret_cast<const char*>(image.constScanLine(y)),
			image.bytesPerLine()));
	}
	return QString::fromLatin1(hash.result().toHex());
}

// The number of device pixels that differ between |a| and |b|, and their
// device-pixel bounds in |*bounds| (when given; empty when none differ).
// -1 when the sizes or formats differ.
[[nodiscard]] inline qint64 PaintDifference(
		const QImage &a,
		const QImage &b,
		QRect *bounds) {
	if (bounds) {
		*bounds = QRect();
	}
	if (a.size() != b.size() || a.format() != b.format()) {
		return -1;
	}
	const auto depth = a.depth();
	const auto bytes = depth / 8;
	auto count = qint64(0);
	auto left = a.width();
	auto top = a.height();
	auto right = -1;
	auto bottom = -1;
	for (auto y = 0; y != a.height(); ++y) {
		const auto lineA = a.constScanLine(y);
		const auto lineB = b.constScanLine(y);
		for (auto x = 0; x != a.width(); ++x) {
			const auto same = (depth % 8)
				? (a.pixel(x, y) == b.pixel(x, y))
				: !std::memcmp(lineA + x * bytes, lineB + x * bytes, bytes);
			if (same) {
				continue;
			}
			++count;
			left = std::min(left, x);
			top = std::min(top, y);
			right = std::max(right, x);
			bottom = std::max(bottom, y);
		}
	}
	if (bounds && count) {
		*bounds = QRect(QPoint(left, top), QPoint(right, bottom));
	}
	return count;
}

// The text fonts |picture| replays with, as sorted unique {family,
// resolved} objects: the family each text item requests and the family
// QFontInfo resolves it to in this process. Text items are re-laid-out at
// replay, so a font missing on the replay side resolves differently here.
[[nodiscard]] inline QJsonArray PaintFonts(const QPicture &picture) {
	auto copy = picture;
	auto device = details::PaintInventoryDevice();
	{
		auto painter = QPainter(&device);
		copy.play(&painter);
	}
	return device.fonts();
}

// Recording fidelity of one timed recording: the differing device pixels
// between a direct QWidget::render() of |owner| (the recording's region,
// offset and flags) onto a sentinel-0 target and PaintReplay() of the
// in-memory recording. 0 means the stream reproduces what the owner paints.
// Call right after RecordPaint(), before the owner's state changes, and
// store the result in |recording.directDifference|.
[[nodiscard]] inline qint64 PaintDirectDifference(
		QWidget *owner,
		const PaintRecording &recording,
		const PaintSurface &surface) {
	auto direct = PaintTarget(surface, PaintSentinel(0));
	{
		auto painter = QPainter(&direct);
		owner->render(
			&painter,
			recording.region.topLeft(),
			QRegion(recording.region),
			QWidget::RenderFlags::fromInt(recording.flags));
	}
	auto picture = recording.picture;
	const auto replay = PaintReplay(
		picture,
		surface,
		PaintSentinel(0),
		nullptr);
	return PaintDifference(direct, replay, nullptr);
}

// This process's side of the replay contract: {version: qVersion(), debug:
// QLibraryInfo::isDebugBuild(), pictureFormat, sidecar, platform,
// colorScheme ("unknown" | "light" | "dark")}. Text rasterization depends
// on the platform plugin and its color scheme (cocoa draws grayscale-smoothed
// glyphs differently in Dark and Light), so the replayer runs under the
// recorded platform and refuses another scheme. Needs a QGuiApplication.
[[nodiscard]] inline QJsonObject PaintQtIdentity() {
	const auto scheme = QGuiApplication::styleHints()->colorScheme();
	const auto schemeName = (scheme == Qt::ColorScheme::Light)
		? QStringLiteral("light")
		: (scheme == Qt::ColorScheme::Dark)
		? QStringLiteral("dark")
		: QStringLiteral("unknown");
	return QJsonObject{
		{ QStringLiteral("version"), QString::fromLatin1(qVersion()) },
		{ QStringLiteral("debug"), QLibraryInfo::isDebugBuild() },
		{
			QStringLiteral("pictureFormat"),
			int(QDataStream::Qt_DefaultCompiledVersion),
		},
		{ QStringLiteral("sidecar"), kPaintSidecarVersion },
		{ QStringLiteral("platform"), QGuiApplication::platformName() },
		{ QStringLiteral("colorScheme"), schemeName },
	};
}

// {rootType, size: [w, h], dpr}.
[[nodiscard]] inline QJsonObject PaintSurfaceJson(
		const PaintSurface &surface) {
	return QJsonObject{
		{ QStringLiteral("rootType"), surface.rootType },
		{
			QStringLiteral("size"),
			QJsonArray{ surface.size.width(), surface.size.height() },
		},
		{ QStringLiteral("dpr"), surface.dpr },
	};
}

// The inverse of PaintSurfaceJson(). A missing or malformed key reads as an
// empty type, an invalid size or a zero ratio, which the caller refuses.
[[nodiscard]] inline PaintSurface PaintSurfaceFromJson(
		const QJsonObject &json) {
	auto result = PaintSurface();
	result.rootType = json.value(QStringLiteral("rootType")).toString();
	const auto size = json.value(QStringLiteral("size")).toArray();
	result.size = (size.size() == 2)
		? QSize(size.at(0).toInt(), size.at(1).toInt())
		: QSize();
	result.dpr = json.value(QStringLiteral("dpr")).toDouble();
	return result;
}

// Saves one recording into |dir| (created when missing). Call after the
// timed window. Writes <name>.qpic (QPicture::save()) and <name>.tables
// (details::WriteSidecar()), reads both back for their identities, replays
// the in-memory recording over PaintSentinel(0) for referenceSha256 and
// takes PaintFonts(). Returns the manifest row: name, role, recordMs, root
// {type, size [w, h], region [x, y, w, h], flags}, qpic and tables {file,
// bytes, sha256}, counts {images, pixmaps, brushes, pens}, fonts,
// referenceSha256, directDifference (only when measured), then the caller's
// fields. On failure returns an empty object and sets |*error| to
// "<code>: <detail>": an invalid stream name (letters, digits, '_', '.',
// '-'), a field key that collides with a row key, a picture not in
// in_memory_only mode, an empty surface, or a write, verify or
// reference-playback failure.
[[nodiscard]] inline QJsonObject SavePaintRecording(
		const QString &dir,
		const PaintRecording &recording,
		const PaintSurface &surface,
		QString *error) {
	const auto fail = [&](const QString &code, const QString &detail) {
		if (error) {
			*error = code + QStringLiteral(": ") + detail;
		}
		return QJsonObject();
	};
	if (!details::ValidStreamName(recording.name)) {
		return fail(QStringLiteral("invalid-stream-name"), recording.name);
	}
	const auto &reserved = details::ReservedRowKeys();
	for (auto i = recording.fields.begin(); i != recording.fields.end(); ++i) {
		if (reserved.contains(i.key())) {
			return fail(QStringLiteral("reserved-field-key"), i.key());
		}
	}
	auto picture = recording.picture;
	if (!picture.data_ptr()->in_memory_only || picture.paintingActive()) {
		return fail(QStringLiteral("not-in-memory-picture"), recording.name);
	}
	if (!QDir().mkpath(dir)) {
		return fail(QStringLiteral("cannot-create-directory"), dir);
	}
	if (surface.size.isEmpty() || !(surface.dpr > 0.)) {
		return fail(QStringLiteral("invalid-surface"), surface.rootType);
	}
	auto commands = QByteArray();
	{
		auto buffer = QBuffer(&commands);
		if (!buffer.open(QIODevice::WriteOnly) || !picture.save(&buffer)) {
			return fail(QStringLiteral("stream-save-failed"), recording.name);
		}
	}
	auto ok = false;
	const auto tables = details::WriteSidecar(*picture.data_ptr(), &ok);
	if (!ok) {
		return fail(QStringLiteral("sidecar-write-failed"), recording.name);
	}
	const auto folder = QDir(dir);
	const auto qpicFile = recording.name + QStringLiteral(".qpic");
	const auto tablesFile = recording.name + QStringLiteral(".tables");
	auto identities = QJsonObject();
	for (const auto &[file, bytes] : {
			std::pair{ qpicFile, commands },
			std::pair{ tablesFile, tables } }) {
		const auto path = folder.filePath(file);
		if (!details::WriteFileBytes(path, bytes)) {
			return fail(QStringLiteral("write-failed"), path);
		}
		const auto written = details::ReadFileBytes(path);
		if (!written || *written != bytes) {
			return fail(QStringLiteral("write-verify-failed"), path);
		}
		auto identity = details::BytesIdentity(*written);
		identity.insert(QStringLiteral("file"), file);
		identities.insert(file, identity);
	}
	auto played = false;
	const auto reference = PaintReplay(
		picture,
		surface,
		PaintSentinel(0),
		&played);
	if (!played) {
		return fail(
			QStringLiteral("reference-playback-failed"),
			recording.name);
	}
	const auto &data = *picture.data_ptr();
	const auto &region = recording.region;
	auto row = QJsonObject{
		{ QStringLiteral("name"), recording.name },
		{ QStringLiteral("role"), recording.role },
		{ QStringLiteral("recordMs"), recording.recordNs / 1e6 },
		{ QStringLiteral("root"), QJsonObject{
			{ QStringLiteral("type"), recording.rootType },
			{
				QStringLiteral("size"),
				QJsonArray{
					recording.rootSize.width(),
					recording.rootSize.height(),
				},
			},
			{
				QStringLiteral("region"),
				QJsonArray{
					region.x(),
					region.y(),
					region.width(),
					region.height(),
				},
			},
			{ QStringLiteral("flags"), recording.flags },
		} },
		{ QStringLiteral("qpic"), identities.value(qpicFile) },
		{ QStringLiteral("tables"), identities.value(tablesFile) },
		{ QStringLiteral("counts"), QJsonObject{
			{ QStringLiteral("images"), qint64(data.image_list.size()) },
			{ QStringLiteral("pixmaps"), qint64(data.pixmap_list.size()) },
			{ QStringLiteral("brushes"), qint64(data.brush_list.size()) },
			{ QStringLiteral("pens"), qint64(data.pen_list.size()) },
		} },
		{ QStringLiteral("fonts"), PaintFonts(picture) },
		{ QStringLiteral("referenceSha256"), PaintImageDigest(reference) },
	};
	if (recording.directDifference >= 0) {
		row.insert(
			QStringLiteral("directDifference"),
			recording.directDifference);
	}
	for (auto i = recording.fields.begin(); i != recording.fields.end(); ++i) {
		row.insert(i.key(), i.value());
	}
	return row;
}

// Writes <dir>/manifest.json: {schema, qt: PaintQtIdentity(), surface:
// PaintSurfaceJson(), sentinels: [[r, g, b], [r, g, b]], streams: |rows|}.
// Returns false and sets |*error| when the file cannot be written.
[[nodiscard]] inline bool WritePaintManifest(
		const QString &dir,
		const PaintSurface &surface,
		const QJsonArray &rows,
		QString *error) {
	auto sentinels = QJsonArray();
	for (const auto index : { 0, 1 }) {
		const auto color = PaintSentinel(index);
		sentinels.push_back(
			QJsonArray{ color.red(), color.green(), color.blue() });
	}
	const auto manifest = QJsonObject{
		{ QStringLiteral("schema"), kPaintStreamSchema },
		{ QStringLiteral("qt"), PaintQtIdentity() },
		{ QStringLiteral("surface"), PaintSurfaceJson(surface) },
		{ QStringLiteral("sentinels"), sentinels },
		{ QStringLiteral("streams"), rows },
	};
	const auto path = QDir(dir).filePath(QStringLiteral("manifest.json"));
	if (!QDir().mkpath(dir)
		|| !details::WriteFileBytes(
			path,
			QJsonDocument(manifest).toJson(QJsonDocument::Indented))) {
		if (error) {
			*error = QStringLiteral("write-failed: ") + path;
		}
		return false;
	}
	return true;
}

// Loads the stream of manifest |row| from <dir>/<name>.qpic and
// <dir>/<name>.tables, reading every byte before any parsing, and returns
// it ready for play() in this process. |error| is one of (exact strings,
// in the order they are decided): "invalid-stream-name",
// "missing-stream-file", "empty-stream-file", "missing-sidecar",
// "empty-sidecar", "picture-format-mismatch" (the .qpic's big-endian
// format major at byte offset 6 is not this Qt's
// QDataStream::Qt_DefaultCompiledVersion), "stream-load-failed"
// (QPicture::load() refused the header or checksum), "sidecar-unreadable",
// "sidecar-version-mismatch", "sidecar-count-mismatch" (parsed list sizes
// differ from the row's counts). On success the fresh picture is put into
// in_memory_only mode after load() and given the four lists.
[[nodiscard]] inline LoadedPaintStream LoadPaintStream(
		const QString &dir,
		const QJsonObject &row) {
	auto result = LoadedPaintStream();
	const auto name = row.value(QStringLiteral("name")).toString();
	if (!details::ValidStreamName(name)) {
		result.error = QStringLiteral("invalid-stream-name");
		return result;
	}
	const auto folder = QDir(dir);
	auto qpic = details::ReadFileBytes(
		folder.filePath(name + QStringLiteral(".qpic")));
	const auto tables = details::ReadFileBytes(
		folder.filePath(name + QStringLiteral(".tables")));
	if (qpic) {
		result.identity.insert(
			QStringLiteral("qpic"),
			details::BytesIdentity(*qpic));
	}
	if (tables) {
		result.identity.insert(
			QStringLiteral("tables"),
			details::BytesIdentity(*tables));
	}
	const auto format = (qpic && qpic->size() >= 8)
		? ((int(quint8(qpic->at(6))) << 8) | int(quint8(qpic->at(7))))
		: -1;
	if (!qpic) {
		result.error = QStringLiteral("missing-stream-file");
	} else if (qpic->isEmpty()) {
		result.error = QStringLiteral("empty-stream-file");
	} else if (!tables) {
		result.error = QStringLiteral("missing-sidecar");
	} else if (tables->isEmpty()) {
		result.error = QStringLiteral("empty-sidecar");
	} else if (format >= 0
		&& format != QDataStream::Qt_DefaultCompiledVersion) {
		result.error = QStringLiteral("picture-format-mismatch");
	}
	if (!result.error.isEmpty()) {
		return result;
	}
	auto buffer = QBuffer(&*qpic);
	if (!buffer.open(QIODevice::ReadOnly) || !result.picture.load(&buffer)) {
		result.error = QStringLiteral("stream-load-failed");
		return result;
	}
	auto sidecar = details::PaintSidecar();
	result.error = details::ReadSidecar(*tables, &sidecar);
	if (!result.error.isEmpty()) {
		return result;
	}
	const auto counts = row.value(QStringLiteral("counts")).toObject();
	const auto count = [&](const char *key) {
		return counts.value(QString::fromLatin1(key)).toInteger(-1);
	};
	if (count("images") != sidecar.images.size()
		|| count("pixmaps") != sidecar.pixmaps.size()
		|| count("brushes") != sidecar.brushes.size()
		|| count("pens") != sidecar.pens.size()) {
		result.error = QStringLiteral("sidecar-count-mismatch");
		return result;
	}
	auto &data = *result.picture.data_ptr();
	data.in_memory_only = true;
	data.image_list = std::move(sidecar.images);
	data.pixmap_list = std::move(sidecar.pixmaps);
	data.brush_list = std::move(sidecar.brushes);
	data.pen_list = std::move(sidecar.pens);
	return result;
}

} // namespace Test
