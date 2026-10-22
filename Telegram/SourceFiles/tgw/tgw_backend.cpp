/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_backend.h"

#include "tgw/tgw_canvas.h"
#include "TwidgetCore/src/css.h"
#include "ui/image/svg_safety.h"

#include <QtCore/QBuffer>
#include <QtCore/QXmlStreamReader>
#include <QtCore/QXmlStreamWriter>
#include <QtGui/private/qcssparser_p.h>
#include <QtGui/QImageReader>
#include <QtSvg/QSvgRenderer>

#include "styles/style_basic.h"

namespace Tgw {
namespace {

constexpr auto kMaxFontSize = 1e6;
constexpr auto kMaxFontPixelSize = 1024;
constexpr auto kMaxNewCanvasFonts = 16;
constexpr auto kMaxCanvasFonts = 32;
constexpr auto kMaxCanvasFontParses = 64;
constexpr auto kMaxCanvasFontSpellings = 64;
constexpr auto kMaxCanvasFontSpellingSize = std::size_t(512);
constexpr auto kCanvasFontKind = std::string_view("canvas-font-limit");
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
constexpr auto kMaxSvgAlphaPaints = 4096;
constexpr auto kMaxSvgAlphaUses = 64 * 1024;
constexpr auto kMaxSvgCssRecords = 64 * 1024;
constexpr auto kMaxSvgStopPairs = int64(256 * 1024);
constexpr auto kMaxSvgSelectorWork = int64(1) << 26;
constexpr auto kSvgSelectorStepWork = int64(64);
constexpr auto kSvgPredicateWork = int64(16);
constexpr auto kSvgClassLookupWork = int64(64);
constexpr auto kSvgClassIndexWork = int64(128);
constexpr auto kSvgSplitWork = int64(128);
constexpr auto kMaxSvgSelectorParts = 32;
constexpr auto kMaxSvgRewriteBytes = 8 * 1024 * 1024;

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

enum class CssProperty : uchar {
	Other,
	Fill,
	Stroke,
	StopColor,
	StopOpacity,
};

struct CssPropertyName {
	std::string_view name;
	CssProperty property = CssProperty::Other;
};

constexpr auto kCssProperties = std::array{
	CssPropertyName{ "fill", CssProperty::Fill },
	CssPropertyName{ "stroke", CssProperty::Stroke },
	CssPropertyName{ "stop-color", CssProperty::StopColor },
	CssPropertyName{ "stop-opacity", CssProperty::StopOpacity },
};

enum class CssToken : uchar {
	Word,
	Hash,
	Other,
};

struct CssSpan {
	int from = 0;
	int till = 0;
	CssToken token = CssToken::Other;
};

struct CssDeclaration {
	CssProperty property = CssProperty::Other;
	QString value;
	std::optional<quint32> rgba;
	int from = 0;
	int till = 0;
	int hashFrom = 0;
	int hashTill = 0;
	int rule = -1;
	bool important = false;
};

struct CssRange {
	int from = 0;
	int till = 0;
};

struct CssText {
	QString text;
	std::vector<CssRange> rules;
	std::vector<CssDeclaration> declarations;
	bool cdata = false;
	bool valid = true;
};

struct SvgScan {
	QSet<QString> names;
	std::vector<quint32> colors;
	std::unordered_map<quint32, int> indices;
	std::vector<CssText> styles;
	int uses = 0;
	int records = 0;
	int stops = 0;
	bool stopAlpha = false;
	bool failed = false;
};

struct SvgElement {
	QString name;
	QString id;
	QString classes;
	QSet<QString> classSet;
	int parent = -1;
	int previous = -1;
	bool indexed = false;
};

struct SvgOpenElement {
	int index = -1;
	int lastChild = -1;
};

struct StopCandidate {
	const CssDeclaration *declaration = nullptr;
	std::optional<float64> opacity;
	int order = 0;
};

struct StopRule {
	QCss::StyleRule parsed;
	std::vector<StopCandidate> candidates;
};

using StopRank = std::tuple<int, int, int>;

struct StopValue {
	StopRank rank = { -1, 0, 0 };
	QString text;
	std::optional<quint32> rgba;
	float64 opacity = 1.;
};

struct StopStyle {
	StopValue color;
	StopValue opacity;
};

struct StopInput {
	std::optional<QString> color;
	std::optional<QString> opacity;
	CssText style;
};

class StopCascade final {
public:
	explicit StopCascade(std::vector<SvgElement> &elements);

	void add(const CssText &css, int &order);
	[[nodiscard]] int64 weight() const;
	[[nodiscard]] bool spent() const;
	[[nodiscard]] StopStyle resolve(int element, const StopInput &own);

private:
	[[nodiscard]] bool addRule(const CssText &css, int rule);
	[[nodiscard]] int specificity(const QCss::StyleRule &rule, int element);
	[[nodiscard]] bool matches(
		const QCss::Selector &selector,
		int part,
		int element);
	[[nodiscard]] bool matches(
		const QCss::BasicSelector &selector,
		SvgElement &element);
	[[nodiscard]] bool matches(
		const QCss::AttributeSelector &selector,
		SvgElement &element);
	[[nodiscard]] bool hasClass(SvgElement &element, const QString &name);
	[[nodiscard]] bool charge(int64 work);

	std::vector<SvgElement> &_elements;
	std::vector<StopRule> _rules;
	int64 _weight = 0;
	int64 _work = 0;

};

class SvgRewriter final {
public:
	SvgRewriter(const QByteArray &svg, const SvgScan &scan);

	[[nodiscard]] QByteArray run();

private:
	void token();
	void start();
	void characters();
	void end();
	void addElement();
	void writeAttributes(bool stop);
	void writePaints();
	void writeStop(const StopInput &own);
	[[nodiscard]] bool fits(int64 size);
	[[nodiscard]] QString rewritten(const CssText &css, bool stop);
	[[nodiscard]] QString paintUrl(quint32 rgba);
	[[nodiscard]] QString prefixed(const QString &name) const;

	const SvgScan &_scan;
	QXmlStreamReader _reader;
	QByteArray _result;
	QXmlStreamWriter _writer;
	std::vector<QString> _ids;
	std::vector<QString> _sheets;
	std::vector<SvgElement> _elements;
	std::vector<SvgOpenElement> _open;
	StopCascade _cascade;
	QString _prefix;
	int _style = -1;
	int _styles = 0;
	bool _styleWritten = false;
	bool _started = false;
	bool _failed = false;

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

[[nodiscard]] QFont BaseFont(FontKind kind) {
	return (kind == FontKind::Monospace)
		? st::normalFont->monospace()->f
		: st::normalFont->f;
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

[[nodiscard]] QFont StyledFont(uint32 style, int pixels) {
	auto result = BaseFont(FontKind(style & 1));
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

[[nodiscard]] int HexNibble(QChar ch) {
	const auto code = ch.unicode();
	if (code >= '0' && code <= '9') {
		return int(code - '0');
	} else if (code >= 'a' && code <= 'f') {
		return int(code - 'a') + 10;
	} else if (code >= 'A' && code <= 'F') {
		return int(code - 'A') + 10;
	}
	return -1;
}

[[nodiscard]] std::optional<quint32> AlphaHex(const QString &value) {
	const auto digits = int(value.size()) - 1;
	if ((digits != 4 && digits != 8) || (value[0] != QChar('#'))) {
		return std::nullopt;
	}
	const auto repeat = (digits == 4) ? 2 : 1;
	auto result = quint32();
	for (auto i = 1; i <= digits; ++i) {
		const auto nibble = HexNibble(value[i]);
		if (nibble < 0) {
			return std::nullopt;
		}
		for (auto j = 0; j != repeat; ++j) {
			result = (result << 4) | quint32(nibble);
		}
	}
	return result;
}

[[nodiscard]] QString RgbText(quint32 rgba) {
	return u"#%1"_q.arg(rgba >> 8, 6, 16, QChar('0'));
}

[[nodiscard]] QString OpacityText(float64 opacity) {
	return QString::number(opacity, 'g', 6);
}

[[nodiscard]] std::optional<float64> ParseOpacity(QString value) {
	const auto percent = value.endsWith(QChar('%'));
	if (percent) {
		value.chop(1);
	}
	auto ok = false;
	const auto result = value.toDouble(&ok);
	if (!ok || !std::isfinite(result)) {
		return std::nullopt;
	}
	return std::clamp(percent ? (result / 100.) : result, 0., 1.);
}

[[nodiscard]] CssProperty PaintProperty(const QString &name) {
	for (const auto &entry : kCssProperties) {
		if (name == QLatin1String(entry.name.data(), int(entry.name.size()))) {
			return entry.property;
		}
	}
	return CssProperty::Other;
}

[[nodiscard]] bool IsPaintProperty(CssProperty property) {
	return (property == CssProperty::Fill)
		|| (property == CssProperty::Stroke);
}

[[nodiscard]] bool IsStopProperty(CssProperty property) {
	return (property == CssProperty::StopColor)
		|| (property == CssProperty::StopOpacity);
}

[[nodiscard]] bool IsCssBlank(QChar ch) {
	const auto code = ch.unicode();
	return (code == ' ')
		|| (code == '\t')
		|| (code == '\n')
		|| (code == '\r')
		|| (code == '\f');
}

[[nodiscard]] bool IsCssNameChar(QChar ch) {
	const auto code = ch.unicode();
	return (code >= 0x80)
		|| (code >= '0' && code <= '9')
		|| (code >= 'a' && code <= 'z')
		|| (code >= 'A' && code <= 'Z')
		|| (code == '-')
		|| (code == '_');
}

[[nodiscard]] bool IsCssWordChar(QChar ch) {
	return IsCssNameChar(ch) || (ch == QChar('.')) || (ch == QChar('%'));
}

[[nodiscard]] int CssOpaqueEnd(const QString &text, int i) {
	const auto size = int(text.size());
	const auto ch = text[i];
	if (ch == QChar('"') || ch == QChar('\'')) {
		for (auto j = i + 1; j < size; ++j) {
			if (text[j] == ch) {
				return j + 1;
			} else if (text[j] == QChar('\n')
				|| text[j] == QChar('\r')
				|| text[j] == QChar('\f')) {
				return -1;
			}
		}
		return -1;
	} else if (ch == QChar('/')
		&& (i + 1 < size)
		&& (text[i + 1] == QChar('*'))) {
		const auto end = int(text.indexOf(u"*/"_q, i + 2));
		return (end < 0) ? -1 : (end + 2);
	}
	return i;
}

[[nodiscard]] int FindCssChar(
		const QString &text,
		int from,
		int till,
		std::string_view stops) {
	auto depth = 0;
	auto i = from;
	while (i < till) {
		const auto next = CssOpaqueEnd(text, i);
		if (next < 0) {
			return -1;
		} else if (next > i) {
			i = next;
			continue;
		}
		const auto code = text[i].unicode();
		if (!depth
			&& (code < 0x80)
			&& (stops.find(char(code)) != std::string_view::npos)) {
			return i;
		} else if (code == '(' || code == '[' || code == '{') {
			++depth;
		} else if ((code == ')' || code == ']' || code == '}') && depth > 0) {
			--depth;
		}
		++i;
	}
	return till;
}

[[nodiscard]] std::optional<std::vector<CssSpan>> CssTokens(
		const QString &text,
		int from,
		int till) {
	auto result = std::vector<CssSpan>();
	auto i = from;
	while (i < till) {
		const auto ch = text[i];
		const auto next = CssOpaqueEnd(text, i);
		if (next < 0) {
			return std::nullopt;
		} else if (next > i) {
			if (ch != QChar('/')) {
				result.push_back({ i, next, CssToken::Other });
			}
			i = next;
		} else if (IsCssBlank(ch)) {
			++i;
		} else if (ch == QChar('(') || ch == QChar('[')) {
			const auto close = FindCssChar(
				text,
				i + 1,
				till,
				(ch == QChar('(')) ? ")" : "]");
			if (close < 0) {
				return std::nullopt;
			}
			const auto end = std::min(close + 1, till);
			result.push_back({ i, end, CssToken::Other });
			i = end;
		} else {
			const auto hash = (ch == QChar('#'));
			const auto start = i + (hash ? 1 : 0);
			auto j = start;
			while (j < till
				&& (hash ? IsCssNameChar(text[j]) : IsCssWordChar(text[j]))) {
				++j;
			}
			const auto token = (j == start)
				? CssToken::Other
				: hash
				? CssToken::Hash
				: CssToken::Word;
			const auto end = (j == start) ? (i + 1) : j;
			result.push_back({ i, end, token });
			i = end;
		}
	}
	return result;
}

[[nodiscard]] QString CssPlain(const QString &text, int from, int till) {
	auto result = QString();
	auto i = from;
	while (i < till) {
		const auto next = CssOpaqueEnd(text, i);
		if (next > i && text[i] == QChar('/')) {
			result.append(QChar(' '));
			i = next;
		} else {
			const auto end = std::max(next, i + 1);
			result.append(text.mid(i, end - i));
			i = end;
		}
	}
	return result.trimmed();
}

[[nodiscard]] int64 EscapedSize(const QString &text, bool attribute) {
	auto result = int64(text.size());
	for (const auto &ch : text) {
		switch (ch.unicode()) {
		case '<':
		case '>': result += 3; break;
		case '&': result += 4; break;
		case '"': result += 5; break;
		case '\t': result += attribute ? 3 : 0; break;
		case '\n':
		case '\r': result += attribute ? 4 : 0; break;
		default: break;
		}
	}
	return result;
}

[[nodiscard]] bool IsCssImportant(
		const QString &text,
		const std::vector<CssSpan> &spans) {
	const auto count = int(spans.size());
	if (count < 2) {
		return false;
	}
	const auto &mark = spans[count - 2];
	const auto &word = spans[count - 1];
	return (mark.token == CssToken::Other)
		&& (text[mark.from] == QChar('!'))
		&& (word.token == CssToken::Word)
		&& (text.mid(word.from, word.till - word.from).compare(
			u"important"_q,
			Qt::CaseInsensitive) == 0);
}

void AddCssDeclaration(CssText &css, int from, int till, int rule) {
	const auto &text = css.text;
	const auto colon = FindCssChar(text, from, till, ":");
	if (colon < 0) {
		css.valid = false;
		return;
	} else if (colon == till) {
		return;
	}
	const auto name = CssTokens(text, from, colon);
	auto value = CssTokens(text, colon + 1, till);
	if (!name || !value) {
		css.valid = false;
		return;
	} else if (name->size() != 1 || name->front().token != CssToken::Word) {
		return;
	}
	const auto property = PaintProperty(text.mid(
		name->front().from,
		name->front().till - name->front().from));
	auto &spans = *value;
	const auto important = IsCssImportant(text, spans);
	if (important) {
		spans.resize(spans.size() - 2);
	}
	if (property == CssProperty::Other || spans.empty()) {
		return;
	}
	const auto semicolon = (till < int(text.size()))
		&& (text[till] == QChar(';'));
	auto declaration = CssDeclaration{
		.property = property,
		.value = CssPlain(text, spans.front().from, spans.back().till),
		.from = from,
		.till = till + (semicolon ? 1 : 0),
		.rule = rule,
		.important = important,
	};
	if (spans.size() == 1 && spans.front().token == CssToken::Hash) {
		declaration.rgba = AlphaHex(declaration.value);
		declaration.hashFrom = spans.front().from;
		declaration.hashTill = spans.front().till;
	}
	if (IsStopProperty(property) || declaration.rgba) {
		css.declarations.push_back(std::move(declaration));
	}
}

void ParseCssList(CssText &css, int from, int till, int rule) {
	auto i = from;
	while (i < till && css.valid) {
		const auto end = FindCssChar(css.text, i, till, ";");
		if (end < 0) {
			css.valid = false;
			return;
		}
		AddCssDeclaration(css, i, end, rule);
		i = end + 1;
	}
}

void ParseCssSheet(CssText &css) {
	const auto &text = css.text;
	const auto size = int(text.size());
	auto i = 0;
	while (i < size && css.valid) {
		const auto open = FindCssChar(text, i, size, "{;");
		if (open < 0) {
			css.valid = false;
			return;
		} else if (open == size) {
			return;
		} else if (text[open] == QChar(';')) {
			i = open + 1;
			continue;
		}
		const auto close = FindCssChar(text, open + 1, size, "}");
		const auto prelude = (close < 0)
			? std::nullopt
			: CssTokens(text, i, open);
		if (!prelude) {
			css.valid = false;
			return;
		} else if (!prelude->empty()
			&& text[prelude->front().from] != QChar('@')) {
			css.rules.push_back({
				prelude->front().from,
				prelude->back().till,
			});
			ParseCssList(css, open + 1, close, int(css.rules.size()) - 1);
		}
		i = close + 1;
	}
}

[[nodiscard]] bool IsCssStyle(const QXmlStreamReader &reader) {
	if (reader.name() != QLatin1String("style")) {
		return false;
	}
	const auto attributes = reader.attributes();
	const auto type = attributes.value(QLatin1String("type"));
	return type.isNull()
		|| (type.compare(QLatin1String("text/css"), Qt::CaseInsensitive) == 0);
}

[[nodiscard]] QString SvgId(const QXmlStreamAttributes &attributes) {
	const auto id = attributes.value(QLatin1String("id"));
	const auto result = id.isEmpty()
		? attributes.value(QLatin1String("xml:id"))
		: id;
	return result.isEmpty() ? QString() : result.toString();
}

[[nodiscard]] bool HasDashPrefix(const QString &value, const QString &prefix) {
	const auto size = int(prefix.size());
	return (int(value.size()) > size)
		&& (value[size] == QChar('-'))
		&& value.startsWith(prefix);
}

[[nodiscard]] std::optional<float64> OpacityOf(
		CssProperty property,
		const QString &value) {
	return (property == CssProperty::StopOpacity)
		? ParseOpacity(value)
		: std::nullopt;
}

void OfferStop(
		StopStyle &style,
		CssProperty property,
		const QString &value,
		std::optional<quint32> rgba,
		std::optional<float64> opacity,
		StopRank rank) {
	const auto color = (property == CssProperty::StopColor);
	auto &target = color ? style.color : style.opacity;
	if (rank <= target.rank || (!color && !opacity)) {
		return;
	} else if (!color) {
		target.opacity = *opacity;
	}
	target.rank = rank;
	target.text = value;
	target.rgba = color ? rgba : std::nullopt;
}

[[nodiscard]] QString PaintId(int index) {
	return u"tgw%1"_q.arg(index);
}

[[nodiscard]] bool MayBePaintId(const QString &name) {
	if (name.size() <= 3 || !name.startsWith(u"tgw"_q)) {
		return false;
	}
	for (auto i = 3; i != int(name.size()); ++i) {
		if (name[i] < QChar('0') || name[i] > QChar('9')) {
			return false;
		}
	}
	return true;
}

void CollectNames(QSet<QString> &names, const QString &text) {
	const auto size = int(text.size());
	auto i = int(text.indexOf(QChar('#')));
	while (i >= 0) {
		auto j = i + 1;
		while (j < size && IsCssNameChar(text[j])) {
			++j;
		}
		const auto name = text.mid(i + 1, j - i - 1);
		if (MayBePaintId(name)) {
			names.insert(name);
		}
		i = int(text.indexOf(QChar('#'), j));
	}
}

void AddPaint(SvgScan &scan, quint32 rgba) {
	if (++scan.uses > kMaxSvgAlphaUses) {
		scan.failed = true;
	} else if (!scan.indices.contains(rgba)) {
		if (int(scan.colors.size()) >= kMaxSvgAlphaPaints) {
			scan.failed = true;
			return;
		}
		scan.indices.emplace(rgba, int(scan.colors.size()));
		scan.colors.push_back(rgba);
	}
}

void ScanDeclarations(SvgScan &scan, const CssText &css, bool stop) {
	if (!css.valid) {
		scan.failed = true;
		return;
	}
	for (const auto &declaration : css.declarations) {
		if (!declaration.rgba) {
			continue;
		} else if (IsPaintProperty(declaration.property)) {
			AddPaint(scan, *declaration.rgba);
		} else if (stop && declaration.property == CssProperty::StopColor) {
			scan.stopAlpha = true;
		}
	}
}

void ScanElement(SvgScan &scan, const QXmlStreamReader &reader) {
	const auto stop = (reader.name() == QLatin1String("stop"));
	if (stop) {
		++scan.stops;
	}
	for (const auto &attribute : reader.attributes()) {
		const auto name = attribute.qualifiedName().toString();
		const auto value = attribute.value().toString();
		if ((name == u"id"_q || name == u"xml:id"_q) && MayBePaintId(value)) {
			scan.names.insert(value);
		}
		CollectNames(scan.names, value);
		if (name == u"style"_q) {
			auto css = CssText{ .text = value };
			ParseCssList(css, 0, int(value.size()), -1);
			ScanDeclarations(scan, css, stop);
			continue;
		}
		const auto property = PaintProperty(name);
		const auto rgba = (property != CssProperty::Other)
			? AlphaHex(value.trimmed())
			: std::nullopt;
		if (!rgba) {
			continue;
		} else if (IsPaintProperty(property)) {
			AddPaint(scan, *rgba);
		} else if (stop && property == CssProperty::StopColor) {
			scan.stopAlpha = true;
		}
	}
}

[[nodiscard]] SvgScan ScanSvg(const QByteArray &svg) {
	auto result = SvgScan();
	auto reader = QXmlStreamReader(svg);
	reader.setNamespaceProcessing(false);
	auto style = -1;
	while (!reader.atEnd() && !result.failed) {
		switch (reader.readNext()) {
		case QXmlStreamReader::StartElement: {
			if (style >= 0) {
				result.failed = true;
				break;
			}
			ScanElement(result, reader);
			if (IsCssStyle(reader)) {
				style = int(result.styles.size());
				result.styles.emplace_back();
			}
		} break;
		case QXmlStreamReader::Characters: {
			if (style >= 0) {
				auto &css = result.styles[style];
				css.text.append(reader.text().toString());
				css.cdata = css.cdata || reader.isCDATA();
			}
		} break;
		case QXmlStreamReader::EndElement: {
			if (style >= 0) {
				auto &css = result.styles[style];
				CollectNames(result.names, css.text);
				ParseCssSheet(css);
				ScanDeclarations(result, css, true);
				result.records += int(css.declarations.size());
				result.failed = result.failed
					|| (result.records > kMaxSvgCssRecords);
				style = -1;
			}
		} break;
		case QXmlStreamReader::EntityReference:
		case QXmlStreamReader::Invalid: result.failed = true; break;
		default: break;
		}
	}
	result.failed = result.failed || reader.hasError();
	return result;
}

StopCascade::StopCascade(std::vector<SvgElement> &elements)
: _elements(elements) {
}

void StopCascade::add(const CssText &css, int &order) {
	auto rule = -1;
	auto added = false;
	for (const auto &declaration : css.declarations) {
		++order;
		if (!IsStopProperty(declaration.property)) {
			continue;
		} else if (declaration.rule != rule) {
			rule = declaration.rule;
			added = addRule(css, rule);
		}
		if (added) {
			_rules.back().candidates.push_back({
				&declaration,
				OpacityOf(declaration.property, declaration.value),
				order,
			});
			++_weight;
		}
	}
}

bool StopCascade::addRule(const CssText &css, int rule) {
	const auto &range = css.rules[rule];
	auto parser = QCss::Parser(
		css.text.mid(range.from, range.till - range.from) + u"{}"_q);
	while (parser.test(QCss::S)
		|| parser.test(QCss::CDO)
		|| parser.test(QCss::CDC)) {
	}
	auto parsed = QCss::StyleRule();
	if (!parser.testRuleset()
		|| !parser.parseRuleset(&parsed)
		|| parser.hasNext()) {
		return false;
	}
	_weight += int64(parsed.selectors.size());
	_rules.push_back({ std::move(parsed) });
	return true;
}

int64 StopCascade::weight() const {
	return _weight;
}

bool StopCascade::spent() const {
	return (_work > kMaxSvgSelectorWork);
}

StopStyle StopCascade::resolve(int element, const StopInput &own) {
	auto result = StopStyle();
	const auto offer = [&](
			CssProperty property,
			const std::optional<QString> &value) {
		if (value) {
			const auto text = value->trimmed();
			OfferStop(
				result,
				property,
				text,
				AlphaHex(text),
				OpacityOf(property, text),
				{ 0, 0, 0 });
		}
	};
	offer(CssProperty::StopColor, own.color);
	offer(CssProperty::StopOpacity, own.opacity);
	for (const auto &rule : _rules) {
		const auto weight = specificity(rule.parsed, element);
		if (spent()) {
			break;
		} else if (weight < 0) {
			continue;
		}
		for (const auto &candidate : rule.candidates) {
			const auto &declaration = *candidate.declaration;
			OfferStop(
				result,
				declaration.property,
				declaration.value,
				declaration.rgba,
				candidate.opacity,
				{
					declaration.important ? 3 : 1,
					weight,
					candidate.order,
				});
		}
	}
	auto order = 0;
	for (const auto &declaration : own.style.declarations) {
		if (IsStopProperty(declaration.property)) {
			OfferStop(
				result,
				declaration.property,
				declaration.value,
				declaration.rgba,
				OpacityOf(declaration.property, declaration.value),
				{ declaration.important ? 4 : 2, 0, order });
		}
		++order;
	}
	return result;
}

int StopCascade::specificity(const QCss::StyleRule &rule, int element) {
	auto result = -1;
	for (const auto &selector : rule.selectors) {
		const auto parts = int(selector.basicSelectors.size());
		if (parts > 0
			&& parts <= kMaxSvgSelectorParts
			&& (selector.basicSelectors.back().relationToNext
				== QCss::BasicSelector::NoRelation)
			&& matches(selector, parts - 1, element)) {
			result = std::max(result, selector.specificity());
		}
	}
	return result;
}

bool StopCascade::matches(
		const QCss::Selector &selector,
		int part,
		int element) {
	if (element < 0 || !charge(kSvgSelectorStepWork)) {
		return false;
	}
	auto &node = _elements[element];
	if (!matches(selector.basicSelectors[part], node)) {
		return false;
	} else if (!part) {
		return true;
	}
	using Relation = QCss::BasicSelector::Relation;
	const auto relation = selector.basicSelectors[part - 1].relationToNext;
	const auto ancestors = (relation == Relation::MatchNextSelectorIfAncestor);
	const auto siblings = (relation
		== Relation::MatchNextSelectorIfIndirectAdjecent);
	if (relation == Relation::MatchNextSelectorIfParent) {
		return matches(selector, part - 1, node.parent);
	} else if (relation == Relation::MatchNextSelectorIfDirectAdjecent) {
		return matches(selector, part - 1, node.previous);
	} else if (!ancestors && !siblings) {
		return false;
	}
	auto next = ancestors ? node.parent : node.previous;
	while (next >= 0 && !spent()) {
		if (matches(selector, part - 1, next)) {
			return true;
		}
		next = ancestors ? _elements[next].parent : _elements[next].previous;
	}
	return false;
}

bool StopCascade::matches(
		const QCss::BasicSelector &selector,
		SvgElement &element) {
	const auto &name = selector.elementName;
	if (!selector.pseudos.isEmpty()
		|| (!name.isEmpty()
			&& (!charge(kSvgPredicateWork + name.size())
				|| (name != element.name)))) {
		return false;
	}
	for (const auto &id : selector.ids) {
		if (!charge(kSvgPredicateWork + id.size()) || (id != element.id)) {
			return false;
		}
	}
	for (const auto &attribute : selector.attributeSelectors) {
		if (!matches(attribute, element)) {
			return false;
		}
	}
	return true;
}

bool StopCascade::matches(
		const QCss::AttributeSelector &selector,
		SvgElement &element) {
	const auto classes = (selector.name == QLatin1String("class"));
	const auto value = classes
		? element.classes
		: (selector.name == QLatin1String("id"))
		? element.id
		: QString();
	const auto &wanted = selector.value;
	const auto length = int64(wanted.size()) + 1;
	if (value.isNull() || !charge(kSvgPredicateWork)) {
		return false;
	}
	switch (selector.valueMatchCriterium) {
	case QCss::AttributeSelector::NoMatch: return true;
	case QCss::AttributeSelector::MatchEqual:
		return charge(length) && (value == wanted);
	case QCss::AttributeSelector::MatchIncludes:
		return classes
			? hasClass(element, wanted)
			: (charge(kSvgSplitWork * (int64(value.size()) + 1))
				&& value.split(QChar(' ')).contains(wanted));
	case QCss::AttributeSelector::MatchDashMatch:
		return charge(length)
			&& ((value == wanted) || HasDashPrefix(value, wanted));
	case QCss::AttributeSelector::MatchBeginsWith:
		return charge(length) && value.startsWith(wanted);
	case QCss::AttributeSelector::MatchEndsWith:
		return charge(length) && value.endsWith(wanted);
	case QCss::AttributeSelector::MatchContains:
		return charge((int64(value.size()) + 1) * length)
			&& value.contains(wanted);
	}
	return false;
}

bool StopCascade::hasClass(SvgElement &element, const QString &name) {
	if (!element.indexed) {
		element.indexed = true;
		const auto size = int64(element.classes.size()) + 1;
		if (!charge(kSvgClassIndexWork * size)) {
			return false;
		}
		const auto list = element.classes.split(
			QChar(' '),
			Qt::SkipEmptyParts);
		element.classSet = QSet<QString>(list.begin(), list.end());
	}
	return charge(kSvgClassLookupWork + name.size())
		&& element.classSet.contains(name);
}

bool StopCascade::charge(int64 work) {
	if (_work <= kMaxSvgSelectorWork) {
		_work += work;
	}
	return (_work <= kMaxSvgSelectorWork);
}

SvgRewriter::SvgRewriter(const QByteArray &svg, const SvgScan &scan)
: _scan(scan)
, _reader(svg)
, _writer(&_result)
, _cascade(_elements) {
	_reader.setNamespaceProcessing(false);
	_writer.setAutoFormatting(false);
	auto next = 0;
	for (auto i = 0; i != int(scan.colors.size()); ++i) {
		auto id = PaintId(next++);
		while (scan.names.contains(id)) {
			id = PaintId(next++);
		}
		_ids.push_back(std::move(id));
	}
	auto order = 0;
	for (const auto &css : scan.styles) {
		auto text = rewritten(css, scan.stopAlpha);
		_sheets.push_back((text != css.text) ? std::move(text) : QString());
		if (scan.stopAlpha) {
			_cascade.add(css, order);
		}
	}
	_failed = _failed
		|| (int64(scan.stops) * _cascade.weight() > kMaxSvgStopPairs);
}

QByteArray SvgRewriter::run() {
	while (!_failed && !_reader.atEnd()) {
		token();
		_failed = _failed || (_result.size() > kMaxSvgRewriteBytes);
	}
	return (_failed || _reader.hasError() || _writer.hasError())
		? QByteArray()
		: _result;
}

void SvgRewriter::token() {
	switch (_reader.readNext()) {
	case QXmlStreamReader::StartDocument: {
		const auto version = _reader.documentVersion().toString();
		if (!version.isEmpty() && _reader.isStandaloneDocument()) {
			_writer.writeStartDocument(version, true);
		} else if (!version.isEmpty()) {
			_writer.writeStartDocument(version);
		}
	} break;
	case QXmlStreamReader::EndDocument: _writer.writeEndDocument(); break;
	case QXmlStreamReader::DTD: {
		const auto text = _reader.text().toString();
		if (fits(text.size())) {
			_writer.writeDTD(text);
		}
	} break;
	case QXmlStreamReader::StartElement: start(); break;
	case QXmlStreamReader::EndElement: end(); break;
	case QXmlStreamReader::Characters: characters(); break;
	case QXmlStreamReader::Comment: {
		const auto text = _reader.text().toString();
		_failed = text.contains(u"--"_q) || text.endsWith(QChar('-'));
		if (fits(text.size())) {
			_writer.writeComment(text);
		}
	} break;
	case QXmlStreamReader::ProcessingInstruction: {
		const auto target = _reader.processingInstructionTarget().toString();
		const auto data = _reader.processingInstructionData().toString();
		_failed = data.contains(u"?>"_q);
		if (fits(target.size() + data.size())) {
			_writer.writeProcessingInstruction(target, data);
		}
	} break;
	default: _failed = true; break;
	}
}

void SvgRewriter::start() {
	const auto stop = _scan.stopAlpha
		&& (_reader.name() == QLatin1String("stop"));
	if (_scan.stopAlpha) {
		addElement();
	}
	_writer.writeStartElement(_reader.qualifiedName().toString());
	writeAttributes(stop);
	if (!_started) {
		_started = true;
		_prefix = _reader.prefix().toString();
		writePaints();
	}
	if (IsCssStyle(_reader)) {
		_style = _styles++;
		_styleWritten = false;
	}
}

void SvgRewriter::characters() {
	const auto sheet = (_style >= 0) ? _sheets[_style] : QString();
	if (!sheet.isNull() && _styleWritten) {
		return;
	}
	_styleWritten = !sheet.isNull();
	const auto text = sheet.isNull() ? _reader.text().toString() : sheet;
	const auto cdata = sheet.isNull()
		? _reader.isCDATA()
		: (_scan.styles[_style].cdata && !sheet.contains(u"]]>"_q));
	if (!fits(cdata ? text.size() : EscapedSize(text, false))) {
		return;
	} else if (cdata) {
		_writer.writeCDATA(text);
	} else {
		_writer.writeCharacters(text);
	}
}

void SvgRewriter::end() {
	_style = -1;
	if (!_open.empty()) {
		_open.pop_back();
	}
	_writer.writeEndElement();
}

void SvgRewriter::addElement() {
	const auto attributes = _reader.attributes();
	const auto classes = attributes.value(QLatin1String("class"));
	const auto index = int(_elements.size());
	auto element = SvgElement{
		.name = _reader.name().toString(),
		.id = SvgId(attributes),
		.classes = classes.isNull()
			? QString()
			: classes.toString().simplified(),
	};
	if (!_open.empty()) {
		element.parent = _open.back().index;
		element.previous = _open.back().lastChild;
		_open.back().lastChild = index;
	}
	_elements.push_back(std::move(element));
	_open.push_back({ index, -1 });
}

void SvgRewriter::writeAttributes(bool stop) {
	auto own = StopInput();
	for (const auto &attribute : _reader.attributes()) {
		const auto name = attribute.qualifiedName().toString();
		auto value = attribute.value().toString();
		const auto property = PaintProperty(name);
		if (stop && IsStopProperty(property)) {
			((property == CssProperty::StopColor)
				? own.color
				: own.opacity) = value;
			continue;
		} else if (name == u"style"_q) {
			own.style = CssText{ .text = value };
			ParseCssList(own.style, 0, int(value.size()), -1);
			value = rewritten(own.style, stop);
		} else if (IsPaintProperty(property)) {
			if (const auto rgba = AlphaHex(value.trimmed())) {
				value = paintUrl(*rgba);
			}
		}
		if (!fits(EscapedSize(value, true) + name.size())) {
			return;
		}
		_writer.writeAttribute(name, value);
	}
	if (stop) {
		writeStop(own);
	}
}

void SvgRewriter::writePaints() {
	if (_ids.empty()) {
		return;
	}
	_writer.writeStartElement(prefixed(u"defs"_q));
	for (auto i = 0; i != int(_ids.size()); ++i) {
		const auto rgba = _scan.colors[i];
		_writer.writeEmptyElement(prefixed(u"solidColor"_q));
		_writer.writeAttribute(u"id"_q, _ids[i]);
		_writer.writeAttribute(u"solid-color"_q, RgbText(rgba));
		_writer.writeAttribute(
			u"solid-opacity"_q,
			OpacityText((rgba & 0xFFU) / 255.));
	}
	_writer.writeEndElement();
}

void SvgRewriter::writeStop(const StopInput &own) {
	const auto style = _cascade.resolve(int(_elements.size()) - 1, own);
	_failed = _failed || _cascade.spent();
	const auto &color = style.color;
	const auto alpha = color.rgba ? ((*color.rgba & 0xFFU) / 255.) : 1.;
	if (!color.text.isEmpty()) {
		_writer.writeAttribute(
			u"stop-color"_q,
			color.rgba ? RgbText(*color.rgba) : color.text);
	}
	if (color.rgba || (std::get<0>(style.opacity.rank) >= 0)) {
		_writer.writeAttribute(
			u"stop-opacity"_q,
			OpacityText(alpha * style.opacity.opacity));
	}
}

bool SvgRewriter::fits(int64 size) {
	_failed = _failed
		|| (int64(_result.size()) + size > kMaxSvgRewriteBytes);
	return !_failed;
}

QString SvgRewriter::rewritten(const CssText &css, bool stop) {
	auto result = QString();
	auto from = 0;
	for (const auto &declaration : css.declarations) {
		const auto remove = stop && IsStopProperty(declaration.property);
		const auto paint = IsPaintProperty(declaration.property)
			&& declaration.rgba.has_value();
		if (!remove && !paint) {
			continue;
		}
		const auto till = remove ? declaration.from : declaration.hashFrom;
		result.append(css.text.mid(from, till - from));
		if (paint) {
			result.append(paintUrl(*declaration.rgba));
		}
		from = remove ? declaration.till : declaration.hashTill;
	}
	result.append(css.text.mid(from));
	return result;
}

QString SvgRewriter::paintUrl(quint32 rgba) {
	const auto i = _scan.indices.find(rgba);
	if (i == _scan.indices.end()) {
		_failed = true;
		return QString();
	}
	return u"url(#%1)"_q.arg(_ids[i->second]);
}

QString SvgRewriter::prefixed(const QString &name) const {
	return _prefix.isEmpty() ? name : (_prefix + QChar(':') + name);
}

// WHY: QtSvg 5 reads no alpha hex colour. A solidColor paint server keeps
// the alpha with the inherited paint and multiplies it by fill or stroke
// opacity, while an inherited opacity would also fade repainted children.
[[nodiscard]] QByteArray SvgWithAlphaPaints(const QByteArray &svg) {
	const auto scan = ScanSvg(svg);
	if (scan.failed || (scan.colors.empty() && !scan.stopAlpha)) {
		return svg;
	}
	const auto result = SvgRewriter(svg, scan).run();
	return (!result.isEmpty()
		&& !Images::SanitizeSvg(result).isEmpty()
		&& SafeForQtSvg(result))
		? result
		: svg;
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
	auto renderer = QSvgRenderer(SvgWithAlphaPaints(sanitized));
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

QtBackend::QtBackend(float64 ratio, Diagnostic diagnostic)
: _ratio(ratio)
, _diagnostic(diagnostic)
, _canvases(std::make_unique<Canvases>(std::move(diagnostic)))
, _canvasFontsLeft(kMaxNewCanvasFonts)
, _canvasParsesLeft(kMaxCanvasFontParses) {
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
		const std::string&,
		twlite::FontMetrics &out) {
	const auto key = ResolveFont(families, size, weight, italic);
	return _canvasText ? admitCanvasFont(key, true, out) : makeFont(key, out);
}

int QtBackend::makeFont(FontKey key, twlite::FontMetrics &out) {
	const auto requested = key.second;
	const auto pixels = int(std::clamp(
		std::round(requested),
		1.,
		float64(kMaxFontPixelSize)));
	const auto scale = requested / pixels;
	const auto font = StyledFont(key.first, pixels);
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
	for (const auto &entry : _canvasFonts) {
		if (entry.second.id == font) {
			return;
		}
	}
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
	_canvases->run(id, width, height, ops, *this);
}

bool QtBackend::canvas_read(
		int id,
		int x,
		int y,
		int width,
		int height,
		std::vector<uint8_t> &rgba) {
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

void QtBackend::beginCanvasBatch() {
	_canvases->refill();
	_canvasFontsLeft = kMaxNewCanvasFonts;
	_canvasParsesLeft = kMaxCanvasFontParses;
}

int QtBackend::canvasFont(
		const std::string &css,
		twlite::FontMetrics &metrics) {
	if (css.size() > kMaxCanvasFontSpellingSize) {
		return refuseCanvasFont(metrics);
	}
	const auto i = _canvasSpellings.find(css);
	if (i != end(_canvasSpellings)) {
		return admitCanvasFont(i->second, false, metrics);
	} else if (_canvasParsesLeft <= 0) {
		return refuseCanvasFont(metrics);
	}
	--_canvasParsesLeft;
	const auto parsed = twlite::css::parse_font(css);
	const auto key = ResolveFont(
		parsed.family,
		parsed.size,
		parsed.weight,
		parsed.italic);
	if (int(_canvasSpellings.size()) >= kMaxCanvasFontSpellings) {
		// Holds no handles, so clearing it mid-message is safe.
		_canvasSpellings.clear();
	}
	_canvasSpellings.emplace(css, key);
	return admitCanvasFont(key, false, metrics);
}

void QtBackend::setCanvasText(bool enabled) {
	_canvasText = enabled;
}

void QtBackend::releaseCanvasFonts() {
	for (auto i = begin(_canvasFonts); i != end(_canvasFonts);) {
		if (i->second.sharedWithCore) {
			++i;
		} else {
			_fonts.remove(i->second.id);
			i = _canvasFonts.erase(i);
		}
	}
	_canvasSpellings.clear();
	_canvasFontsLeft = 0;
	_canvasParsesLeft = 0;
}

int QtBackend::admitCanvasFont(
		FontKey key,
		bool sharedWithCore,
		twlite::FontMetrics &out) {
	const auto i = _canvasFonts.find(key);
	if (i != end(_canvasFonts)) {
		i->second.sharedWithCore |= sharedWithCore;
		out = i->second.metrics;
		return i->second.id;
	} else if (_canvasFontsLeft <= 0
		|| int(_canvasFonts.size()) >= kMaxCanvasFonts) {
		return refuseCanvasFont(out);
	}
	const auto id = makeFont(key, out);
	twlite::set_em_box(out, out.ascent, out.descent, float(key.second));
	_canvasFonts.emplace(key, CachedFont{ id, out, sharedWithCore });
	--_canvasFontsLeft;
	return id;
}

int QtBackend::refuseCanvasFont(twlite::FontMetrics &out) {
	out = twlite::FontMetrics();
	if (!std::exchange(_canvasFontLimitNoted, true) && _diagnostic) {
		_diagnostic(
			std::string(kCanvasFontKind),
			"canvas text exceeded the allowance of "
				+ std::to_string(kMaxNewCanvasFonts)
				+ " new fonts per message, "
				+ std::to_string(kMaxCanvasFonts)
				+ " per widget, "
				+ std::to_string(kMaxCanvasFontParses)
				+ " font strings per message of at most "
				+ std::to_string(kMaxCanvasFontSpellingSize)
				+ " bytes; text in other fonts is skipped");
	}
	return 0;
}

} // namespace Tgw
