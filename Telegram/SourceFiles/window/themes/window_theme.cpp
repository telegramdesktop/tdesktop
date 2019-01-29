/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme.h"

#include "window/themes/window_theme_preview.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "core/application.h"
#include "storage/serialize_common.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "storage/localstorage.h"
#include "base/parse_helper.h"
#include "base/zlib_help.h"
#include "ui/image/image.h"
#include "boxes/background_box.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"

namespace Data {
namespace {

constexpr auto FromLegacyBackgroundId(int32 legacyId) -> WallPaperId {
	return uint64(0xFFFFFFFF00000000ULL) | uint64(uint32(legacyId));
}

constexpr auto kUninitializedBackground = FromLegacyBackgroundId(-999);
constexpr auto kTestingThemeBackground = FromLegacyBackgroundId(-666);
constexpr auto kTestingDefaultBackground = FromLegacyBackgroundId(-665);
constexpr auto kTestingEditorBackground = FromLegacyBackgroundId(-664);
constexpr auto kThemeBackground = FromLegacyBackgroundId(-2);
constexpr auto kCustomBackground = FromLegacyBackgroundId(-1);
constexpr auto kLegacy1DefaultBackground = FromLegacyBackgroundId(0);
constexpr auto kDefaultBackground = FromLegacyBackgroundId(105);

[[nodiscard]] bool ValidateFlags(MTPDwallPaper::Flags flags) {
	using Flag = MTPDwallPaper::Flag;
	const auto all = Flag(0)
		| Flag::f_creator
		| Flag::f_default
		| Flag::f_pattern
		| Flag::f_settings;
	return !(flags & ~all);
}

[[nodiscard]] bool ValidateFlags(MTPDwallPaperSettings::Flags flags) {
	using Flag = MTPDwallPaperSettings::Flag;
	const auto all = Flag(0)
		| Flag::f_background_color
		| Flag::f_blur
		| Flag::f_intensity
		| Flag::f_motion;
	return !(flags & ~all);
}

quint32 SerializeMaybeColor(std::optional<QColor> color) {
	return color
		? ((quint32(std::clamp(color->red(), 0, 255)) << 16)
			| (quint32(std::clamp(color->green(), 0, 255)) << 8)
			| quint32(std::clamp(color->blue(), 0, 255)))
		: quint32(-1);
}

std::optional<QColor> MaybeColorFromSerialized(quint32 serialized) {
	return (serialized == quint32(-1))
		? std::nullopt
		: std::make_optional(QColor(
			int((serialized >> 16) & 0xFFU),
			int((serialized >> 8) & 0xFFU),
			int(serialized & 0xFFU)));
}

std::optional<QColor> ColorFromString(const QString &string) {
	if (string.size() != 6) {
		return {};
	} else if (ranges::find_if(string, [](QChar ch) {
		return (ch < 'a' || ch > 'f')
			&& (ch < 'A' || ch > 'F')
			&& (ch < '0' || ch > '9');
	}) != string.end()) {
		return {};
	}
	const auto component = [](const QString &text, int index) {
		const auto decimal = [](QChar hex) {
			const auto code = hex.unicode();
			return (code >= '0' && code <= '9')
				? int(code - '0')
				: (code >= 'a' && code <= 'f')
				? int(code - 'a' + 0x0a)
				: int(code - 'A' + 0x0a);
		};
		index *= 2;
		return decimal(text[index]) * 0x10 + decimal(text[index + 1]);
	};
	return QColor(
		component(string, 0),
		component(string, 1),
		component(string, 2),
		255);
}

} // namespace

WallPaper::WallPaper(WallPaperId id) : _id(id) {
}

void WallPaper::setLocalImageAsThumbnail(not_null<Image*> image) {
	Expects(IsDefaultWallPaper(*this)
		|| IsLegacy1DefaultWallPaper(*this)
		|| IsCustomWallPaper(*this));
	Expects(_thumbnail == nullptr);

	_thumbnail = image;
}

WallPaperId WallPaper::id() const {
	return _id;
}

std::optional<QColor> WallPaper::backgroundColor() const {
	return _backgroundColor;
}

DocumentData *WallPaper::document() const {
	return _document;
}

Image *WallPaper::thumbnail() const {
	return _thumbnail;
}

bool WallPaper::isPattern() const {
	return _flags & MTPDwallPaper::Flag::f_pattern;
}

bool WallPaper::isDefault() const {
	return _flags & MTPDwallPaper::Flag::f_default;
}

bool WallPaper::isCreator() const {
	return _flags & MTPDwallPaper::Flag::f_creator;
}

int WallPaper::patternIntensity() const {
	return _intensity;
}

bool WallPaper::hasShareUrl() const {
	return !_slug.isEmpty();
}

QString WallPaper::shareUrl() const {
	return hasShareUrl()
		? Core::App().createInternalLinkFull("bg/" + _slug)
		: QString();
}

void WallPaper::loadThumbnail() const {
	if (_thumbnail) {
		_thumbnail->load(fileOrigin());
	}
}

void WallPaper::loadDocument() const {
	if (_document) {
		_document->save(fileOrigin(), QString());
	}
}

FileOrigin WallPaper::fileOrigin() const {
	return FileOriginWallpaper(_id, _accessHash);
}

WallPaper WallPaper::withUrlParams(
		const QMap<QString, QString> &params) const {
	using Flag = MTPDwallPaperSettings::Flag;

	auto result = *this;
	result._settings = Flag(0);
	result._backgroundColor = ColorFromString(_slug);
	result._intensity = kDefaultIntensity;

	if (auto mode = params.value("mode"); !mode.isEmpty()) {
		const auto list = mode.replace('+', ' ').split(' ');
		for (const auto &change : list) {
			if (change == qstr("blur")) {
				result._settings |= Flag::f_blur;
			} else if (change == qstr("motion")) {
				result._settings |= Flag::f_motion;
			}
		}
	}
	if (const auto color = ColorFromString(params.value("bg_color"))) {
		result._backgroundColor = color;
	}
	if (const auto string = params.value("intensity"); !string.isEmpty()) {
		auto ok = false;
		const auto intensity = string.toInt(&ok);
		if (ok && base::in_range(intensity, 0, 101)) {
			result._intensity = intensity;
		}
	}

	return result;
}

std::optional<WallPaper> WallPaper::Create(const MTPWallPaper &data) {
	return data.match([](const MTPDwallPaper &data) {
		return Create(data);
	});
}

std::optional<WallPaper> WallPaper::Create(const MTPDwallPaper &data) {
	using Flag = MTPDwallPaper::Flag;

	const auto document = Auth().data().processDocument(
		data.vdocument);
	if (!document->checkWallPaperProperties()) {
		return std::nullopt;
	}
	auto result = WallPaper(data.vid.v);
	result._accessHash = data.vaccess_hash.v;
	result._flags = data.vflags.v;
	result._slug = qs(data.vslug);
	result._document = document;
	result._thumbnail = document->thumbnail();
	if (data.has_settings()) {
		const auto isPattern = ((result._flags & Flag::f_pattern) != 0);
		data.vsettings.match([&](const MTPDwallPaperSettings &data) {
			using Flag = MTPDwallPaperSettings::Flag;

			result._settings = data.vflags.v;
			if (isPattern && data.has_background_color()) {
				result._backgroundColor = MaybeColorFromSerialized(
					data.vbackground_color.v);
			} else {
				result._settings &= ~Flag::f_background_color;
			}
			if (isPattern && data.has_intensity()) {
				result._intensity = data.vintensity.v;
			} else {
				result._settings &= ~Flag::f_intensity;
			}
		});
	}
	return result;
}

QByteArray WallPaper::serialize() const {
	auto size = sizeof(quint64) // _id
		+ sizeof(quint64) // _accessHash
		+ sizeof(qint32) // _flags
		+ Serialize::stringSize(_slug)
		+ sizeof(qint32) // _settings
		+ sizeof(quint32) // _backgroundColor
		+ sizeof(qint32); // _intensity

	auto result = QByteArray();
	result.reserve(size);
	{
		auto stream = QDataStream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< quint64(_id)
			<< quint64(_accessHash)
			<< qint32(_flags)
			<< _slug
			<< qint32(_settings)
			<< SerializeMaybeColor(_backgroundColor)
			<< qint32(_intensity);
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromSerialized(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return std::nullopt;
	}

	auto id = quint64();
	auto accessHash = quint64();
	auto flags = qint32();
	auto slug = QString();
	auto settings = qint32();
	auto backgroundColor = quint32();
	auto intensity = qint32();

	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		>> id
		>> accessHash
		>> flags
		>> slug
		>> settings
		>> backgroundColor
		>> intensity;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	} else if (intensity < 0 || intensity > 100) {
		return std::nullopt;
	}
	auto result = WallPaper(id);
	result._accessHash = accessHash;
	result._flags = MTPDwallPaper::Flags::from_raw(flags);
	result._slug = slug;
	result._settings = MTPDwallPaperSettings::Flags::from_raw(settings);
	result._backgroundColor = MaybeColorFromSerialized(backgroundColor);
	result._intensity = intensity;
	if (!ValidateFlags(result._flags) || !ValidateFlags(result._settings)) {
		return std::nullopt;
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromLegacySerialized(
		quint64 id,
		quint64 accessHash,
		quint32 flags,
		QString slug) {
	auto result = WallPaper(id);
	result._accessHash = accessHash;
	result._flags = MTPDwallPaper::Flags::from_raw(flags);
	result._slug = slug;
	result._backgroundColor = ColorFromString(slug);
	if (!ValidateFlags(result._flags)) {
		return std::nullopt;
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromLegacyId(qint32 legacyId) {
	auto result = WallPaper(FromLegacyBackgroundId(legacyId));
	if (!IsCustomWallPaper(result)) {
		result._flags = MTPDwallPaper::Flag::f_default;
	}
	return result;
}

std::optional<WallPaper> WallPaper::FromColorSlug(const QString &slug) {
	if (const auto color = ColorFromString(slug)) {
		auto result = CustomWallPaper();
		result._slug = slug;
		result._backgroundColor = color;
		return result;
	}
	return std::nullopt;
}

WallPaper ThemeWallPaper() {
	return WallPaper(kThemeBackground);
}

bool IsThemeWallPaper(const WallPaper &paper) {
	return (paper.id() == kThemeBackground);
}

WallPaper CustomWallPaper() {
	return WallPaper(kCustomBackground);
}

bool IsCustomWallPaper(const WallPaper &paper) {
	return (paper.id() == kCustomBackground);
}

WallPaper Legacy1DefaultWallPaper() {
	return WallPaper(kLegacy1DefaultBackground);
}

bool IsLegacy1DefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kLegacy1DefaultBackground);
}

WallPaper DefaultWallPaper() {
	return WallPaper(kDefaultBackground);
}

bool IsDefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kDefaultBackground);
}

QColor PatternColor(QColor background) {
	const auto hue = background.hueF();
	const auto saturation = background.saturationF();
	const auto value = background.valueF();
	return QColor::fromHsvF(
		hue,
		std::min(1.0, saturation + 0.05 + 0.1 * (1. - saturation)),
		(value > 0.5
			? std::max(0., value * 0.65)
			: std::max(0., std::min(1., 1. - value * 0.65))),
		0.4
	).toRgb();
}

QImage PreparePatternImage(
		QImage image,
		QColor bg,
		QColor fg,
		int intensity) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	// Similar to ColorizePattern.
	// But here we set bg to all 'alpha=0' pixels and fg to opaque ones.

	const auto width = image.width();
	const auto height = image.height();
	const auto alpha = anim::interpolate(
		0,
		255,
		fg.alphaF() * std::clamp(intensity / 100., 0., 1.));
	if (!alpha) {
		image.fill(bg);
		return image;
	}
	fg.setAlpha(255);
	const auto patternBg = anim::shifted(bg);
	const auto patternFg = anim::shifted(fg);

	const auto resultBytesPerPixel = (image.depth() >> 3);
	constexpr auto resultIntsPerPixel = 1;
	const auto resultIntsPerLine = (image.bytesPerLine() >> 2);
	const auto resultIntsAdded = resultIntsPerLine - width * resultIntsPerPixel;
	auto resultInts = reinterpret_cast<uint32*>(image.bits());
	Assert(resultIntsAdded >= 0);
	Assert(image.depth() == static_cast<int>((resultIntsPerPixel * sizeof(uint32)) << 3));
	Assert(image.bytesPerLine() == (resultIntsPerLine << 2));

	const auto maskBytesPerPixel = (image.depth() >> 3);
	const auto maskBytesPerLine = image.bytesPerLine();
	const auto maskBytesAdded = maskBytesPerLine - width * maskBytesPerPixel;

	// We want to read the last byte of four available.
	// This is the difference with style::colorizeImage.
	auto maskBytes = image.constBits() + (maskBytesPerPixel - 1);
	Assert(maskBytesAdded >= 0);
	Assert(image.depth() == (maskBytesPerPixel << 3));
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != width; ++x) {
			const auto maskOpacity = static_cast<anim::ShiftedMultiplier>(
				*maskBytes) + 1;
			const auto fgOpacity = (maskOpacity * alpha) >> 8;
			const auto bgOpacity = 256 - fgOpacity;
			*resultInts = anim::unshifted(
				patternBg * bgOpacity + patternFg * fgOpacity);
			maskBytes += maskBytesPerPixel;
			resultInts += resultIntsPerPixel;
		}
		maskBytes += maskBytesAdded;
		resultInts += resultIntsAdded;
	}
	return image;
}

namespace details {

WallPaper UninitializedWallPaper() {
	return WallPaper(kUninitializedBackground);
}

bool IsUninitializedWallPaper(const WallPaper &paper) {
	return (paper.id() == kUninitializedBackground);
}

WallPaper TestingThemeWallPaper() {
	return WallPaper(kTestingThemeBackground);
}

bool IsTestingThemeWallPaper(const WallPaper &paper) {
	return (paper.id() == kTestingThemeBackground);
}

WallPaper TestingDefaultWallPaper() {
	return WallPaper(kTestingDefaultBackground);
}

bool IsTestingDefaultWallPaper(const WallPaper &paper) {
	return (paper.id() == kTestingDefaultBackground);
}

WallPaper TestingEditorWallPaper() {
	return WallPaper(kTestingEditorBackground);
}

bool IsTestingEditorWallPaper(const WallPaper &paper) {
	return (paper.id() == kTestingEditorBackground);
}

} // namespace details
} // namespace Data

namespace Window {
namespace Theme {
namespace {

constexpr auto kThemeFileSizeLimit = 5 * 1024 * 1024;
constexpr auto kThemeBackgroundSizeLimit = 4 * 1024 * 1024;
constexpr auto kBackgroundSizeLimit = 25 * 1024 * 1024;
constexpr auto kThemeSchemeSizeLimit = 1024 * 1024;
constexpr auto kNightThemeFile = str_const(":/gui/night.tdesktop-theme");

struct Applying {
	QString pathRelative;
	QString pathAbsolute;
	QByteArray content;
	QByteArray paletteForRevert;
	Cached cached;
	Fn<void()> overrideKeep;
};

NeverFreedPointer<ChatBackground> GlobalBackground;
Applying GlobalApplying;

inline bool AreTestingTheme() {
	return !GlobalApplying.paletteForRevert.isEmpty();
};

QByteArray readThemeContent(const QString &path) {
	QFile file(path);
	if (!file.exists()) {
		LOG(("Theme Error: theme file not found: %1").arg(path));
		return QByteArray();
	}

	if (file.size() > kThemeFileSizeLimit) {
		LOG(("Theme Error: theme file too large: %1 (should be less than 5 MB, got %2)").arg(path).arg(file.size()));
		return QByteArray();
	}
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Theme Error: could not open theme file: %1").arg(path));
		return QByteArray();
	}

	return file.readAll();
}

inline uchar readHexUchar(char code, bool &error) {
	if (code >= '0' && code <= '9') {
		return ((code - '0') & 0xFF);
	} else if (code >= 'a' && code <= 'f') {
		return ((code + 10 - 'a') & 0xFF);
	} else if (code >= 'A' && code <= 'F') {
		return ((code + 10 - 'A') & 0xFF);
	}
	error = true;
	return 0xFF;
}

inline uchar readHexUchar(char char1, char char2, bool &error) {
	return ((readHexUchar(char1, error) & 0x0F) << 4) | (readHexUchar(char2, error) & 0x0F);
}

bool readNameAndValue(const char *&from, const char *end, QLatin1String *outName, QLatin1String *outValue) {
	using base::parse::skipWhitespaces;
	using base::parse::readName;

	if (!skipWhitespaces(from, end)) return true;

	*outName = readName(from, end);
	if (outName->size() == 0) {
		LOG(("Theme Error: Could not read name in the color scheme."));
		return false;
	}
	if (!skipWhitespaces(from, end)) {
		LOG(("Theme Error: Unexpected end of the color scheme."));
		return false;
	}
	if (*from != ':') {
		LOG(("Theme Error: Expected ':' between each name and value in the color scheme (while reading key '%1')").arg(*outName));
		return false;
	}
	if (!skipWhitespaces(++from, end)) {
		LOG(("Theme Error: Unexpected end of the color scheme."));
		return false;
	}
	auto valueStart = from;
	if (*from == '#') ++from;

	if (readName(from, end).size() == 0) {
		LOG(("Theme Error: Expected a color value in #rrggbb or #rrggbbaa format in the color scheme (while reading key '%1')").arg(*outName));
		return false;
	}
	*outValue = QLatin1String(valueStart, from - valueStart);

	if (!skipWhitespaces(from, end)) {
		LOG(("Theme Error: Unexpected end of the color scheme."));
		return false;
	}
	if (*from != ';') {
		LOG(("Theme Error: Expected ';' after each value in the color scheme (while reading key '%1')").arg(*outName));
		return false;
	}
	++from;
	return true;
}

enum class SetResult {
	Ok,
	Bad,
	NotFound,
};
SetResult setColorSchemeValue(QLatin1String name, QLatin1String value, Instance *out) {
	auto result = style::palette::SetResult::Ok;
	auto size = value.size();
	auto data = value.data();
	if (data[0] == '#' && (size == 7 || size == 9)) {
		auto error = false;
		auto r = readHexUchar(data[1], data[2], error);
		auto g = readHexUchar(data[3], data[4], error);
		auto b = readHexUchar(data[5], data[6], error);
		auto a = (size == 9) ? readHexUchar(data[7], data[8], error) : uchar(255);
		if (error) {
			LOG(("Theme Warning: Skipping value '%1: %2' (expected a color value in #rrggbb or #rrggbbaa or a previously defined key in the color scheme)").arg(name).arg(value));
			return SetResult::Ok;
		} else if (out) {
			result = out->palette.setColor(name, r, g, b, a);
		} else {
			result = style::main_palette::setColor(name, r, g, b, a);
		}
	} else {
		if (out) {
			result = out->palette.setColor(name, value);
		} else {
			result = style::main_palette::setColor(name, value);
		}
	}
	if (result == style::palette::SetResult::Ok) {
		return SetResult::Ok;
	} else if (result == style::palette::SetResult::KeyNotFound) {
		return SetResult::NotFound;
	} else if (result == style::palette::SetResult::ValueNotFound) {
		LOG(("Theme Warning: Skipping value '%1: %2' (expected a color value in #rrggbb or #rrggbbaa or a previously defined key in the color scheme)").arg(name).arg(value));
		return SetResult::Ok;
	} else if (result == style::palette::SetResult::Duplicate) {
		LOG(("Theme Warning: Color value appears more than once in the color scheme (while applying '%1: %2')").arg(name).arg(value));
		return SetResult::Ok;
	} else {
		LOG(("Theme Error: Unexpected internal error."));
	}
	return SetResult::Bad;
}

bool loadColorScheme(const QByteArray &content, Instance *out) {
	auto unsupported = QMap<QLatin1String, QLatin1String>();
	return ReadPaletteValues(content, [&unsupported, out](QLatin1String name, QLatin1String value) {
		// Find the named value in the already read unsupported list.
		value = unsupported.value(value, value);

		auto result = setColorSchemeValue(name, value, out);
		if (result == SetResult::Bad) {
			return false;
		} else if (result == SetResult::NotFound) {
			unsupported.insert(name, value);
		}
		return true;
	});
}

void applyBackground(QImage &&background, bool tiled, Instance *out) {
	if (out) {
		out->background = std::move(background);
		out->tiled = tiled;
	} else {
		Background()->setThemeData(std::move(background), tiled);
	}
}

bool loadThemeFromCache(const QByteArray &content, const Cached &cache) {
	if (cache.paletteChecksum != style::palette::Checksum()) {
		return false;
	}
	if (cache.contentChecksum != hashCrc32(content.constData(), content.size())) {
		return false;
	}

	QImage background;
	if (!cache.background.isEmpty()) {
		QDataStream stream(cache.background);
		QImageReader reader(stream.device());
#ifndef OS_MAC_OLD
		reader.setAutoTransform(true);
#endif // OS_MAC_OLD
		if (!reader.read(&background) || background.isNull()) {
			return false;
		}
	}

	if (!style::main_palette::load(cache.colors)) {
		return false;
	}
	Background()->saveAdjustableColors();
	if (!background.isNull()) {
		applyBackground(std::move(background), cache.tiled, nullptr);
	}

	return true;
}

enum class LoadResult {
	Loaded,
	Failed,
	NotFound,
};

LoadResult loadBackgroundFromFile(zlib::FileToRead &file, const char *filename, QByteArray *outBackground) {
	*outBackground = file.readFileContent(filename, zlib::kCaseInsensitive, kThemeBackgroundSizeLimit);
	if (file.error() == UNZ_OK) {
		return LoadResult::Loaded;
	} else if (file.error() == UNZ_END_OF_LIST_OF_FILE) {
		file.clearError();
		return LoadResult::NotFound;
	}
	LOG(("Theme Error: could not read '%1' in the theme file.").arg(filename));
	return LoadResult::Failed;
}

bool loadBackground(zlib::FileToRead &file, QByteArray *outBackground, bool *outTiled) {
	auto result = loadBackgroundFromFile(file, "background.jpg", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);

	result = loadBackgroundFromFile(file, "background.png", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);

	*outTiled = true;
	result = loadBackgroundFromFile(file, "tiled.jpg", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);

	result = loadBackgroundFromFile(file, "tiled.png", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);
	return true;
}

bool loadTheme(const QByteArray &content, Cached &cache, Instance *out = nullptr) {
	cache = Cached();
	zlib::FileToRead file(content);

	unz_global_info globalInfo = { 0 };
	file.getGlobalInfo(&globalInfo);
	if (file.error() == UNZ_OK) {
		auto schemeContent = file.readFileContent("colors.tdesktop-theme", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
		if (file.error() == UNZ_END_OF_LIST_OF_FILE) {
			file.clearError();
			schemeContent = file.readFileContent("colors.tdesktop-palette", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
		}
		if (file.error() != UNZ_OK) {
			LOG(("Theme Error: could not read 'colors.tdesktop-theme' or 'colors.tdesktop-palette' in the theme file."));
			return false;
		}
		if (!loadColorScheme(schemeContent, out)) {
			return false;
		}
		Background()->saveAdjustableColors();

		auto backgroundTiled = false;
		auto backgroundContent = QByteArray();
		if (!loadBackground(file, &backgroundContent, &backgroundTiled)) {
			return false;
		}

		if (!backgroundContent.isEmpty()) {
			auto check = QBuffer(&backgroundContent);
			auto reader = QImageReader(&check);
			const auto size = reader.size();
			if (size.isEmpty()
				|| (size.width() * size.height() > kBackgroundSizeLimit)) {
				LOG(("Theme Error: bad background image size in the theme file."));
				return false;
			}
			auto background = App::readImage(backgroundContent);
			if (background.isNull()) {
				LOG(("Theme Error: could not read background image in the theme file."));
				return false;
			}
			auto buffer = QBuffer(&cache.background);
			if (!background.save(&buffer, "BMP")) {
				LOG(("Theme Error: could not write background image as a BMP to cache."));
				return false;
			}
			cache.tiled = backgroundTiled;

			applyBackground(std::move(background), cache.tiled, out);
		}
	} else {
		// Looks like it is not a .zip theme.
		if (!loadColorScheme(content, out)) {
			return false;
		}
		Background()->saveAdjustableColors();
	}
	if (out) {
		cache.colors = out->palette.save();
	} else {
		cache.colors = style::main_palette::save();
	}
	cache.paletteChecksum = style::palette::Checksum();
	cache.contentChecksum = hashCrc32(content.constData(), content.size());

	return true;
}

QImage validateBackgroundImage(QImage image) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	image.setDevicePixelRatio(cRetinaFactor());
	return image;
}

void adjustColor(style::color color, float64 hue, float64 saturation) {
	auto original = color->c;
	original.setHslF(hue, saturation, original.lightnessF(), original.alphaF());
	color.set(original.red(), original.green(), original.blue(), original.alpha());
}

void WriteAppliedTheme() {
	auto saved = Saved();
	saved.pathRelative = GlobalApplying.pathRelative;
	saved.pathAbsolute = GlobalApplying.pathAbsolute;
	saved.content = std::move(GlobalApplying.content);
	saved.cache = std::move(GlobalApplying.cached);
	Local::writeTheme(saved);
}

void ClearApplying() {
	GlobalApplying = Applying();
}

} // namespace

ChatBackground::AdjustableColor::AdjustableColor(style::color data)
: item(data)
, original(data->c) {
}

ChatBackground::ChatBackground() : _adjustableColors({
		st::msgServiceBg,
		st::msgServiceBgSelected,
		st::historyScrollBg,
		st::historyScrollBgOver,
		st::historyScrollBarBg,
		st::historyScrollBarBgOver }) {
	saveAdjustableColors();
}

void ChatBackground::setThemeData(QImage &&themeImage, bool themeTile) {
	_themeImage = validateBackgroundImage(std::move(themeImage));
	_themeTile = themeTile;
}

void ChatBackground::start() {
	if (Data::details::IsUninitializedWallPaper(_paper)) {
		if (!Local::readBackground()) {
			set(Data::ThemeWallPaper());
		}
	}
}

void ChatBackground::set(const Data::WallPaper &paper, QImage image) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}

	const auto needResetAdjustable = Data::IsDefaultWallPaper(paper)
		&& !Data::IsDefaultWallPaper(_paper)
		&& !nightMode()
		&& _themeAbsolutePath.isEmpty();
	if (Data::IsThemeWallPaper(paper) && _themeImage.isNull()) {
		setPaper(Data::DefaultWallPaper());
	} else {
		setPaper(paper);
		if (needResetAdjustable) {
			// If we had a default color theme with non-default background,
			// and we switch to default background we must somehow switch from
			// adjusted service colors to default (non-adjusted) service colors.
			// The only way to do that right now is through full palette reset.
			restoreAdjustableColors();
		}
	}
	if (Data::IsThemeWallPaper(_paper)) {
		(nightMode() ? _tileNightValue : _tileDayValue) = _themeTile;
		setPreparedImage(_themeImage, _themeImage);
	} else if (Data::details::IsTestingThemeWallPaper(_paper)
		|| Data::details::IsTestingDefaultWallPaper(_paper)
		|| Data::details::IsTestingEditorWallPaper(_paper)) {
		if (Data::details::IsTestingDefaultWallPaper(_paper)
			|| image.isNull()) {
			image.load(qsl(":/gui/art/bg.jpg"));
			setPaper(Data::details::TestingDefaultWallPaper());
		}
		image = validateBackgroundImage(std::move(image));
		setPreparedImage(image, image);
	} else {
		if (Data::IsLegacy1DefaultWallPaper(_paper)) {
			image.load(qsl(":/gui/art/bg_initial.jpg"));
			const auto scale = cScale() * cIntRetinaFactor();
			if (scale != 100) {
				image = image.scaledToWidth(
					ConvertScale(image.width(), scale),
					Qt::SmoothTransformation);
			}
		} else if (Data::IsDefaultWallPaper(_paper)
			|| (!_paper.backgroundColor() && image.isNull())) {
			setPaper(Data::DefaultWallPaper());
			image.load(qsl(":/gui/art/bg.jpg"));
		}
		Local::writeBackground(
			_paper,
			((Data::IsDefaultWallPaper(_paper)
				|| Data::IsLegacy1DefaultWallPaper(_paper))
				? QImage()
				: image));
		if (const auto fill = _paper.backgroundColor()) {
			if (_paper.isPattern() && !image.isNull()) {
				auto prepared = validateBackgroundImage(
					Data::PreparePatternImage(
						image,
						*fill,
						Data::PatternColor(*fill),
						_paper.patternIntensity()));
				setPreparedImage(std::move(image), std::move(prepared));
			} else {
				_original = QImage();
				_pixmap = QPixmap();
				_pixmapForTiled = QPixmap();
				if (adjustPaletteRequired()) {
					adjustPaletteUsingColor(*fill);
				}
			}
		} else {
			image = validateBackgroundImage(std::move(image));
			setPreparedImage(image, image);
		}
	}
	Assert(colorForFill()
		|| (!_original.isNull()
			&& !_pixmap.isNull()
			&& !_pixmapForTiled.isNull()));

	notify(BackgroundUpdate(BackgroundUpdate::Type::New, tile()));
	if (needResetAdjustable) {
		notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
		notify(BackgroundUpdate(BackgroundUpdate::Type::ApplyingTheme, tile()), true);
	}
}

void ChatBackground::setPreparedImage(QImage original, QImage prepared) {
	Expects(original.format() == QImage::Format_ARGB32_Premultiplied);
	Expects(original.width() > 0 && original.height() > 0);
	Expects(prepared.format() == QImage::Format_ARGB32_Premultiplied);
	Expects(prepared.width() > 0 && prepared.height() > 0);

	_original = std::move(original);
	if (adjustPaletteRequired()) {
		adjustPaletteUsingBackground(prepared);
	}
	preparePixmaps(std::move(prepared));
}

void ChatBackground::preparePixmaps(QImage image) {
	const auto width = image.width();
	const auto height = image.height();
	const auto isSmallForTiled = (width < kMinimumTiledSize)
		|| (height < kMinimumTiledSize);
	if (isSmallForTiled) {
		const auto repeatTimesX = qCeil(kMinimumTiledSize / (1. * width));
		const auto repeatTimesY = qCeil(kMinimumTiledSize / (1. * height));
		auto imageForTiled = QImage(
			width * repeatTimesX,
			height * repeatTimesY,
			QImage::Format_ARGB32_Premultiplied);
		imageForTiled.setDevicePixelRatio(image.devicePixelRatio());
		auto imageForTiledBytes = imageForTiled.bits();
		auto bytesInLine = width * sizeof(uint32);
		for (auto timesY = 0; timesY != repeatTimesY; ++timesY) {
			auto imageBytes = image.constBits();
			for (auto y = 0; y != height; ++y) {
				for (auto timesX = 0; timesX != repeatTimesX; ++timesX) {
					memcpy(imageForTiledBytes, imageBytes, bytesInLine);
					imageForTiledBytes += bytesInLine;
				}
				imageBytes += image.bytesPerLine();
				imageForTiledBytes += imageForTiled.bytesPerLine() - (repeatTimesX * bytesInLine);
			}
		}
		_pixmapForTiled = App::pixmapFromImageInPlace(std::move(imageForTiled));
	}
	_pixmap = App::pixmapFromImageInPlace(std::move(image));
	if (!isSmallForTiled) {
		_pixmapForTiled = _pixmap;
	}
}

void ChatBackground::setPaper(const Data::WallPaper &paper) {
	_paper = paper;
}

bool ChatBackground::adjustPaletteRequired() {
	const auto usingThemeBackground = [&] {
		return Data::IsThemeWallPaper(_paper)
			|| Data::details::IsTestingThemeWallPaper(_paper);
	};
	const auto usingDefaultBackground = [&] {
		return Data::IsDefaultWallPaper(_paper)
			|| Data::details::IsTestingDefaultWallPaper(_paper);
	};
	const auto testingPalette = [&] {
		const auto path = AreTestingTheme()
			? GlobalApplying.pathAbsolute
			: _themeAbsolutePath;
		return IsPaletteTestingPath(path);
	};

	if (testingPalette()) {
		return false;
	} else if (isNonDefaultThemeOrBackground() || nightMode()) {
		return !usingThemeBackground();
	}
	return !usingDefaultBackground();
}

void ChatBackground::adjustPaletteUsingBackground(const QImage &image) {
	uint64 components[3] = { 0 };
	uint64 componentsScroll[3] = { 0 };
	const auto w = image.width();
	const auto h = image.height();
	const auto size = w * h;
	if (const auto pix = image.constBits()) {
		for (auto i = 0, l = size * 4; i != l; i += 4) {
			components[2] += pix[i + 0];
			components[1] += pix[i + 1];
			components[0] += pix[i + 2];
		}
	}

	if (size) {
		for (auto i = 0; i != 3; ++i) {
			components[i] /= size;
		}
	}

	adjustPaletteUsingColor(
		QColor(components[0], components[1], components[2]));
}

void ChatBackground::adjustPaletteUsingColor(QColor color) {
	const auto hue = color.hslHueF();
	const auto saturation = color.hslSaturationF();
	for (const auto &color : _adjustableColors) {
		adjustColor(color.item, hue, saturation);
	}
}

std::optional<QColor> ChatBackground::colorForFill() const {
	return _pixmap.isNull() ? _paper.backgroundColor() : std::nullopt;
}

QImage ChatBackground::createCurrentImage() const {
	if (const auto fill = colorForFill()) {
		auto result = QImage(
			kMinimumTiledSize,
			kMinimumTiledSize,
			QImage::Format_ARGB32_Premultiplied);
		result.fill(*fill);
		return result;
	}
	return pixmap().toImage();
}

bool ChatBackground::tile() const {
	return nightMode() ? _tileNightValue : _tileDayValue;
}

bool ChatBackground::tileDay() const {
	if (Data::details::IsTestingThemeWallPaper(_paper) ||
		Data::details::IsTestingDefaultWallPaper(_paper)) {
		if (!nightMode()) {
			return _tileForRevert;
		}
	}
	return _tileDayValue;
}

bool ChatBackground::tileNight() const {
	if (Data::details::IsTestingThemeWallPaper(_paper) ||
		Data::details::IsTestingDefaultWallPaper(_paper)) {
		if (nightMode()) {
			return _tileForRevert;
		}
	}
	return _tileNightValue;
}

void ChatBackground::ensureStarted() {
	if (_pixmap.isNull() && !_paper.backgroundColor()) {
		// We should start first, otherwise the default call
		// to start() will reset this value to _themeTile.
		start();
	}
}

void ChatBackground::setTile(bool tile) {
	ensureStarted();
	const auto old = this->tile();
	if (nightMode()) {
		setTileNightValue(tile);
	} else {
		setTileDayValue(tile);
	}
	if (this->tile() != old) {
		if (!Data::details::IsTestingThemeWallPaper(_paper)
			&& !Data::details::IsTestingDefaultWallPaper(_paper)) {
			Local::writeUserSettings();
		}
		notify(BackgroundUpdate(BackgroundUpdate::Type::Changed, tile));
	}
}

void ChatBackground::setTileDayValue(bool tile) {
	ensureStarted();
	_tileDayValue = tile;
}

void ChatBackground::setTileNightValue(bool tile) {
	ensureStarted();
	_tileNightValue = tile;
}

void ChatBackground::setThemeAbsolutePath(const QString &path) {
	_themeAbsolutePath = path;
}

QString ChatBackground::themeAbsolutePath() const {
	return _themeAbsolutePath;
}

void ChatBackground::reset() {
	if (Data::details::IsTestingThemeWallPaper(_paper)
		|| Data::details::IsTestingDefaultWallPaper(_paper)) {
		if (_themeImage.isNull()) {
			_paperForRevert = Data::DefaultWallPaper();
			_originalForRevert = QImage();
			_tileForRevert = false;
		} else {
			_paperForRevert = Data::ThemeWallPaper();
			_originalForRevert = _themeImage;
			_tileForRevert = _themeTile;
		}
	} else {
		set(Data::ThemeWallPaper());
		restoreAdjustableColors();
		notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
		notify(BackgroundUpdate(BackgroundUpdate::Type::ApplyingTheme, tile()), true);
	}
}

void ChatBackground::saveForRevert() {
	ensureStarted();
	if (!Data::details::IsTestingThemeWallPaper(_paper)
		&& !Data::details::IsTestingDefaultWallPaper(_paper)) {
		_paperForRevert = _paper;
		_originalForRevert = std::move(_original);
		_tileForRevert = tile();
	}
}

void ChatBackground::saveAdjustableColors() {
	for (auto &color : _adjustableColors) {
		color.original = color.item->c;
	}
}

void ChatBackground::restoreAdjustableColors() {
	for (const auto &color : _adjustableColors) {
		const auto value = color.original;
		color.item.set(value.red(), value.green(), value.blue(), value.alpha());
	}
}

void ChatBackground::setTestingTheme(Instance &&theme) {
	style::main_palette::apply(theme.palette);
	saveAdjustableColors();

	auto switchToThemeBackground = !theme.background.isNull()
		|| Data::IsThemeWallPaper(_paper)
		|| (Data::IsDefaultWallPaper(_paper)
			&& !nightMode()
			&& _themeAbsolutePath.isEmpty());
	if (AreTestingTheme()
		&& IsPaletteTestingPath(GlobalApplying.pathAbsolute)) {
		// Grab current background image if it is not already custom
		// Use prepared pixmap, not original image, because we're
		// for sure switching to a non-pattern wall-paper (testing editor).
		if (!Data::IsCustomWallPaper(_paper)) {
			saveForRevert();
			set(
				Data::details::TestingEditorWallPaper(),
				std::move(_pixmap).toImage());
		}
	} else if (switchToThemeBackground) {
		saveForRevert();
		set(
			Data::details::TestingThemeWallPaper(),
			std::move(theme.background));
		setTile(theme.tiled);
	} else {
		// Apply current background image so that service bg colors are recounted.
		set(_paper, std::move(_original));
	}
	notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
}

void ChatBackground::setTestingDefaultTheme() {
	style::main_palette::reset();
	saveAdjustableColors();

	saveForRevert();
	set(Data::details::TestingDefaultWallPaper());
	setTile(false);
	notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
}

void ChatBackground::keepApplied(const QString &path, bool write) {
	setThemeAbsolutePath(path);
	if (Data::details::IsTestingEditorWallPaper(_paper)) {
		setPaper(Data::CustomWallPaper());
		_themeImage = QImage();
		_themeTile = false;
		if (write) {
			writeNewBackgroundSettings();
		}
	} else if (Data::details::IsTestingThemeWallPaper(_paper)) {
		setPaper(Data::ThemeWallPaper());
		_themeImage = validateBackgroundImage(base::duplicate(_original));
		_themeTile = tile();
		if (write) {
			writeNewBackgroundSettings();
		}
	} else if (Data::details::IsTestingDefaultWallPaper(_paper)) {
		setPaper(Data::DefaultWallPaper());
		_themeImage = QImage();
		_themeTile = false;
		if (write) {
			writeNewBackgroundSettings();
		}
	}
	notify(BackgroundUpdate(BackgroundUpdate::Type::ApplyingTheme, tile()), true);
}

bool ChatBackground::isNonDefaultThemeOrBackground() {
	start();
	return nightMode()
		? (_themeAbsolutePath != NightThemePath()
			|| !Data::IsThemeWallPaper(_paper))
		: (!_themeAbsolutePath.isEmpty()
			|| !Data::IsDefaultWallPaper(_paper));
}

bool ChatBackground::isNonDefaultBackground() {
	start();
	return _themeAbsolutePath.isEmpty()
		? !Data::IsDefaultWallPaper(_paper)
		: !Data::IsThemeWallPaper(_paper);
}

void ChatBackground::writeNewBackgroundSettings() {
	if (tile() != _tileForRevert) {
		Local::writeUserSettings();
	}
	Local::writeBackground(
		_paper,
		((Data::IsThemeWallPaper(_paper)
			|| Data::IsDefaultWallPaper(_paper))
			? QImage()
			: _original));
}

void ChatBackground::revert() {
	if (Data::details::IsTestingThemeWallPaper(_paper)
		|| Data::details::IsTestingDefaultWallPaper(_paper)
		|| Data::details::IsTestingEditorWallPaper(_paper)) {
		setTile(_tileForRevert);
		set(_paperForRevert, std::move(_originalForRevert));
	} else {
		// Apply current background image so that service bg colors are recounted.
		set(_paper, std::move(_original));
	}
	notify(BackgroundUpdate(BackgroundUpdate::Type::RevertingTheme, tile()), true);
}

void ChatBackground::setNightModeValue(bool nightMode) {
	_nightMode = nightMode;
}

bool ChatBackground::nightMode() const {
	return _nightMode;
}

void ChatBackground::toggleNightMode(std::optional<QString> themePath) {
	const auto settingDefault = themePath.has_value();
	const auto oldNightMode = _nightMode;
	const auto newNightMode = !_nightMode;
	_nightMode = newNightMode;
	auto read = settingDefault ? Saved() : Local::readThemeAfterSwitch();
	auto path = read.pathAbsolute;

	_nightMode = oldNightMode;
	auto oldTileValue = (_nightMode ? _tileNightValue : _tileDayValue);
	const auto alreadyOnDisk = [&] {
		if (read.content.isEmpty()) {
			return false;
		}
		auto preview = std::make_unique<Preview>();
		preview->pathAbsolute = std::move(read.pathAbsolute);
		preview->pathRelative = std::move(read.pathRelative);
		preview->content = std::move(read.content);
		preview->instance.cached = std::move(read.cache);
		const auto loaded = loadTheme(
			preview->content,
			preview->instance.cached,
			&preview->instance);
		if (!loaded) {
			return false;
		}
		Apply(std::move(preview));
		return true;
	}();
	if (!alreadyOnDisk) {
		path = themePath
			? *themePath
			: (newNightMode ? NightThemePath() : QString());
		ApplyDefaultWithPath(path);
	}

	// Theme editor could have already reverted the testing of this toggle.
	if (AreTestingTheme()) {
		GlobalApplying.overrideKeep = [=] {
			_nightMode = newNightMode;

			// Restore the value, it was set inside theme testing.
			(oldNightMode ? _tileNightValue : _tileDayValue) = oldTileValue;

			if (!alreadyOnDisk) {
				// First-time switch to default night mode should write it.
				WriteAppliedTheme();
			}
			ClearApplying();
			keepApplied(path, settingDefault);
			if (tile() != _tileForRevert) {
				Local::writeUserSettings();
			}
			Local::writeSettings();
			if (!settingDefault && !Local::readBackground()) {
				set(Data::ThemeWallPaper());
			}
		};
	}
}

ChatBackground *Background() {
	GlobalBackground.createIfNull();
	return GlobalBackground.data();
}

bool Load(Saved &&saved) {
	if (saved.content.size() < 4) {
		LOG(("Theme Error: Could not load theme from '%1' (%2)"
			).arg(saved.pathRelative
			).arg(saved.pathAbsolute));
		return false;
	}

	GlobalBackground.createIfNull();
	if (loadThemeFromCache(saved.content, saved.cache)) {
		Background()->setThemeAbsolutePath(saved.pathAbsolute);
		return true;
	}

	if (!loadTheme(saved.content, saved.cache)) {
		return false;
	}
	Local::writeTheme(saved);
	Background()->setThemeAbsolutePath(saved.pathAbsolute);
	return true;
}

void Unload() {
	GlobalBackground.clear();
	GlobalApplying = Applying();
}

bool Apply(const QString &filepath) {
	if (auto preview = PreviewFromFile(filepath)) {
		return Apply(std::move(preview));
	}
	return false;
}

bool Apply(std::unique_ptr<Preview> preview) {
	GlobalApplying.pathRelative = std::move(preview->pathRelative);
	GlobalApplying.pathAbsolute = std::move(preview->pathAbsolute);
	GlobalApplying.content = std::move(preview->content);
	GlobalApplying.cached = std::move(preview->instance.cached);
	if (GlobalApplying.paletteForRevert.isEmpty()) {
		GlobalApplying.paletteForRevert = style::main_palette::save();
	}
	Background()->setTestingTheme(std::move(preview->instance));
	return true;
}

void ApplyDefaultWithPath(const QString &themePath) {
	if (!themePath.isEmpty()) {
		if (auto preview = PreviewFromFile(themePath)) {
			Apply(std::move(preview));
		}
	} else {
		GlobalApplying.pathRelative = QString();
		GlobalApplying.pathAbsolute = QString();
		GlobalApplying.content = QByteArray();
		GlobalApplying.cached = Cached();
		if (GlobalApplying.paletteForRevert.isEmpty()) {
			GlobalApplying.paletteForRevert = style::main_palette::save();
		}
		Background()->setTestingDefaultTheme();
	}
}

bool ApplyEditedPalette(const QString &path, const QByteArray &content) {
	Instance out;
	if (!loadColorScheme(content, &out)) {
		return false;
	}
	out.cached.colors = out.palette.save();
	out.cached.paletteChecksum = style::palette::Checksum();
	out.cached.contentChecksum = hashCrc32(content.constData(), content.size());

	GlobalApplying.pathRelative = path.isEmpty()
		? QString()
		: QDir().relativeFilePath(path);
	GlobalApplying.pathAbsolute = path.isEmpty()
		? QString()
		: QFileInfo(path).absoluteFilePath();
	GlobalApplying.content = content;
	GlobalApplying.cached = out.cached;
	if (GlobalApplying.paletteForRevert.isEmpty()) {
		GlobalApplying.paletteForRevert = style::main_palette::save();
	}
	Background()->setTestingTheme(std::move(out));
	KeepApplied();
	return true;
}

void KeepApplied() {
	if (!AreTestingTheme()) {
		return;
	} else if (GlobalApplying.overrideKeep) {
		// This callback will be destroyed while running.
		// And it won't be able to safely access captures after that.
		// So we save it on stack for the time while it is running.
		const auto onstack = base::take(GlobalApplying.overrideKeep);
		onstack();
		return;
	}
	const auto path = GlobalApplying.pathAbsolute;
	WriteAppliedTheme();
	ClearApplying();
	Background()->keepApplied(path, true);
}

void Revert() {
	if (!AreTestingTheme()) {
		return;
	}
	style::main_palette::load(GlobalApplying.paletteForRevert);
	Background()->saveAdjustableColors();

	ClearApplying();
	Background()->revert();
}

QString NightThemePath() {
	return str_const_toString(kNightThemeFile);
}

bool IsNonDefaultBackground() {
	return Background()->isNonDefaultBackground();
}

bool IsNightMode() {
	return GlobalBackground ? Background()->nightMode() : false;
}

void SetNightModeValue(bool nightMode) {
	if (GlobalBackground || nightMode) {
		Background()->setNightModeValue(nightMode);
	}
}

void ToggleNightMode() {
	Background()->toggleNightMode(std::nullopt);
}

void ToggleNightMode(const QString &path) {
	Background()->toggleNightMode(path);
}

bool LoadFromFile(const QString &path, Instance *out, QByteArray *outContent) {
	*outContent = readThemeContent(path);
	if (outContent->size() < 4) {
		LOG(("Theme Error: Could not load theme from %1").arg(path));
		return false;
	}

	return loadTheme(*outContent,  out->cached, out);
}

bool IsPaletteTestingPath(const QString &path) {
	if (path.endsWith(qstr(".tdesktop-palette"), Qt::CaseInsensitive)) {
		return QFileInfo(path).exists();
	}
	return false;
}

void ComputeBackgroundRects(QRect wholeFill, QSize imageSize, QRect &to, QRect &from) {
	if (uint64(imageSize.width()) * wholeFill.height() > uint64(imageSize.height()) * wholeFill.width()) {
		float64 pxsize = wholeFill.height() / float64(imageSize.height());
		int takewidth = qCeil(wholeFill.width() / pxsize);
		if (takewidth > imageSize.width()) {
			takewidth = imageSize.width();
		} else if ((imageSize.width() % 2) != (takewidth % 2)) {
			++takewidth;
		}
		to = QRect(int((wholeFill.width() - takewidth * pxsize) / 2.), 0, qCeil(takewidth * pxsize), wholeFill.height());
		from = QRect((imageSize.width() - takewidth) / 2, 0, takewidth, imageSize.height());
	} else {
		float64 pxsize = wholeFill.width() / float64(imageSize.width());
		int takeheight = qCeil(wholeFill.height() / pxsize);
		if (takeheight > imageSize.height()) {
			takeheight = imageSize.height();
		} else if ((imageSize.height() % 2) != (takeheight % 2)) {
			++takeheight;
		}
		to = QRect(0, int((wholeFill.height() - takeheight * pxsize) / 2.), wholeFill.width(), qCeil(takeheight * pxsize));
		from = QRect(0, (imageSize.height() - takeheight) / 2, imageSize.width(), takeheight);
	}
}

bool CopyColorsToPalette(const QString &path, const QByteArray &themeContent) {
	auto paletteContent = themeContent;

	zlib::FileToRead file(themeContent);

	unz_global_info globalInfo = { 0 };
	file.getGlobalInfo(&globalInfo);
	if (file.error() == UNZ_OK) {
		paletteContent = file.readFileContent("colors.tdesktop-theme", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
		if (file.error() == UNZ_END_OF_LIST_OF_FILE) {
			file.clearError();
			paletteContent = file.readFileContent("colors.tdesktop-palette", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
		}
		if (file.error() != UNZ_OK) {
			LOG(("Theme Error: could not read 'colors.tdesktop-theme' or 'colors.tdesktop-palette' in the theme file, while copying to '%1'.").arg(path));
			return false;
		}
	}

	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		LOG(("Theme Error: could not open file for write '%1'").arg(path));
		return false;
	}

	if (f.write(paletteContent) != paletteContent.size()) {
		LOG(("Theme Error: could not write palette to '%1'").arg(path));
		return false;
	}
	return true;
}

bool ReadPaletteValues(const QByteArray &content, Fn<bool(QLatin1String name, QLatin1String value)> callback) {
	if (content.size() > kThemeSchemeSizeLimit) {
		LOG(("Theme Error: color scheme file too large (should be less than 1 MB, got %2)").arg(content.size()));
		return false;
	}

	auto data = base::parse::stripComments(content);
	auto from = data.constData(), end = from + data.size();
	while (from != end) {
		auto name = QLatin1String("");
		auto value = QLatin1String("");
		if (!readNameAndValue(from, end, &name, &value)) {
			return false;
		}
		if (name.size() == 0) { // End of content reached.
			return true;
		}
		if (!callback(name, value)) {
			return false;
		}
	}
	return true;
}

} // namespace Theme
} // namespace Window
