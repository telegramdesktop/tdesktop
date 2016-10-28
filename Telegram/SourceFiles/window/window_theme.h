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
#pragma once

namespace Window {
namespace Theme {
namespace internal {

constexpr int32 kUninitializedBackground = -3;

} // namespace internal

constexpr int32 kThemeBackground = -2;
constexpr int32 kCustomBackground = -1;
constexpr int32 kOldBackground = 0;
constexpr int32 kDefaultBackground = 21;

struct Cached {
	QByteArray colors;
	QByteArray background;
	bool tiled = false;
	int32 paletteChecksum = 0;
	int32 contentChecksum = 0;
};
bool Load(const QString &pathRelative, const QString &pathAbsolute, const QByteArray &content, Cached &cache);
void Unload();

struct Instance {
	style::palette palette;
	QImage background;
	Cached cached;
	bool tiled = false;
};
bool LoadFromFile(const QString &file, Instance *out, QByteArray *outContent);

struct BackgroundUpdate {
	enum class Type {
		New,
		Changed,
		Start,
	};

	BackgroundUpdate(Type type, bool tiled) : type(type), tiled(tiled) {
	}
	Type type;
	bool tiled;
};

class ChatBackground : public base::Observable<BackgroundUpdate> {
public:
	// This method is allowed to (and should) be called before start().
	void setThemeData(QImage &&themeImage, bool themeTile);

	// This method is setting the default (themed) image if none was set yet.
	void start();
	void setImage(int32 id, QImage &&image = QImage());
	void setTile(bool tile);
	void reset() {
		setImage(kThemeBackground);
	}

	int32 id() const;
	const QPixmap &image() const;
	bool tile() const;

private:
	void setPreparedImage(QImage &&image);

	int32 _id = internal::kUninitializedBackground;
	QPixmap _image;
	bool _tile = false;

	QImage _themeImage;
	bool _themeTile = false;

};

ChatBackground *Background();

} // namespace Theme
} // namespace Window
