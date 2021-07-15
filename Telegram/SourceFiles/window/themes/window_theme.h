/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_wall_paper.h"
#include "data/data_cloud_themes.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class Controller;
} // namespace Window

namespace Window {
namespace Theme {

inline constexpr auto kThemeSchemeSizeLimit = 1024 * 1024;
inline constexpr auto kThemeBackgroundSizeLimit = 4 * 1024 * 1024;

struct ParsedTheme;

[[nodiscard]] bool IsEmbeddedTheme(const QString &path);

struct Object {
	QString pathRelative;
	QString pathAbsolute;
	QByteArray content;
	Data::CloudTheme cloud;
};
struct Cached {
	QByteArray colors;
	QByteArray background;
	bool tiled = false;
	int32 paletteChecksum = 0;
	int32 contentChecksum = 0;
};
struct Saved {
	Object object;
	Cached cache;
};
bool Initialize(Saved &&saved);
void Uninitialize();

struct Instance {
	style::palette palette;
	QImage background;
	Cached cached;
	bool tiled = false;
};

struct Preview {
	Object object;
	Instance instance;
	QImage preview;
};

bool Apply(
	const QString &filepath,
	const Data::CloudTheme &cloud = Data::CloudTheme());
bool Apply(std::unique_ptr<Preview> preview);
void ApplyDefaultWithPath(const QString &themePath);
bool ApplyEditedPalette(const QByteArray &content);
void KeepApplied();
void KeepFromEditor(
	const QByteArray &originalContent,
	const ParsedTheme &originalParsed,
	const Data::CloudTheme &cloud,
	const QByteArray &themeContent,
	const ParsedTheme &themeParsed,
	const QImage &background);
QString NightThemePath();
[[nodiscard]] bool IsNightMode();
void SetNightModeValue(bool nightMode);
void ToggleNightMode();
void ToggleNightMode(const QString &themePath);
void ToggleNightModeWithConfirmation(
	not_null<Controller*> window,
	Fn<void()> toggle);
void ResetToSomeDefault();
[[nodiscard]] bool IsNonDefaultBackground();
void Revert();

[[nodiscard]] QString EditingPalettePath();

bool LoadFromFile(
	const QString &file,
	not_null<Instance*> out,
	Cached *outCache,
	not_null<QByteArray*> outContent);
bool LoadFromContent(
	const QByteArray &content,
	not_null<Instance*> out,
	Cached *outCache);
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
		ApplyingEdit,
	};

	BackgroundUpdate(Type type, bool tiled) : type(type), tiled(tiled) {
	}
	[[nodiscard]] bool paletteChanged() const {
		return (type == Type::TestingTheme)
			|| (type == Type::RevertingTheme)
			|| (type == Type::ApplyingEdit)
			|| (type == Type::New);
	}
	Type type;
	bool tiled;
};

enum class ClearEditing {
	Temporary,
	RevertChanges,
	KeepChanges,
};

class ChatBackground final {
public:
	ChatBackground();

	[[nodiscard]] rpl::producer<BackgroundUpdate> updates() const {
		return _updates.events();
	}

	void start();

	// This method is allowed to (and should) be called before start().
	void setThemeData(QImage &&themeImage, bool themeTile);

	// This method is setting the default (themed) image if none was set yet.
	void set(const Data::WallPaper &paper, QImage image = QImage());
	void setTile(bool tile);
	void setTileDayValue(bool tile);
	void setTileNightValue(bool tile);
	void setThemeObject(const Object &object);
	[[nodiscard]] const Object &themeObject() const;
	[[nodiscard]] std::optional<Data::CloudTheme> editingTheme() const;
	void setEditingTheme(const Data::CloudTheme &editing);
	void clearEditingTheme(ClearEditing clear = ClearEditing::Temporary);
	void reset();

	void setTestingTheme(Instance &&theme);
	void saveAdjustableColors();
	void setTestingDefaultTheme();
	void revert();

	void appliedEditedPalette();
	void downloadingStarted(bool tile);

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
	[[nodiscard]] bool nightModeChangeAllowed() const;

private:
	struct AdjustableColor {
		AdjustableColor(style::color data);

		style::color item;
		QColor original;
	};

	[[nodiscard]] bool started() const;
	void initialRead();
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
	void reapplyWithNightMode(
		std::optional<QString> themePath,
		bool newNightMode);
	void keepApplied(const Object &object, bool write);
	[[nodiscard]] bool isNonDefaultThemeOrBackground();
	[[nodiscard]] bool isNonDefaultBackground();
	void checkUploadWallPaper();

	friend bool IsNightMode();
	friend void SetNightModeValue(bool nightMode);
	friend void ToggleNightMode();
	friend void ToggleNightMode(const QString &themePath);
	friend void ResetToSomeDefault();
	friend void KeepApplied();
	friend void KeepFromEditor(
		const QByteArray &originalContent,
		const ParsedTheme &originalParsed,
		const Data::CloudTheme &cloud,
		const QByteArray &themeContent,
		const ParsedTheme &themeParsed,
		const QImage &background);
	friend bool IsNonDefaultBackground();

	Main::Session *_session = nullptr;
	rpl::event_stream<BackgroundUpdate> _updates;
	Data::WallPaper _paper = Data::details::UninitializedWallPaper();
	std::optional<QColor> _paperColor;
	QImage _original;
	QPixmap _pixmap;
	QPixmap _pixmapForTiled;
	bool _nightMode = false;
	bool _tileDayValue = false;
	bool _tileNightValue = true;
	std::optional<bool> _localStoredTileDayValue;
	std::optional<bool> _localStoredTileNightValue;

	bool _isMonoColorImage = false;

	Object _themeObject;
	QImage _themeImage;
	bool _themeTile = false;
	std::optional<Data::CloudTheme> _editingTheme;

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

[[nodiscard]] ChatBackground *Background();

void ComputeBackgroundRects(QRect wholeFill, QSize imageSize, QRect &to, QRect &from);

bool ReadPaletteValues(const QByteArray &content, Fn<bool(QLatin1String name, QLatin1String value)> callback);

} // namespace Theme
} // namespace Window
