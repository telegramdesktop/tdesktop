/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme.h"

#include "window/themes/window_theme_preview.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "base/parse_helper.h"
#include "base/zlib_help.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"
#include "boxes/background_box.h"

namespace Window {
namespace Theme {
namespace {

constexpr auto kThemeFileSizeLimit = 5 * 1024 * 1024;
constexpr auto kThemeBackgroundSizeLimit = 4 * 1024 * 1024;
constexpr auto kThemeSchemeSizeLimit = 1024 * 1024;
constexpr auto kMinimumTiledSize = 512;
constexpr auto kNightThemeFile = str_const(":/gui/night.tdesktop-theme");

struct Data {
	struct Applying {
		QString pathRelative;
		QString pathAbsolute;
		QByteArray content;
		QByteArray paletteForRevert;
		Cached cached;
		Fn<void()> overrideKeep;
	};

	ChatBackground background;
	Applying applying;
};
NeverFreedPointer<Data> instance;

inline bool AreTestingTheme() {
	if (instance) {
		return !instance->applying.paletteForRevert.isEmpty();
	}
	return false;
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
			auto background = App::readImage(backgroundContent);
			if (background.isNull()) {
				LOG(("Theme Error: could not read background image in the theme file."));
				return false;
			}
			QBuffer buffer(&cache.background);
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

QImage prepareBackgroundImage(QImage &&image) {
	if (image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_ARGB32_Premultiplied && image.format() != QImage::Format_RGB32) {
		image = std::move(image).convertToFormat(QImage::Format_RGB32);
	}
	image.setDevicePixelRatio(cRetinaFactor());
	return std::move(image);
}

void adjustColor(style::color color, float64 hue, float64 saturation) {
	auto original = color->c;
	original.setHslF(hue, saturation, original.lightnessF(), original.alphaF());
	color.set(original.red(), original.green(), original.blue(), original.alpha());
}

void WriteAppliedTheme() {
	auto saved = Saved();
	saved.pathRelative = instance->applying.pathRelative;
	saved.pathAbsolute = instance->applying.pathAbsolute;
	saved.content = std::move(instance->applying.content);
	saved.cache = std::move(instance->applying.cached);
	Local::writeTheme(saved);
}

void ClearApplying() {
	instance->applying = Data::Applying();
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
	_themeImage = prepareBackgroundImage(std::move(themeImage));
	_themeTile = themeTile;
}

void ChatBackground::start() {
	if (_id == internal::kUninitializedBackground) {
		if (!Local::readBackground()) {
			setImage(kThemeBackground);
		}
	}
}

void ChatBackground::setImage(int32 id, QImage &&image) {
	auto needResetAdjustable = (id == kDefaultBackground)
		&& (_id != kDefaultBackground)
		&& !nightMode()
		&& _themeAbsolutePath.isEmpty();
	if (id == kThemeBackground && _themeImage.isNull()) {
		id = kDefaultBackground;
	} else if (needResetAdjustable) {
		// If we had a default color theme with non-default background,
		// and we switch to default background we must somehow switch from
		// adjusted service colors to default (non-adjusted) service colors.
		// The only way to do that right now is through full palette reset.
		restoreAdjustableColors();
	}
	_id = id;
	if (_id == kThemeBackground) {
		(nightMode() ? _tileNightValue : _tileDayValue) = _themeTile;
		setPreparedImage(QImage(_themeImage));
	} else if (_id == internal::kTestingThemeBackground
		|| _id == internal::kTestingDefaultBackground
		|| _id == internal::kTestingEditorBackground) {
		if (_id == internal::kTestingDefaultBackground || image.isNull()) {
			image.load(qsl(":/gui/art/bg.jpg"));
			_id = internal::kTestingDefaultBackground;
		}
		setPreparedImage(std::move(image));
	} else {
		if (_id == kInitialBackground) {
			image.load(qsl(":/gui/art/bg_initial.jpg"));
			if (cRetina()) {
				image = image.scaledToWidth(image.width() * 2, Qt::SmoothTransformation);
			} else if (cScale() != dbisOne) {
				image = image.scaledToWidth(convertScale(image.width()), Qt::SmoothTransformation);
			}
		} else if (_id == kDefaultBackground || image.isNull()) {
			_id = kDefaultBackground;
			image.load(qsl(":/gui/art/bg.jpg"));
		}
		Local::writeBackground(_id, (_id == kDefaultBackground || _id == kInitialBackground) ? QImage() : image);
		setPreparedImage(prepareBackgroundImage(std::move(image)));
	}
	Assert(!_pixmap.isNull() && !_pixmapForTiled.isNull());
	notify(BackgroundUpdate(BackgroundUpdate::Type::New, tile()));
	if (needResetAdjustable) {
		notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
		notify(BackgroundUpdate(BackgroundUpdate::Type::ApplyingTheme, tile()), true);
	}
}

void ChatBackground::setPreparedImage(QImage &&image) {
	image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());

	auto adjustColors = [&] {
		const auto usingThemeBackground = [&] {
			return (_id == kThemeBackground)
				|| (_id == internal::kTestingThemeBackground);
		};
		const auto usingDefaultBackground = [&] {
			return (_id == kDefaultBackground)
				|| (_id == internal::kTestingDefaultBackground);
		};
		const auto testingPalette = [&] {
			const auto path = AreTestingTheme()
				? instance->applying.pathAbsolute
				: _themeAbsolutePath;
			return IsPaletteTestingPath(path);
		};

		if (testingPalette()) {
			return false;
		} else if (IsNonDefaultThemeOrBackground() || nightMode()) {
			return !usingThemeBackground();
		}
		return !usingDefaultBackground();
	}();
	if (adjustColors) {
		adjustPaletteUsingBackground(image);
	}

	auto width = image.width();
	auto height = image.height();
	Assert(width > 0 && height > 0);
	auto isSmallForTiled = (width < kMinimumTiledSize || height < kMinimumTiledSize);
	if (isSmallForTiled) {
		auto repeatTimesX = qCeil(kMinimumTiledSize / float64(width));
		auto repeatTimesY = qCeil(kMinimumTiledSize / float64(height));
		auto imageForTiled = QImage(width * repeatTimesX, height * repeatTimesY, QImage::Format_ARGB32_Premultiplied);
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

void ChatBackground::adjustPaletteUsingBackground(const QImage &img) {
	Assert(img.format() == QImage::Format_ARGB32_Premultiplied);

	uint64 components[3] = { 0 };
	uint64 componentsScroll[3] = { 0 };
	auto w = img.width();
	auto h = img.height();
	auto size = w * h;
	if (auto pix = img.constBits()) {
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

	auto bgColor = QColor(components[0], components[1], components[2]);
	auto hue = bgColor.hslHueF();
	auto saturation = bgColor.hslSaturationF();
	for (const auto &color : _adjustableColors) {
		adjustColor(color.item, hue, saturation);
	}
}

int32 ChatBackground::id() const {
	return _id;
}

bool ChatBackground::tile() const {
	return nightMode() ? _tileNightValue : _tileDayValue;
}

bool ChatBackground::tileDay() const {
	if (_id == internal::kTestingThemeBackground ||
		_id == internal::kTestingDefaultBackground) {
		if (!nightMode()) {
			return _tileForRevert;
		}
	}
	return _tileDayValue;
}

bool ChatBackground::tileNight() const {
	if (_id == internal::kTestingThemeBackground ||
		_id == internal::kTestingDefaultBackground) {
		if (nightMode()) {
			return _tileForRevert;
		}
	}
	return _tileNightValue;
}

void ChatBackground::ensureStarted() {
	if (_pixmap.isNull()) {
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
		if (_id != internal::kTestingThemeBackground
			&& _id != internal::kTestingDefaultBackground) {
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
	if (_id == internal::kTestingThemeBackground
		|| _id == internal::kTestingDefaultBackground) {
		if (_themeImage.isNull()) {
			_idForRevert = kDefaultBackground;
			_imageForRevert = QImage();
			_tileForRevert = false;
		} else {
			_idForRevert = kThemeBackground;
			_imageForRevert = _themeImage;
			_tileForRevert = _themeTile;
		}
	} else {
		setImage(kThemeBackground);
		restoreAdjustableColors();
		notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
		notify(BackgroundUpdate(BackgroundUpdate::Type::ApplyingTheme, tile()), true);
	}
}

void ChatBackground::saveForRevert() {
	ensureStarted();
	if (_id != internal::kTestingThemeBackground
		&& _id != internal::kTestingDefaultBackground) {
		_idForRevert = _id;
		_imageForRevert = std::move(_pixmap).toImage();
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
		|| (_id == kThemeBackground)
		|| (_id == kDefaultBackground
			&& !nightMode()
			&& _themeAbsolutePath.isEmpty());
	if (AreTestingTheme() && IsPaletteTestingPath(instance->applying.pathAbsolute)) {
		// Grab current background image if it is not already custom
		if (_id != kCustomBackground) {
			saveForRevert();
			setImage(internal::kTestingEditorBackground, std::move(_pixmap).toImage());
		}
	} else if (switchToThemeBackground) {
		saveForRevert();
		setImage(internal::kTestingThemeBackground, std::move(theme.background));
		setTile(theme.tiled);
	} else {
		// Apply current background image so that service bg colors are recounted.
		setImage(_id, std::move(_pixmap).toImage());
	}
	notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
}

void ChatBackground::setTestingDefaultTheme() {
	style::main_palette::reset();
	saveAdjustableColors();

	saveForRevert();
	setImage(internal::kTestingDefaultBackground);
	setTile(false);
	notify(BackgroundUpdate(BackgroundUpdate::Type::TestingTheme, tile()), true);
}

void ChatBackground::keepApplied(const QString &path, bool write) {
	setThemeAbsolutePath(path);
	if (_id == internal::kTestingEditorBackground) {
		_id = kCustomBackground;
		_themeImage = QImage();
		_themeTile = false;
		if (write) {
			writeNewBackgroundSettings();
		}
	} else if (_id == internal::kTestingThemeBackground) {
		_id = kThemeBackground;
		_themeImage = _pixmap.toImage();
		_themeTile = tile();
		if (write) {
			writeNewBackgroundSettings();
		}
	} else if (_id == internal::kTestingDefaultBackground) {
		_id = kDefaultBackground;
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
			|| _id != kThemeBackground)
		: (!_themeAbsolutePath.isEmpty()
			|| _id != kDefaultBackground);
}

bool ChatBackground::isNonDefaultBackground() {
	start();
	return _themeAbsolutePath.isEmpty()
		? (_id != kDefaultBackground)
		: (_id != kThemeBackground);
}

void ChatBackground::writeNewBackgroundSettings() {
	if (tile() != _tileForRevert) {
		Local::writeUserSettings();
	}
	Local::writeBackground(
		_id,
		((_id == kThemeBackground || _id == kDefaultBackground)
			? QImage()
			: _pixmap.toImage()));
}

void ChatBackground::revert() {
	if (_id == internal::kTestingThemeBackground
		|| _id == internal::kTestingDefaultBackground
		|| _id == internal::kTestingEditorBackground) {
		setTile(_tileForRevert);
		setImage(_idForRevert, std::move(_imageForRevert));
	} else {
		// Apply current background image so that service bg colors are recounted.
		setImage(_id, std::move(_pixmap).toImage());
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
		instance->applying.overrideKeep = [=] {
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
				setImage(kThemeBackground);
			}
		};
	}
}

ChatBackground *Background() {
	instance.createIfNull();
	return &instance->background;
}

bool Load(Saved &&saved) {
	if (saved.content.size() < 4) {
		LOG(("Theme Error: Could not load theme from '%1' (%2)"
			).arg(saved.pathRelative
			).arg(saved.pathAbsolute));
		return false;
	}

	instance.createIfNull();
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
	instance.clear();
}

bool Apply(const QString &filepath) {
	if (auto preview = PreviewFromFile(filepath)) {
		return Apply(std::move(preview));
	}
	return false;
}

bool Apply(std::unique_ptr<Preview> preview) {
	instance.createIfNull();
	instance->applying.pathRelative = std::move(preview->pathRelative);
	instance->applying.pathAbsolute = std::move(preview->pathAbsolute);
	instance->applying.content = std::move(preview->content);
	instance->applying.cached = std::move(preview->instance.cached);
	if (instance->applying.paletteForRevert.isEmpty()) {
		instance->applying.paletteForRevert = style::main_palette::save();
	}
	Background()->setTestingTheme(std::move(preview->instance));
	return true;
}

void ApplyDefault() {
	ApplyDefaultWithPath(IsNightMode() ? NightThemePath() : QString());
}

void ApplyDefaultWithPath(const QString &themePath) {
	if (!themePath.isEmpty()) {
		if (auto preview = PreviewFromFile(themePath)) {
			Apply(std::move(preview));
		}
	} else {
		instance.createIfNull();
		instance->applying.pathRelative = QString();
		instance->applying.pathAbsolute = QString();
		instance->applying.content = QByteArray();
		instance->applying.cached = Cached();
		if (instance->applying.paletteForRevert.isEmpty()) {
			instance->applying.paletteForRevert = style::main_palette::save();
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

	instance.createIfNull();
	instance->applying.pathRelative = path.isEmpty()
		? QString()
		: QDir().relativeFilePath(path);
	instance->applying.pathAbsolute = path.isEmpty()
		? QString()
		: QFileInfo(path).absoluteFilePath();
	instance->applying.content = content;
	instance->applying.cached = out.cached;
	if (instance->applying.paletteForRevert.isEmpty()) {
		instance->applying.paletteForRevert = style::main_palette::save();
	}
	Background()->setTestingTheme(std::move(out));
	KeepApplied();
	return true;
}

void KeepApplied() {
	if (!AreTestingTheme()) {
		return;
	} else if (instance->applying.overrideKeep) {
		// This callback will be destroyed while running.
		// And it won't be able to safely access captures after that.
		// So we save it on stack for the time while it is running.
		const auto saved = base::take(instance->applying.overrideKeep);
		saved();
		return;
	}
	const auto path = instance->applying.pathAbsolute;
	WriteAppliedTheme();
	ClearApplying();
	Background()->keepApplied(path, true);
}

void Revert() {
	if (!AreTestingTheme()) {
		return;
	}
	style::main_palette::load(instance->applying.paletteForRevert);
	Background()->saveAdjustableColors();

	ClearApplying();
	Background()->revert();
}

QString NightThemePath() {
	return str_const_toString(kNightThemeFile);
}

bool IsNonDefaultThemeOrBackground() {
	return Background()->isNonDefaultThemeOrBackground();
}

bool IsNonDefaultBackground() {
	return Background()->isNonDefaultBackground();
}

bool IsNightMode() {
	return instance ? Background()->nightMode() : false;
}

void SetNightModeValue(bool nightMode) {
	if (instance || nightMode) {
		Background()->setNightModeValue(nightMode);
	}
}

void ToggleNightMode() {
	Background()->toggleNightMode(std::nullopt);
}

void ToggleNightMode(const QString &path) {
	Background()->toggleNightMode(path);
}

bool SuggestThemeReset() {
	return IsNonDefaultThemeOrBackground();
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
