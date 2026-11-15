/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct colorizer;
class palette;
} // namespace style

namespace Ui {
struct ChatThemeBackground;
} // namespace Ui

namespace Window {
namespace Theme {

enum class EmbeddedType {
	DayBlue,
	Default,
	Night,
	NightGreen,
};

struct EmbeddedScheme {
	EmbeddedType type = EmbeddedType();
	QColor background;
	QColor sent;
	QColor received;
	QColor radiobuttonInactive;
	QColor radiobuttonActive;
	rpl::producer<QString> name;
	QString path;
	QColor accentColor;
};

class AccentColors final {
public:
	[[nodiscard]] QByteArray serialize() const;
	bool setFromSerialized(const QByteArray &serialized);

	void set(EmbeddedType type, const QColor &value);
	void clear(EmbeddedType type);
	[[nodiscard]] std::optional<QColor> get(EmbeddedType type) const;

private:
	base::flat_map<EmbeddedType, QColor> _data;

};

[[nodiscard]] style::colorizer ColorizerFrom(
	const EmbeddedScheme &scheme,
	const QColor &color);
void ApplyColorizerAccent(style::colorizer &colorizer, const QColor &color);
[[nodiscard]] std::optional<QColor> SystemAccentColor();
[[nodiscard]] style::colorizer ColorizerForTheme(const QString &absolutePath);

void Colorize(
	EmbeddedScheme &scheme,
	const style::colorizer &colorizer);

[[nodiscard]] std::vector<EmbeddedScheme> EmbeddedThemes();
[[nodiscard]] std::vector<QColor> DefaultAccentColors(EmbeddedType type);

[[nodiscard]] Fn<void(style::palette&)> PreparePaletteCallback(
	bool dark,
	std::optional<QColor> accent);
[[nodiscard]] Fn<void(style::palette&)> PrepareCurrentPaletteCallback();
[[nodiscard]] std::unique_ptr<style::palette> PrepareEmbeddedPalette(
	EmbeddedType type,
	std::optional<QColor> accent);

class EmbeddedAccentRecipe final {
public:
	explicit EmbeddedAccentRecipe(EmbeddedType type);

	bool apply(style::palette &palette, const QColor &accent) const;

private:
	struct Leaf {
		QLatin1String rule;
		QColor raw;
	};
	struct Key {
		QLatin1String name;
		int index = 0;
		int leaf = 0;
	};
	struct Operand {
		QColor fixed;
		int leaf = -1;
	};
	struct Contrast {
		QLatin1String name;
		int index = 0;
		Operand over;
		Operand under;
	};

	EmbeddedScheme _scheme;
	std::vector<Leaf> _leaves;
	std::vector<Key> _keys;
	std::vector<Contrast> _contrast;

};

[[nodiscard]] const EmbeddedAccentRecipe &AccentRecipe(EmbeddedType type);

// Chat background paired with the PreparePaletteCallback palette.
[[nodiscard]] Ui::ChatThemeBackground PrepareDefaultBackground(bool dark);

} // namespace Theme
} // namespace Window
