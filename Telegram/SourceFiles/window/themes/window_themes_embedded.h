/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_keys.h"

class QImage;

namespace style {
struct colorizer;
} // namespace style

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

[[nodiscard]] style::colorizer ColorizerFrom(
	const EmbeddedScheme &scheme,
	const QColor &color);
[[nodiscard]] style::colorizer ColorizerForTheme(const QString &absolutePath);

void Colorize(
	EmbeddedScheme &scheme,
	const style::colorizer &colorizer);

[[nodiscard]] std::vector<EmbeddedScheme> EmbeddedThemes();
[[nodiscard]] std::vector<QColor> DefaultAccentColors(EmbeddedType type);

} // namespace Theme
} // namespace Window