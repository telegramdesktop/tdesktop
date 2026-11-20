/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_backend.h"

#include "core/src/png.h"
#include "tgw/tgw_canvas.h"
#include "tgw/tgw_emoji.h"
#include "tgw/tgw_emoji_images.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_renderer_rules.h"
#include "tgw/tgw_renderer_work.h"
#include "twidget/sandbox_engine.h"

#include <QtCore/QBuffer>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>

#include <zlib.h>

#include "styles/style_basic.h"

namespace Tgw {
namespace {

using RendererRules::kMaxCanvasFonts;
using RendererRules::kMaxNewCanvasFonts;

constexpr auto kMaxFontSize = 1e6;
constexpr auto kMaxFontPixelSize = 1024;
constexpr auto kMaxLiveFonts = 16384;
constexpr auto kMaxFamiliesBytes = std::size_t(128 * 1024);
constexpr auto kMaxMeasuredText = std::size_t(320 * 1024);
constexpr auto kMaxImageUrlSize = std::size_t(5 * 1024 * 1024);
constexpr auto kMaxImageSide = 4096;
constexpr auto kMaxImagePixels = 4 * 1024 * 1024;
constexpr auto kImagePixelBytes = 4;
constexpr auto kMaxBackendImageBytes = int64(32 * 1024 * 1024);
constexpr auto kMaxBackendImages = 4096;
constexpr auto kMaxTotalImageBytes = int64(128 * 1024 * 1024);
constexpr auto kMaxTotalImages = 32768;
constexpr auto kMaxPublishedSeconds = 1e7;
constexpr auto kFrameInterval = crl::time(1000) / st::universalDuration;
constexpr auto kAsciiSpaces = std::string_view(" \t\n\f\r");
constexpr auto kDataPrefix = std::string_view("data:");
constexpr auto kBase64 = std::string_view("base64");
constexpr auto kGifFormat = std::string_view("gif");
constexpr auto kGifScreenOffset = 6;
constexpr auto kPngFormat = std::string_view("png");
constexpr auto kPngSignature = std::string_view("\x89PNG\r\n\x1a\n", 8);
constexpr auto kPngChunkFrame = 12;
constexpr auto kPngHeaderOffset = 16;
constexpr auto kPngHeaderSize = 13;

constexpr auto kFontWeights = std::array{
	QFont::Thin,
	QFont::ExtraLight,
	QFont::Light,
	QFont::Normal,
	QFont::Medium,
	QFont::DemiBold,
	QFont::Bold,
	QFont::ExtraBold,
	QFont::Black,
};

enum class FontKind : uchar {
	Regular,
	Monospace,
};

struct GenericFamily {
	std::string_view name;
	FontKind kind = FontKind::Regular;
};

constexpr auto kGenericFamilies = std::array{
	GenericFamily{ "-apple-system", FontKind::Regular },
	GenericFamily{ "blinkmacsystemfont", FontKind::Regular },
	GenericFamily{ "system-ui", FontKind::Regular },
	GenericFamily{ "sans-serif", FontKind::Regular },
	GenericFamily{ "ui-sans-serif", FontKind::Regular },
	GenericFamily{ "serif", FontKind::Regular },
	GenericFamily{ "ui-serif", FontKind::Regular },
	GenericFamily{ "ui-rounded", FontKind::Regular },
	GenericFamily{ "cursive", FontKind::Regular },
	GenericFamily{ "fantasy", FontKind::Regular },
	GenericFamily{ "emoji", FontKind::Regular },
	GenericFamily{ "math", FontKind::Regular },
	GenericFamily{ "fangsong", FontKind::Regular },
	GenericFamily{ "ui-monospace", FontKind::Monospace },
	GenericFamily{ "monospace", FontKind::Monospace },
};

struct ImageType {
	std::string_view mime;
	std::string_view format;
};

constexpr auto kImageTypes = std::array{
	ImageType{ "image/png", "png" },
	ImageType{ "image/jpeg", "jpeg" },
	ImageType{ "image/jpg", "jpeg" },
	ImageType{ "image/gif", kGifFormat },
	ImageType{ "image/webp", "webp" },
};

struct DataUrl {
	std::string_view format;
	QByteArray bytes;
};

struct DecodedImage {
	QImage image;
	QSizeF size;
};

struct PngHeader {
	QByteArray bytes;
	QSizeF size;
	uint32 width = 0;
	uchar depth = 0;
	uchar type = 0;
};

struct ImageCharges {
	int64 bytes = 0;
	int count = 0;
};

[[nodiscard]] ImageCharges &TotalCharges() {
	static auto result = ImageCharges();
	return result;
}

[[nodiscard]] std::chrono::steady_clock::time_point PublishedAt(
		float64 seconds) {
	const auto in = std::chrono::duration<float64>(
		std::clamp(seconds, 0., kMaxPublishedSeconds));
	return std::chrono::steady_clock::now()
		+ std::chrono::duration_cast<std::chrono::steady_clock::duration>(in);
}

[[nodiscard]] std::string_view Trimmed(std::string_view value) {
	const auto from = value.find_first_not_of(kAsciiSpaces);
	if (from == std::string_view::npos) {
		return {};
	}
	const auto till = value.find_last_not_of(kAsciiSpaces);
	return value.substr(from, till - from + 1);
}

[[nodiscard]] std::string AsciiLower(std::string_view value) {
	auto result = std::string(value);
	for (auto &ch : result) {
		if (ch >= 'A' && ch <= 'Z') {
			ch = char(ch - 'A' + 'a');
		}
	}
	return result;
}

[[nodiscard]] int FontWeightIndex(int weight) {
	return (std::clamp(weight, 100, 900) + 50) / 100 - 1;
}

[[nodiscard]] std::optional<FontKind> GenericKind(std::string_view family) {
	const auto name = AsciiLower(Trimmed(family));
	for (const auto &entry : kGenericFamilies) {
		if (entry.name == name) {
			return entry.kind;
		}
	}
	return std::nullopt;
}

[[nodiscard]] FontKind ResolveFamilies(std::string_view families) {
	auto quote = char();
	auto escaped = false;
	auto from = std::size_t();
	for (auto i = std::size_t(); i <= families.size(); ++i) {
		const auto ch = (i < families.size()) ? families[i] : ',';
		if (escaped) {
			escaped = false;
		} else if (ch == '\\') {
			escaped = true;
		} else if (quote) {
			if (ch == quote) {
				quote = char();
			}
		} else if (ch == '"' || ch == '\'') {
			quote = ch;
		} else if (ch == ',') {
			if (const auto kind = GenericKind(
					families.substr(from, i - from))) {
				return *kind;
			}
			from = i + 1;
		}
	}
	return FontKind::Regular;
}

[[nodiscard]] std::pair<uint32, float64> ResolveFont(
		std::string_view families,
		float size,
		int weight,
		bool italic) {
	const auto style = (uint32(FontWeightIndex(weight)) << 2)
		| (uint32(italic) << 1)
		| uint32(ResolveFamilies(families));
	const auto requested = std::isfinite(size)
		? std::clamp(float64(size), 0., kMaxFontSize)
		: 0.;
	return { style, requested };
}

[[nodiscard]] QFont StyledFont(
		const QString &family,
		uint32 style,
		int pixels) {
	auto result = QFont(family);
	result.setPixelSize(pixels);
	result.setWeight(kFontWeights[style >> 2]);
	result.setItalic((style & 2) != 0);
	return result;
}

[[nodiscard]] std::string_view ImageFormat(std::string_view mime) {
	for (const auto &type : kImageTypes) {
		if (type.mime == mime) {
			return type.format;
		}
	}
	return {};
}

[[nodiscard]] bool IsHexDigit(char ch) {
	return (ch >= '0' && ch <= '9')
		|| (ch >= 'a' && ch <= 'f')
		|| (ch >= 'A' && ch <= 'F');
}

[[nodiscard]] QByteArray PercentDecoded(std::string_view data) {
	auto i = data.find('%');
	while (i != std::string_view::npos) {
		if ((data.size() - i < 3)
			|| !IsHexDigit(data[i + 1])
			|| !IsHexDigit(data[i + 2])) {
			return QByteArray();
		}
		i = data.find('%', i + 3);
	}
	return QByteArray::fromPercentEncoding(
		QByteArray::fromRawData(data.data(), int(data.size())));
}

[[nodiscard]] QByteArray Base64Decoded(QByteArray data) {
	const auto space = [](char ch) {
		return (kAsciiSpaces.find(ch) != std::string_view::npos);
	};
	const auto end = std::remove_if(data.begin(), data.end(), space);
	data.truncate(int(end - data.begin()));
	auto result = QByteArray::fromBase64Encoding(
		std::move(data),
		QByteArray::Base64Encoding
			| QByteArray::AbortOnBase64DecodingErrors);
	return result ? std::move(*result) : QByteArray();
}

[[nodiscard]] DataUrl ParseDataUrl(std::string_view url) {
	if (url.size() > kMaxImageUrlSize || !url.starts_with(kDataPrefix)) {
		return {};
	}
	const auto comma = url.find(',');
	if (comma == std::string_view::npos) {
		return {};
	}
	const auto meta = url.substr(
		kDataPrefix.size(),
		comma - kDataPrefix.size());
	const auto format = ImageFormat(
		AsciiLower(Trimmed(meta.substr(0, meta.find(';')))));
	if (format.empty()) {
		return {};
	}
	const auto last = meta.rfind(';');
	const auto base64 = (last != std::string_view::npos)
		&& (AsciiLower(Trimmed(meta.substr(last + 1))) == kBase64);
	auto bytes = PercentDecoded(url.substr(comma + 1));
	return {
		format,
		base64 ? Base64Decoded(std::move(bytes)) : std::move(bytes),
	};
}

[[nodiscard]] bool ValidImageSize(QSizeF size) {
	const auto width = size.width();
	const auto height = size.height();
	return (width > 0.)
		&& (height > 0.)
		&& (width <= kMaxImageSide)
		&& (height <= kMaxImageSide)
		&& (width * height <= kMaxImagePixels);
}

[[nodiscard]] int64 ImageBytes(QSizeF size) {
	return int64(size.width()) * int64(size.height()) * kImagePixelBytes;
}

[[nodiscard]] bool FitsBudget(const QImage &image, int64 budget) {
	return !image.isNull()
		&& ValidImageSize(QSizeF(image.size()))
		&& (int64(image.sizeInBytes()) <= budget);
}

[[nodiscard]] bool GifScreenFits(const QByteArray &bytes, int64 budget) {
	if (bytes.size() < kGifScreenOffset + 4
		|| (!bytes.startsWith("GIF87a") && !bytes.startsWith("GIF89a"))) {
		return false;
	}
	const auto side = [&](int index) {
		return int(uchar(bytes[index])) | (int(uchar(bytes[index + 1])) << 8);
	};
	const auto size = QSizeF(
		side(kGifScreenOffset),
		side(kGifScreenOffset + 2));
	return ValidImageSize(size) && (ImageBytes(size) <= budget);
}

[[nodiscard]] uint32 ReadUInt32(const QByteArray &bytes, qsizetype at) {
	auto result = uint32();
	for (auto i = 0; i != 4; ++i) {
		result = (result << 8) | uint32(uchar(bytes[at + i]));
	}
	return result;
}

void AppendUInt32(QByteArray &bytes, uint32 value) {
	for (auto shift = 24; shift >= 0; shift -= 8) {
		bytes.append(char(uchar(value >> shift)));
	}
}

void AppendPngChunk(
		QByteArray &png,
		std::string_view type,
		const QByteArray &data) {
	AppendUInt32(png, uint32(data.size()));
	const auto from = png.size();
	png.append(type.data(), qsizetype(type.size()));
	png.append(data);
	AppendUInt32(png, uint32(crc32(
		0,
		reinterpret_cast<const Bytef*>(png.constData() + from),
		uInt(png.size() - from))));
}

[[nodiscard]] bool ValidPngDepth(uchar type, uchar depth) {
	const auto packed = (depth == 1) || (depth == 2) || (depth == 4);
	switch (type) {
	case 0: return packed || (depth == 8) || (depth == 16);
	case 3: return packed || (depth == 8);
	case 2:
	case 4:
	case 6: return (depth == 8) || (depth == 16);
	}
	return false;
}

[[nodiscard]] std::optional<PngHeader> ReadPngHeader(const QByteArray &bytes) {
	const auto signature = QByteArray::fromRawData(
		kPngSignature.data(),
		qsizetype(kPngSignature.size()));
	if ((bytes.size() < kPngHeaderOffset + kPngHeaderSize + 4)
		|| !bytes.startsWith(signature)
		|| (bytes.mid(kPngHeaderOffset - 4, 4) != "IHDR")
		|| (ReadUInt32(bytes, kPngHeaderOffset - 8) < kPngHeaderSize)) {
		return std::nullopt;
	}
	return PngHeader{
		.bytes = bytes.mid(kPngHeaderOffset, kPngHeaderSize),
		.size = QSizeF(
			ReadUInt32(bytes, kPngHeaderOffset),
			ReadUInt32(bytes, kPngHeaderOffset + 4)),
		.width = ReadUInt32(bytes, kPngHeaderOffset),
		.depth = uchar(bytes[kPngHeaderOffset + 8]),
		.type = uchar(bytes[kPngHeaderOffset + 9]),
	};
}

[[nodiscard]] uint64 PngStride(const PngHeader &header) {
	const auto channels = (header.type == 2)
		? 3
		: (header.type == 4)
		? 2
		: (header.type == 6)
		? 4
		: 1;
	return (uint64(header.width) * channels * header.depth + 7) / 8 + 1;
}

[[nodiscard]] bool PngRecoverable(const PngHeader &header, int64 budget) {
	if (!ValidImageSize(header.size)
		|| (ImageBytes(header.size) > budget)
		|| !ValidPngDepth(header.type, header.depth)) {
		return false;
	}
	const auto rows = uint64(header.size.height());
	const auto ceiling = 2 * uint64(ImageBytes(header.size)) + rows;
	return (PngStride(header) <= ceiling / rows);
}

// The rows before the damage, of a PNG that png_intact_rows() measured.
[[nodiscard]] QByteArray IntactPng(
		const QByteArray &bytes,
		const PngHeader &header,
		size_t rows) {
	const auto stride = PngStride(header);
	auto before = QByteArray();
	auto compressed = QByteArray();
	auto at = qsizetype(kPngSignature.size());
	while (at + kPngChunkFrame <= bytes.size()) {
		const auto length = ReadUInt32(bytes, at);
		const auto left = bytes.size() - at - kPngChunkFrame;
		if (uint64(length) > uint64(left)) {
			break;
		}
		const auto size = qsizetype(length);
		const auto chunk = bytes.mid(at + 4, 4);
		if (chunk == "IEND") {
			break;
		} else if (chunk == "IDAT") {
			compressed.append(bytes.constData() + at + 8, size);
		} else if (chunk != "IHDR" && compressed.isEmpty()) {
			before.append(bytes.constData() + at, size + kPngChunkFrame);
		}
		at += size + kPngChunkFrame;
	}
	auto unpacked = QByteArray(qsizetype(stride * rows), Qt::Uninitialized);
	auto stream = z_stream();
	if (inflateInit(&stream) != Z_OK) {
		return QByteArray();
	}
	stream.next_in = reinterpret_cast<Bytef*>(compressed.data());
	stream.avail_in = uInt(compressed.size());
	stream.next_out = reinterpret_cast<Bytef*>(unpacked.data());
	stream.avail_out = uInt(unpacked.size());
	// The damage ends the stream: the rows inflated before it are kept.
	inflate(&stream, Z_FINISH);
	const auto kept = (uint64(unpacked.size()) - stream.avail_out) / stride;
	inflateEnd(&stream);
	if (!kept) {
		return QByteArray();
	}
	unpacked.truncate(qsizetype(kept * stride));
	auto packed = QByteArray(
		qsizetype(compressBound(uLong(unpacked.size()))),
		Qt::Uninitialized);
	auto packedSize = uLongf(packed.size());
	if (compress(
			reinterpret_cast<Bytef*>(packed.data()),
			&packedSize,
			reinterpret_cast<const Bytef*>(unpacked.constData()),
			uLong(unpacked.size())) != Z_OK) {
		return QByteArray();
	}
	packed.truncate(qsizetype(packedSize));
	auto shortened = QByteArray();
	AppendUInt32(shortened, header.width);
	AppendUInt32(shortened, uint32(kept));
	shortened.append(header.bytes.mid(8));
	auto result = QByteArray(
		kPngSignature.data(),
		qsizetype(kPngSignature.size()));
	AppendPngChunk(result, "IHDR", shortened);
	result.append(before);
	AppendPngChunk(result, "IDAT", packed);
	AppendPngChunk(result, "IEND", QByteArray());
	return result;
}

[[nodiscard]] DecodedImage DecodeRaster(
		QByteArray bytes,
		std::string_view format,
		int64 budget) {
	if ((format == kGifFormat) && !GifScreenFits(bytes, budget)) {
		return {};
	}
	auto buffer = QBuffer(&bytes);
	auto reader = QImageReader(
		&buffer,
		QByteArray(format.data(), int(format.size())));
	reader.setAutoDetectImageFormat(false);
	reader.setDecideFormatFromContent(false);
	reader.setAutoTransform(true);
	const auto size = QSizeF(reader.size());
	if (!ValidImageSize(size) || (ImageBytes(size) > budget)) {
		return {};
	}
	auto image = QImage();
	if (!reader.read(&image) || !ValidImageSize(QSizeF(image.size()))) {
		return {};
	}
	image = std::move(image).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	if (!FitsBudget(image, budget)) {
		return {};
	}
	const auto intrinsic = QSizeF(image.size());
	return { std::move(image), intrinsic };
}

// WHY: Chromium shows a damaged PNG's rows before the damage and leaves
// the rest transparent, where Qt's decoder refuses the whole image.
[[nodiscard]] DecodedImage DecodeDamagedPng(
		const QByteArray &bytes,
		const PngHeader &header,
		size_t intact,
		int64 budget) {
	auto image = QImage(
		header.size.toSize(),
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	const auto rows = intact
		? DecodeRaster(IntactPng(bytes, header, intact), kPngFormat, budget)
		: DecodedImage();
	if (!rows.image.isNull()) {
		auto p = QPainter(&image);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.drawImage(0, 0, rows.image);
	}
	return { std::move(image), header.size };
}

[[nodiscard]] DecodedImage ReadRaster(
		QByteArray bytes,
		std::string_view format,
		int64 budget) {
	if (format != kPngFormat) {
		return DecodeRaster(std::move(bytes), format, budget);
	} else if (const auto header = ReadPngHeader(bytes)) {
		if (!PngRecoverable(*header, budget)) {
			return {};
		}
		const auto intact = twlite::png_intact_rows(
			reinterpret_cast<const uint8_t*>(bytes.constData()),
			size_t(bytes.size()));
		if (intact != SIZE_MAX) {
			return DecodeDamagedPng(bytes, *header, intact, budget);
		}
	}
	return DecodeRaster(std::move(bytes), format, budget);
}

} // namespace

QtBackend::QtBackend(
	Diagnostic diagnostic,
	Fn<void()> refused,
	QString regularFamily,
	QString monospaceFamily,
	std::vector<FontPosition> positions)
: _diagnostic(diagnostic)
, _refused(std::move(refused))
, _regularFamily(std::move(regularFamily))
, _monospaceFamily(std::move(monospaceFamily))
, _positions(std::move(positions))
, _canvases(std::make_unique<Canvases>(std::move(diagnostic)))
, _emoji(std::make_unique<EmojiAccount>())
, _meter([=](QString reason) { refuse(std::move(reason)); })
, _canvasFontsLeft(kMaxNewCanvasFonts) {
}

QtBackend::~QtBackend() {
	auto &total = TotalCharges();
	total.bytes -= _imageBytes;
	total.count -= int(_images.size());
}

bool QtBackend::serves(const WidgetMeter::Work &work) const {
	return !_released && work.admitted() && !refused();
}

int QtBackend::create_font(
		const std::string &families,
		float size,
		int weight,
		bool italic,
		const std::string&,
		twlite::FontMetrics &out) {
	const auto work = _meter.work();
	if (!serves(work) || !admitFamilies(families)) {
		out = twlite::FontMetrics();
		return 0;
	}
	return makeFont(ResolveFont(families, size, weight, italic), out, false);
}

int QtBackend::create_canvas_font(
		const std::string &families,
		float size,
		int weight,
		bool italic,
		twlite::FontMetrics &out) {
	const auto work = _meter.work();
	if (!serves(work) || !admitFamilies(families)) {
		return refuseCanvasFont(out);
	}
	return admitCanvasFont(ResolveFont(families, size, weight, italic), out);
}

int QtBackend::makeFont(
		FontKey key,
		twlite::FontMetrics &out,
		bool canvas) {
	const auto fail = [&](QString limit) {
		refuse(std::move(limit));
		out = twlite::FontMetrics();
		return 0;
	};
	if (int(_fonts.size()) >= kMaxLiveFonts) {
		return fail(PaintLimit(
			u"%1 live fonts"_q.arg(_fonts.size() + 1),
			kMaxLiveFonts));
	} else if (_lastFontId == std::numeric_limits<int>::max()) {
		return fail(PaintLimit(
			u"%1 font handles"_q.arg(int64(_lastFontId) + 1),
			std::numeric_limits<int>::max()));
	}
	const auto requested = key.second;
	const auto pixels = int(std::clamp(
		std::round(requested),
		1.,
		float64(kMaxFontPixelSize)));
	const auto scale = requested / pixels;
	const auto &family = (FontKind(key.first & 1) == FontKind::Monospace)
		? _monospaceFamily
		: _regularFamily;
	const auto font = StyledFont(family, key.first, pixels);
	AdoptFontPositions(font, _positions);
	const auto metrics = QFontMetricsF(font);
	out.ascent = float(metrics.ascent() * scale);
	out.descent = float(metrics.descent() * scale);
	out.height = float(metrics.lineSpacing() * scale);
	out.x_height = float(metrics.xHeight() * scale);
	out.zero_width = float(metrics.horizontalAdvance(QChar('0')) * scale);
	const auto id = ++_lastFontId;
	_fonts.emplace(id, QtFont{ font, metrics, scale, canvas });
	return id;
}

void QtBackend::delete_font(int font) {
	const auto i = _fonts.find(font);
	if (i == end(_fonts)) {
		return;
	} else if (i->second.canvas) {
		const auto j = ranges::find_if(_canvasFonts, [&](const auto &entry) {
			return (entry.second.id == font);
		});
		if (j != end(_canvasFonts)) {
			_canvasFonts.erase(j);
		}
	}
	_fonts.erase(i);
}

float QtBackend::text_width(int font, const std::string &text) {
	const auto work = _meter.work();
	if (!serves(work)) {
		return 0.f;
	} else if (text.size() > kMaxMeasuredText) {
		refuse(PaintLimit(
			u"%1 bytes of measured text"_q.arg(text.size()),
			kMaxMeasuredText));
		return 0.f;
	}
	const auto i = _fonts.find(font);
	if (i == end(_fonts)) {
		return 0.f;
	}
	const auto &entry = i->second;
	const auto utf16 = QString::fromUtf8(text.data(), int(text.size()));
	const auto cells = entry.canvas
		? std::vector<EmojiCell>()
		: FindEmojiCells(utf16);
	if (cells.empty()) {
		return float(entry.metrics.horizontalAdvance(utf16) * entry.scale);
	}
	const auto width = WalkTextWithEmoji(
		entry.metrics,
		utf16,
		cells,
		entry.font.pixelSize());
	return float(width * entry.scale);
}

bool QtBackend::load_image(
		int id,
		const std::string &url,
		float &width,
		float &height) {
	const auto work = _meter.work();
	if (!serves(work) || _images.contains(id)) {
		return false;
	} else if (int(_images.size()) >= kMaxBackendImages) {
		refuse(PaintLimit(
			u"%1 images"_q.arg(_images.size() + 1),
			kMaxBackendImages));
		return false;
	}
	auto &total = TotalCharges();
	if (total.count >= kMaxTotalImages) {
		return false;
	}
	auto data = ParseDataUrl(url);
	if (data.bytes.isEmpty()) {
		return false;
	}
	const auto budget = std::min(
		kMaxBackendImageBytes - _imageBytes,
		kMaxTotalImageBytes - total.bytes);
	auto decoded = ReadRaster(std::move(data.bytes), data.format, budget);
	if (decoded.image.isNull()) {
		return false;
	}
	const auto bytes = int64(decoded.image.sizeInBytes());
	_imageBytes += bytes;
	total.bytes += bytes;
	++total.count;
	_images.emplace(id, Image{ std::move(decoded.image), decoded.size });
	width = float(decoded.size.width());
	height = float(decoded.size.height());
	return true;
}

void QtBackend::canvas_run(
		int id,
		int width,
		int height,
		const std::vector<twlite::CanvasOp> &ops) {
	const auto work = _meter.work();
	if (!serves(work)) {
		return;
	}
	auto refusal = _canvases->run(id, width, height, ops, *this);
	if (!refusal.isEmpty()) {
		refuse(std::move(refusal));
	}
}

bool QtBackend::canvas_read(
		int id,
		int x,
		int y,
		int width,
		int height,
		std::vector<uint8_t> &rgba) {
	const auto work = _meter.work();
	if (!serves(work)) {
		return false;
	}
	return _canvases->read(id, x, y, width, height, rgba);
}

void QtBackend::canvas_release(int id) {
	_canvases->release(id);
}

const QtFont *QtBackend::font(int id) const {
	const auto i = _fonts.find(id);
	return (i != end(_fonts)) ? &i->second : nullptr;
}

const QImage *QtBackend::image(int id) const {
	const auto i = _images.find(id);
	return (i != end(_images)) ? &i->second.image : nullptr;
}

QSizeF QtBackend::imageSize(int id) const {
	const auto i = _images.find(id);
	return (i != end(_images)) ? i->second.size : QSizeF();
}

const QImage *QtBackend::canvas(int id) const {
	return _canvases->bitmap(id);
}

const EmojiAccount &QtBackend::emoji() const {
	return *_emoji;
}

bool QtBackend::beginFrame() {
	const auto now = crl::now();
	if (_meter.heldFor() || (now - _framed < kFrameInterval)) {
		return false;
	}
	_framed = now;
	_canvases->refill();
	_canvasFontsLeft = kMaxNewCanvasFonts;
	return true;
}

void QtBackend::noteCanvasFontRefused() {
	if (!std::exchange(_canvasFontLimitNoted, true) && _diagnostic) {
		_diagnostic(
			std::string(RendererRules::kCanvasFontLimitKind),
			RendererRules::CanvasFontLimitText());
	}
}

void QtBackend::releaseLive() {
	if (std::exchange(_released, true)) {
		return;
	}
	auto &total = TotalCharges();
	total.bytes -= base::take(_imageBytes);
	total.count -= int(_images.size());
	base::take(_images);
	base::take(_fonts);
	base::take(_canvasFonts);
	_canvases->clear();
	_canvasFontsLeft = 0;
	_emoji->close();
}

WidgetMeter::Operation QtBackend::operation() {
	return _meter.operation();
}

WidgetMeter::Work QtBackend::work() {
	return _meter.work();
}

WidgetMeter::Work QtBackend::workUnlessHeld() {
	return _meter.workUnlessHeld();
}

crl::time QtBackend::heldFor() const {
	return _meter.heldFor();
}

void QtBackend::input() {
	_meter.input();
}

void QtBackend::publishWaits(
		std::shared_ptr<twidget::SandboxHostedWait> wait) {
	if (!wait) {
		_meter.publish(nullptr, nullptr);
		return;
	}
	_meter.publish([=](float64 seconds) {
		wait->hold(PublishedAt(seconds));
	}, [=](float64 seconds) {
		wait->exempt(PublishedAt(seconds));
	});
}

bool QtBackend::refused() const {
	return !_refusal.isEmpty();
}

const QString &QtBackend::refusal() const {
	return _refusal;
}

bool QtBackend::admitFamilies(const std::string &families) {
	if (families.size() <= kMaxFamiliesBytes) {
		return true;
	}
	refuse(PaintLimit(
		u"%1 bytes of font families"_q.arg(families.size()),
		kMaxFamiliesBytes));
	return false;
}

int QtBackend::admitCanvasFont(FontKey key, twlite::FontMetrics &out) {
	const auto i = _canvasFonts.find(key);
	if (i != end(_canvasFonts)) {
		out = i->second.metrics;
		return i->second.id;
	} else if (_canvasFontsLeft <= 0
		|| int(_canvasFonts.size()) >= kMaxCanvasFonts) {
		return refuseCanvasFont(out);
	}
	const auto id = makeFont(key, out, true);
	if (!id) {
		return refuseCanvasFont(out);
	}
	twlite::set_em_box(out, out.ascent, out.descent, float(key.second));
	_canvasFonts.emplace(key, CachedFont{ id, out });
	--_canvasFontsLeft;
	return id;
}

int QtBackend::refuseCanvasFont(twlite::FontMetrics &out) {
	out = twlite::FontMetrics();
	noteCanvasFontRefused();
	return 0;
}

void QtBackend::refuse(QString reason) {
	if (!_refusal.isEmpty()) {
		return;
	}
	_refusal = std::move(reason);
	if (_refused) {
		_refused();
	}
}

} // namespace Tgw
