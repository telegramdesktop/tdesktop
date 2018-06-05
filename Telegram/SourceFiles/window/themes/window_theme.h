/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
namespace Theme {
namespace internal {

constexpr int32 kUninitializedBackground = -999;
constexpr int32 kTestingThemeBackground = -666;
constexpr int32 kTestingDefaultBackground = -665;
constexpr int32 kTestingEditorBackground = -664;

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
	QImage preview;
};

bool Apply(const QString &filepath);
bool Apply(std::unique_ptr<Preview> preview);
void ApplyDefault();
bool ApplyEditedPalette(const QString &path, const QByteArray &content);
void KeepApplied();
bool IsNonDefaultUsed();
bool IsNightTheme();
void SwitchNightTheme(bool enabled);
void Revert();

bool LoadFromFile(const QString &file, Instance *out, QByteArray *outContent);
bool IsPaletteTestingPath(const QString &path);

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

	enum class ChangeMode {
		SwitchToThemeBackground,
		LeaveCurrentCustomBackground,
	};
	void setTestingTheme(Instance &&theme, ChangeMode mode = ChangeMode::SwitchToThemeBackground);
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

bool CopyColorsToPalette(const QString &path, const QByteArray &themeContent);

bool ReadPaletteValues(const QByteArray &content, Fn<bool(QLatin1String name, QLatin1String value)> callback);

} // namespace Theme
} // namespace Window
