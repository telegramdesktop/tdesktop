/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_backend.h"

#include "ui/image/svg_safety.h"

#include <QtCore/QBuffer>
#include <QtCore/QXmlStreamReader>
#include <QtGui/QImageReader>
#include <QtSvg/QSvgRenderer>

#include "styles/style_basic.h"

namespace Tgw {
namespace {

constexpr auto kMaxFontSize = 1e6;
constexpr auto kMaxFontPixelSize = 1024;
constexpr auto kMaxImageUrlSize = std::size_t(5 * 1024 * 1024);
constexpr auto kMaxImageSide = 4096;
constexpr auto kMaxImagePixels = 4 * 1024 * 1024;
constexpr auto kImagePixelBytes = 4;
constexpr auto kMaxBackendImageBytes = int64(32 * 1024 * 1024);
constexpr auto kMaxBackendImages = 1024;
constexpr auto kMaxTotalImageBytes = int64(128 * 1024 * 1024);
constexpr auto kMaxTotalImages = 4096;
constexpr auto kAsciiSpaces = std::string_view(" \t\n\f\r");
constexpr auto kDataPrefix = std::string_view("data:");
constexpr auto kBase64 = std::string_view("base64");
constexpr auto kSvgFormat = std::string_view("svg");
constexpr auto kGifFormat = std::string_view("gif");
constexpr auto kGifScreenOffset = 6;
constexpr auto kMaxSvgDashTurns = float64(1 << 20);
constexpr auto kMaxSvgNumberLength = std::size_t(64);
constexpr auto kSvgNumberStart = std::string_view("+-.0123456789");
constexpr auto kSvgDeclarationEnd = std::string_view(";{}");
constexpr auto kCssComment = std::string_view("/*");
constexpr auto kCssPlusExponent = std::string_view("e+");

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
	ImageType{ "image/avif", "avif" },
	ImageType{ "image/bmp", "bmp" },
	ImageType{ "image/x-icon", "ico" },
	ImageType{ "image/vnd.microsoft.icon", "ico" },
	ImageType{ "image/svg+xml", kSvgFormat },
};

struct DataUrl {
	std::string_view format;
	QByteArray bytes;
};

struct DecodedImage {
	QImage image;
	QSizeF size;
};

struct ImageCharges {
	int64 bytes = 0;
	int count = 0;
};

struct SvgDashProperty {
	std::string_view name;
	bool offset = false;
};

constexpr auto kSvgDashProperties = std::array{
	SvgDashProperty{ "stroke-dashoffset", true },
	SvgDashProperty{ "stroke-dasharray", false },
};

struct SvgDashes {
	float64 offset = 0.;
	float64 dash = std::numeric_limits<float64>::infinity();
};

[[nodiscard]] ImageCharges &TotalCharges() {
	static auto result = ImageCharges();
	return result;
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

[[nodiscard]] QFont::Weight FontWeight(int weight) {
	const auto index = (std::clamp(weight, 100, 900) + 50) / 100 - 1;
	return kFontWeights[index];
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

[[nodiscard]] QFont BaseFont(FontKind kind) {
	return (kind == FontKind::Monospace)
		? st::normalFont->monospace()->f
		: st::normalFont->f;
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

[[nodiscard]] QSizeF SvgSize(const QSvgRenderer &renderer) {
	const auto size = QSizeF(renderer.defaultSize());
	return size.isEmpty() ? renderer.viewBoxF().size() : size;
}

[[nodiscard]] std::string LowerLatin(const QByteArray &value) {
	return AsciiLower(std::string_view(value.constData(), value.size()));
}

[[nodiscard]] bool IsDigit(char ch) {
	return (ch >= '0' && ch <= '9');
}

[[nodiscard]] std::size_t SkipSign(std::string_view text, std::size_t i) {
	return (i < text.size() && (text[i] == '-' || text[i] == '+'))
		? (i + 1)
		: i;
}

[[nodiscard]] std::size_t SkipDigits(std::string_view text, std::size_t i) {
	while (i < text.size() && IsDigit(text[i])) {
		++i;
	}
	return i;
}

[[nodiscard]] std::size_t SvgNumberEnd(std::string_view text, std::size_t i) {
	i = SkipDigits(text, SkipSign(text, i));
	if (i < text.size() && text[i] == '.') {
		i = SkipDigits(text, i + 1);
	}
	if (i < text.size() && text[i] == 'e') {
		i = SkipDigits(text, SkipSign(text, i + 1));
	}
	return i;
}

[[nodiscard]] bool AddDashNumbers(
		SvgDashes &dashes,
		std::string_view text,
		bool offset) {
	auto i = text.find_first_of(kSvgNumberStart);
	while (i != std::string_view::npos) {
		const auto till = SvgNumberEnd(text, i);
		const auto number = text.substr(i, till - i);
		if (number.size() > kMaxSvgNumberLength) {
			return false;
		}
		auto ok = false;
		const auto value = QByteArray::fromRawData(
			number.data(),
			int(number.size())).toDouble(&ok);
		if (ok && offset) {
			dashes.offset = std::max(dashes.offset, std::abs(value));
		} else if (ok && value > 0.) {
			dashes.dash = std::min(dashes.dash, value);
		}
		i = text.find_first_of(kSvgNumberStart, till);
	}
	return true;
}

[[nodiscard]] const SvgDashProperty *DashProperty(std::string_view name) {
	for (const auto &property : kSvgDashProperties) {
		if (property.name == name) {
			return &property;
		}
	}
	return nullptr;
}

[[nodiscard]] bool HasDashProperty(std::string_view text) {
	for (const auto &property : kSvgDashProperties) {
		if (text.find(property.name) != std::string_view::npos) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool AddCssDashes(SvgDashes &dashes, std::string_view css) {
	if ((css.find('\\') != std::string_view::npos)
		|| ((css.find(kCssComment) != std::string_view::npos)
			&& HasDashProperty(css))) {
		return false;
	}
	auto from = std::size_t();
	while (from < css.size()) {
		const auto till = std::min(
			css.find_first_of(kSvgDeclarationEnd, from),
			css.size());
		const auto declaration = css.substr(from, till - from);
		const auto colon = declaration.find(':');
		const auto property = (colon != std::string_view::npos)
			? DashProperty(Trimmed(declaration.substr(0, colon)))
			: nullptr;
		if (property) {
			const auto value = declaration.substr(colon + 1);
			if ((value.find(kCssPlusExponent) != std::string_view::npos)
				|| !AddDashNumbers(dashes, value, property->offset)) {
				return false;
			}
		}
		from = till + 1;
	}
	return true;
}

[[nodiscard]] bool AddSvgDashes(
		SvgDashes &dashes,
		std::string_view name,
		std::string_view value) {
	if (name == "style") {
		return AddCssDashes(dashes, value);
	}
	const auto property = DashProperty(name);
	return !property || AddDashNumbers(dashes, value, property->offset);
}

[[nodiscard]] bool SafeForQtSvg(const QByteArray &svg) {
	auto reader = QXmlStreamReader(svg);
	auto dashes = SvgDashes();
	auto styles = 0;
	while (!reader.atEnd()) {
		switch (reader.readNext()) {
		case QXmlStreamReader::StartElement: {
			const auto element = LowerLatin(reader.name().toLatin1());
			if (element == "style") {
				++styles;
			}
			for (const auto &attribute : reader.attributes()) {
				const auto name = LowerLatin(attribute.name().toLatin1());
				const auto value = LowerLatin(attribute.value().toLatin1());
				if ((element == "image" && name == "href")
					|| !AddSvgDashes(dashes, name, value)) {
					return false;
				}
			}
		} break;
		case QXmlStreamReader::EndElement: {
			if (styles > 0
				&& LowerLatin(reader.name().toLatin1()) == "style") {
				--styles;
			}
		} break;
		case QXmlStreamReader::Characters: {
			if (styles > 0
				&& !AddCssDashes(
					dashes,
					LowerLatin(reader.text().toLatin1()))) {
				return false;
			}
		} break;
		default: break;
		}
	}
	return !reader.hasError()
		&& (dashes.offset <= dashes.dash * kMaxSvgDashTurns);
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

[[nodiscard]] DecodedImage ReadSvg(
		const QByteArray &bytes,
		float64 ratio,
		int64 budget) {
	const auto sanitized = Images::SanitizeSvg(bytes);
	if (sanitized.isEmpty() || !SafeForQtSvg(sanitized)) {
		return {};
	}
	auto renderer = QSvgRenderer(sanitized);
	if (!renderer.isValid()) {
		return {};
	}
	const auto size = SvgSize(renderer);
	const auto pixels = QSizeF(
		std::ceil(size.width() * ratio),
		std::ceil(size.height() * ratio));
	if (!ValidImageSize(size)
		|| !ValidImageSize(pixels)
		|| (ImageBytes(pixels) > budget)) {
		return {};
	}
	auto image = QImage(
		QSize(int(pixels.width()), int(pixels.height())),
		QImage::Format_ARGB32_Premultiplied);
	if (!FitsBudget(image, budget)) {
		return {};
	}
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		renderer.render(&p, QRectF(QPointF(), pixels));
	}
	return { std::move(image), size };
}

[[nodiscard]] DecodedImage ReadRaster(
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

} // namespace

QtBackend::QtBackend(float64 ratio)
: _ratio(ratio) {
}

QtBackend::~QtBackend() {
	auto &total = TotalCharges();
	total.bytes -= _imageBytes;
	total.count -= int(_images.size());
}

int QtBackend::create_font(
		const std::string &families,
		float size,
		int weight,
		bool italic,
		twlite::FontMetrics &out) {
	const auto requested = std::isfinite(size)
		? std::clamp(float64(size), 0., kMaxFontSize)
		: 0.;
	const auto pixels = int(std::clamp(
		std::round(requested),
		1.,
		float64(kMaxFontPixelSize)));
	const auto scale = requested / pixels;
	auto font = BaseFont(ResolveFamilies(families));
	font.setPixelSize(pixels);
	font.setWeight(FontWeight(weight));
	font.setItalic(italic);
	const auto metrics = QFontMetricsF(font);
	out.ascent = float(metrics.ascent() * scale);
	out.descent = float(metrics.descent() * scale);
	out.height = float(metrics.lineSpacing() * scale);
	out.x_height = float(metrics.xHeight() * scale);
	out.zero_width = float(metrics.horizontalAdvance(QChar('0')) * scale);
	const auto id = ++_lastFontId;
	_fonts.emplace(id, QtFont{ font, metrics, scale });
	return id;
}

void QtBackend::delete_font(int font) {
	_fonts.remove(font);
}

float QtBackend::text_width(int font, const std::string &text) {
	const auto i = _fonts.find(font);
	if (i == end(_fonts)) {
		return 0.f;
	}
	const auto utf16 = QString::fromUtf8(text.data(), int(text.size()));
	return float(i->second.metrics.horizontalAdvance(utf16)
		* i->second.scale);
}

bool QtBackend::load_image(
		int id,
		const std::string &url,
		float &width,
		float &height) {
	auto &total = TotalCharges();
	if (_images.contains(id)
		|| (int(_images.size()) >= kMaxBackendImages)
		|| (total.count >= kMaxTotalImages)) {
		return false;
	}
	auto data = ParseDataUrl(url);
	if (data.bytes.isEmpty()) {
		return false;
	}
	const auto budget = std::min(
		kMaxBackendImageBytes - _imageBytes,
		kMaxTotalImageBytes - total.bytes);
	auto decoded = (data.format == kSvgFormat)
		? ReadSvg(data.bytes, _ratio, budget)
		: ReadRaster(std::move(data.bytes), data.format, budget);
	if (decoded.image.isNull()) {
		return false;
	}
	const auto bytes = int64(decoded.image.sizeInBytes());
	_imageBytes += bytes;
	total.bytes += bytes;
	++total.count;
	_images.emplace(id, std::move(decoded.image));
	width = float(decoded.size.width());
	height = float(decoded.size.height());
	return true;
}

const QtFont *QtBackend::font(int id) const {
	const auto i = _fonts.find(id);
	return (i != end(_fonts)) ? &i->second : nullptr;
}

const QImage *QtBackend::image(int id) const {
	const auto i = _images.find(id);
	return (i != end(_images)) ? &i->second : nullptr;
}

} // namespace Tgw
