/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_wall_paper.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
namespace Theme {

constexpr auto kThemeSchemeSizeLimit = 1024 * 1024;

struct Cached {
	QByteArray colors;
	QByteArray background;
	bool tiled = false;
	int32 paletteChecksum = 0;
	int32 contentChecksum = 0;
};
struct Saved {
	QString pathRelative;
	QString pathAbsolute;
	QByteArray content;
	Cached cache;
};
bool Load(Saved &&saved);
void Unload();

struct Instance {
	style::palette palette;
	QImage background;
	Cached cached;
	bool tiled = false;
};

struct Preview {
	QString pathRelative;
	QString pathAbsolute;
	Instance instance;
	QByteArray content;
	QImage preview;
};

bool Apply(const QString &filepath);
bool Apply(std::unique_ptr<Preview> preview);
void ApplyDefaultWithPath(const QString &themePath);
bool ApplyEditedPalette(const QString &path, const QByteArray &content);
void KeepApplied();
QString NightThemePath();
[[nodiscard]] bool IsNightMode();
void SetNightModeValue(bool nightMode);
void ToggleNightMode();
void ToggleNightMode(const QString &themePath);
[[nodiscard]] bool IsNonDefaultBackground();
void Revert();

bool LoadFromFile(
	const QString &file,
	Instance *out,
	QByteArray *outContent);
bool IsPaletteTestingPath(const QString &path);
QColor CountAverageColor(const QImage &image);
QColor AdjustedColor(QColor original, QColor background);
QImage ProcessBackgroundImage(QImage image);

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

class ChatBackground
	: public base::Observable<BackgroundUpdate>
	, private base::Subscriber {
public:
	ChatBackground();

	// This method is allowed to (and should) be called before start().
	void setThemeData(QImage &&themeImage, bool themeTile);

	// This method is setting the default (themed) image if none was set yet.
	void start();
	void set(const Data::WallPaper &paper, QImage image = QImage());
	void setTile(bool tile);
	void setTileDayValue(bool tile);
	void setTileNightValue(bool tile);
	void setThemeAbsolutePath(const QString &path);
	[[nodiscard]] QString themeAbsolutePath() const;
	void reset();

	void setTestingTheme(Instance &&theme);
	void saveAdjustableColors();
	void setTestingDefaultTheme();
	void revert();

	[[nodiscard]] Data::WallPaper paper() const {
		return _paper;
	}
	[[nodiscard]] WallPaperId id() const {
		return _paper.id();
	}
	[[nodiscard]] const QPixmap &pixmap() const {
		return _pixmap;
	}
	[[nodiscard]] const QPixmap &pixmapForTiled() const {
		return _pixmapForTiled;
	}
	[[nodiscard]] std::optional<QColor> colorForFill() const;
	[[nodiscard]] QImage createCurrentImage() const;
	[[nodiscard]] bool tile() const;
	[[nodiscard]] bool tileDay() const;
	[[nodiscard]] bool tileNight() const;
	[[nodiscard]] bool isMonoColorImage() const;

private:
	struct AdjustableColor {
		AdjustableColor(style::color data);

		style::color item;
		QColor original;
	};

	void ensureStarted();
	void saveForRevert();
	void setPreparedImage(QImage original, QImage prepared);
	void preparePixmaps(QImage image);
	void writeNewBackgroundSettings();
	void setPaper(const Data::WallPaper &paper);

	[[nodiscard]] bool adjustPaletteRequired();
	void adjustPaletteUsingBackground(const QImage &image);
	void adjustPaletteUsingColor(QColor color);
	void restoreAdjustableColors();

	void setNightModeValue(bool nightMode);
	[[nodiscard]] bool nightMode() const;
	void toggleNightMode(std::optional<QString> themePath);
	void keepApplied(const QString &path, bool write);
	[[nodiscard]] bool isNonDefaultThemeOrBackground();
	[[nodiscard]] bool isNonDefaultBackground();
	void checkUploadWallPaper();
	[[nodiscard]] bool testingPalette() const;

	friend bool IsNightMode();
	friend void SetNightModeValue(bool nightMode);
	friend void ToggleNightMode();
	friend void ToggleNightMode(const QString &themePath);
	friend void KeepApplied();
	friend bool IsNonDefaultBackground();

	Main::Session *_session = nullptr;
	Data::WallPaper _paper = Data::details::UninitializedWallPaper();
	std::optional<QColor> _paperColor;
	QImage _original;
	QPixmap _pixmap;
	QPixmap _pixmapForTiled;
	bool _nightMode = false;
	bool _tileDayValue = false;
	bool _tileNightValue = true;

	bool _isMonoColorImage = false;

	QString _themeAbsolutePath;
	QImage _themeImage;
	bool _themeTile = false;

	Data::WallPaper _paperForRevert
		= Data::details::UninitializedWallPaper();
	QImage _originalForRevert;
	bool _tileForRevert = false;

	std::vector<AdjustableColor> _adjustableColors;
	FullMsgId _wallPaperUploadId;
	mtpRequestId _wallPaperRequestId = 0;
	rpl::lifetime _wallPaperUploadLifetime;

	rpl::lifetime _lifetime;

};

ChatBackground *Background();

void ComputeBackgroundRects(QRect wholeFill, QSize imageSize, QRect &to, QRect &from);

bool CopyColorsToPalette(
	const QString &destination,
	const QString &themePath,
	const QByteArray &themeContent);

bool ReadPaletteValues(const QByteArray &content, Fn<bool(QLatin1String name, QLatin1String value)> callback);

} // namespace Theme
} // namespace Window
