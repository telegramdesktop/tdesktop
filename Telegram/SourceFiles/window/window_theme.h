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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Window {
namespace Theme {
namespace internal {

constexpr int32 kUninitializedBackground = -999;
constexpr int32 kTestingThemeBackground = -666;
constexpr int32 kTestingDefaultBackground = -665;

} // namespace internal

constexpr int32 kThemeBackground = -2;
constexpr int32 kCustomBackground = -1;
constexpr int32 kInitialBackground = 0;
constexpr int32 kDefaultBackground = 105;

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

struct Preview {
	QString path;
	Instance instance;
	QByteArray content;
	QPixmap preview;
};

bool Apply(const QString &filepath);
bool Apply(std_::unique_ptr<Preview> preview);
void ApplyDefault();
void KeepApplied();
void Revert();

bool LoadFromFile(const QString &file, Instance *out, QByteArray *outContent);

struct BackgroundUpdate {
	enum class Type {
		New,
		Changed,
		Start,
		TestingTheme,
		RevertingTheme,
		ApplyingTheme,
	};

	BackgroundUpdate(Type type, bool tiled) : type(type), tiled(tiled) {
	}
	bool paletteChanged() const {
		return (type == Type::TestingTheme || type == Type::RevertingTheme);
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
	void reset();

	void setTestingTheme(Instance &&theme);
	void setTestingDefaultTheme();
	void keepApplied();
	void revert();

	int32 id() const;
	const QPixmap &pixmap() const {
		return _pixmap;
	}
	const QPixmap &pixmapForTiled() const {
		return _pixmapForTiled;
	}
	bool tile() const;
	bool tileForSave() const;

private:
	void ensureStarted();
	void saveForRevert();
	void setPreparedImage(QImage &&image);
	void writeNewBackgroundSettings();

	int32 _id = internal::kUninitializedBackground;
	QPixmap _pixmap;
	QPixmap _pixmapForTiled;
	bool _tile = false;

	QImage _themeImage;
	bool _themeTile = false;

	int32 _idForRevert = internal::kUninitializedBackground;
	QImage _imageForRevert;
	bool _tileForRevert = false;

};

ChatBackground *Background();

void ComputeBackgroundRects(QRect wholeFill, QSize imageSize, QRect &to, QRect &from);

} // namespace Theme
} // namespace Window
