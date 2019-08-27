/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_keys.h"

class QImage;

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
	tr::phrase<> name;
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

struct Colorizer {
	struct Color {
		int hue = 0;
		int saturation = 0;
		int value = 0;
	};
	int hueThreshold = 0;
	int lightnessMin = 0;
	int lightnessMax = 255;
	Color was;
	Color now;
	base::flat_set<QLatin1String> ignoreKeys;
	base::flat_map<QLatin1String, std::pair<Color, Color>> keepContrast;

	explicit operator bool() const {
		return (hueThreshold > 0);
	}
};

[[nodiscard]] Colorizer ColorizerFrom(
	const EmbeddedScheme &scheme,
	const QColor &color);
[[nodiscard]] Colorizer ColorizerForTheme(const QString &absolutePath);

void Colorize(uchar &r, uchar &g, uchar &b, const Colorizer &colorizer);
void Colorize(
	QLatin1String name,
	uchar &r,
	uchar &g,
	uchar &b,
	const Colorizer &colorizer);
void Colorize(QImage &image, const Colorizer &colorizer);
void Colorize(EmbeddedScheme &scheme, const Colorizer &colorizer);
[[nodiscard]] QByteArray Colorize(
	QLatin1String hexColor,
	const Colorizer &colorizer);

[[nodiscard]] std::vector<EmbeddedScheme> EmbeddedThemes();
[[nodiscard]] std::vector<QColor> DefaultAccentColors(EmbeddedType type);

} // namespace Theme
} // namespace Window