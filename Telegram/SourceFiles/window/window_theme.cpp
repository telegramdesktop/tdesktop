/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "window/window_theme.h"

#include "mainwidget.h"
#include "localstorage.h"
#include "core/parse_helper.h"
#include "core/zlib_help.h"

namespace Window {
namespace Theme {
namespace {

constexpr int kThemeFileSizeLimit = 5 * 1024 * 1024;
constexpr int kThemeBackgroundSizeLimit = 4 * 1024 * 1024;
constexpr int kThemeSchemeSizeLimit = 1024 * 1024;

struct Data {
	ChatBackground background;
};
NeverFreedPointer<Data> instance;

QByteArray readThemeContent(const QString &path) {
	QFile file(path);
	if (!file.exists()) {
		LOG(("Error: theme file not found: %1").arg(path));
		return QByteArray();
	}

	if (file.size() > kThemeFileSizeLimit) {
		LOG(("Error: theme file too large: %1 (should be less than 5 MB, got %2)").arg(path).arg(file.size()));
		return QByteArray();
	}
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Warning: could not open theme file: %1").arg(path));
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

bool readNameAndValue(const char *&from, const char *end, QByteArray *outName, QByteArray *outValue) {
	using base::parse::skipWhitespaces;
	using base::parse::readName;

	if (!skipWhitespaces(from, end)) return true;

	*outName = readName(from, end);
	if (outName->isEmpty()) {
		LOG(("Error: Could not read name in the color scheme."));
		return false;
	}
	if (!skipWhitespaces(from, end)) {
		LOG(("Error: Unexpected end of the color scheme."));
		return false;
	}
	if (*from != ':') {
		LOG(("Error: Expected ':' between each name and value in the color scheme."));
		return false;
	}
	if (!skipWhitespaces(++from, end)) {
		LOG(("Error: Unexpected end of the color scheme."));
		return false;
	}
	auto valueStart = from;
	if (*from == '#') ++from;

	if (readName(from, end).isEmpty()) {
		LOG(("Error: Expected a color value in #rrggbb or #rrggbbaa format in the color scheme."));
		return false;
	}
	*outValue = QByteArray::fromRawData(valueStart, from - valueStart);

	if (!skipWhitespaces(from, end)) {
		LOG(("Error: Unexpected end of the color scheme."));
		return false;
	}
	if (*from != ';') {
		LOG(("Error: Expected ';' after each value in the color scheme."));
		return false;
	}
	++from;
	return true;
}

bool loadColorScheme(const QByteArray &content, Instance *out = nullptr) {
	if (content.size() > kThemeSchemeSizeLimit) {
		LOG(("Error: color scheme file too large (should be less than 1 MB, got %2)").arg(content.size()));
		return false;
	}

	auto data = base::parse::stripComments(content);
	auto from = data.constData(), end = from + data.size();
	while (from != end) {
		QByteArray name, value;
		if (!readNameAndValue(from, end, &name, &value)) {
			return false;
		}
		if (name.isEmpty()) { // End of content reached.
			return true;
		}

		auto size = value.size();
		auto error = false;
		if (value[0] == '#' && (size == 7 || size == 8)) {
			auto r = readHexUchar(value[1], value[2], error);
			auto g = readHexUchar(value[3], value[4], error);
			auto b = readHexUchar(value[5], value[6], error);
			auto a = (size == 8) ? readHexUchar(value[7], value[8], error) : uchar(255);
			if (!error) {
				if (out) {
					error = !out->palette.setColor(QLatin1String(name), r, g, b, a);
				} else {
					error = !style::main_palette::setColor(QLatin1String(name), r, g, b, a);
				}
			}
		} else {
			if (out) {
				error = !out->palette.setColor(QLatin1String(name), QLatin1String(value));
			} else {
				error = !style::main_palette::setColor(QLatin1String(name), QLatin1String(value));
			}
		}
		if (error) {
			LOG(("Error: Expected a color value in #rrggbb or #rrggbbaa format in the color scheme."));
			return false;
		}
	}
	return true;
}

void applyBackground(QImage &&background, bool tiled, Instance *out) {
	if (out) {
		out->background = std_::move(background);
		out->tiled = tiled;
	} else {
		Background()->setThemeData(std_::move(background), tiled);
	}
}

bool loadThemeFromCache(const QByteArray &content, Cached &cache) {
	if (cache.paletteChecksum != style::palette::kChecksum) {
		return false;
	}
	if (cache.contentChecksum != hashCrc32(content.constData(), content.size())) {
		return false;
	}

	QImage background;
	if (!cache.background.isEmpty()) {
		QBuffer buffer(&cache.background);
		QImageReader reader(&buffer);
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
	if (!background.isNull()) {
		applyBackground(std_::move(background), cache.tiled, nullptr);
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
	LOG(("Error: could not read '%1' in the theme file.").arg(filename));
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
		if (file.error() != UNZ_OK) {
			LOG(("Error: could not read 'colors.tdesktop-theme' in the theme file."));
			return false;
		}
		if (!loadColorScheme(schemeContent, out)) {
			return false;
		}

		auto backgroundTiled = false;
		auto backgroundContent = QByteArray();
		if (!loadBackground(file, &backgroundContent, &backgroundTiled)) {
			return false;
		}

		if (!backgroundContent.isEmpty()) {
			auto background = App::readImage(backgroundContent);
			if (background.isNull()) {
				LOG(("Error: could not read background image in the theme file."));
				return false;
			}
			QBuffer buffer(&cache.background);
			if (!background.save(&buffer, "BMP")) {
				LOG(("Error: could not write background image as a BMP to cache."));
				return false;
			}
			cache.tiled = backgroundTiled;

			applyBackground(std_::move(background), cache.tiled, out);
		}
	} else {
		// Looks like it is not a .zip theme.
		if (!loadColorScheme(content, out)) {
			return false;
		}
	}
	if (out) {
		cache.colors = out->palette.save();
	} else {
		cache.colors = style::main_palette::save();
	}
	cache.paletteChecksum = style::palette::kChecksum;
	cache.contentChecksum = hashCrc32(content.constData(), content.size());

	return true;
}

QImage prepareBackgroundImage(QImage &&image) {
	if (image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_ARGB32_Premultiplied && image.format() != QImage::Format_RGB32) {
		image = std_::move(image).convertToFormat(QImage::Format_RGB32);
	}
	image.setDevicePixelRatio(cRetinaFactor());
	return std_::move(image);
}

} // namespace

void ChatBackground::setThemeData(QImage &&themeImage, bool themeTile) {
	_themeImage = prepareBackgroundImage(std_::move(themeImage));
	_themeTile = themeTile;
}

void ChatBackground::start() {
	if (_id == internal::kUninitializedBackground) {
		setImage(kThemeBackground);
	}
}

void ChatBackground::setImage(int32 id, QImage &&image) {
	if (id == kThemeBackground && _themeImage.isNull()) {
		id = kDefaultBackground;
	}
	_id = id;
	if (_id == kThemeBackground) {
		_tile = _themeTile;
		setPreparedImage(QImage(_themeImage));
	} else {
		if (_id == kDefaultBackground) {
			image.load(qsl(":/gui/art/bg.jpg"));
		} else if (_id == kOldBackground || image.isNull()) {
			_id = kOldBackground;
			image.load(qsl(":/gui/art/bg_old.png"));
			if (cRetina()) {
				image = image.scaledToWidth(image.width() * 2, Qt::SmoothTransformation);
			} else if (cScale() != dbisOne) {
				image = image.scaledToWidth(convertScale(image.width()), Qt::SmoothTransformation);
			}
		}
		Local::writeBackground(_id, (_id == kDefaultBackground || _id == kOldBackground) ? QImage() : image);
		setPreparedImage(prepareBackgroundImage(std_::move(image)));
	}
	t_assert(!_image.isNull());
	notify(BackgroundUpdate(BackgroundUpdate::Type::New, _tile));
}

void ChatBackground::setPreparedImage(QImage &&image) {
	App::initColorsFromBackground(image);
	_image = App::pixmapFromImageInPlace(std_::move(image));
}

int32 ChatBackground::id() const {
	return _id;
}

const QPixmap &ChatBackground::image() const {
	return _image;
}

bool ChatBackground::tile() const {
	return _tile;
}

void ChatBackground::setTile(bool tile) {
	if (_image.isNull()) {
		// We should start first, otherwise the default call
		// to start() will reset this value to _themeTile.
		start();
	}
	if (_tile != tile) {
		_tile = tile;
		Local::writeUserSettings();
		notify(BackgroundUpdate(BackgroundUpdate::Type::Changed, _tile));
	}
}

ChatBackground *Background() {
	instance.createIfNull();
	return &instance->background;
}

bool Load(const QString &pathRelative, const QString &pathAbsolute, const QByteArray &content, Cached &cache) {
	if (content.size() < 4) {
		LOG(("Error: Could not load theme from '%1' (%2)").arg(pathRelative).arg(pathAbsolute));
		return false;
	}

	instance.createIfNull();
	if (loadThemeFromCache(content, cache)) {
		return true;
	}

	if (!loadTheme(content, cache)) {
		return false;
	}
	Local::writeTheme(pathRelative, pathAbsolute, content, cache);
	return true;
}

void Unload() {
	instance.clear();
}

bool LoadFromFile(const QString &path, Instance *out, QByteArray *outContent) {
	*outContent = readThemeContent(path);
	if (outContent->size() < 4) {
		LOG(("Error: Could not load theme from %1").arg(path));
		return false;
	}

	return loadTheme(*outContent,  out->cached, out);
}

} // namespace Theme
} // namespace Window
