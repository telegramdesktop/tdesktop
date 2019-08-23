/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/themes/window_themes_embedded.h"

namespace Core {

class Settings final {
public:
	void moveFrom(Settings &&other) {
		_variables = std::move(other._variables);
	}
	[[nodiscard]] QByteArray serialize() const;
	void constructFromSerialized(const QByteArray &serialized);

	void setThemesAccentColors(Window::Theme::AccentColors &&colors) {
		_variables.themesAccentColors = std::move(colors);
	}
	[[nodiscard]] Window::Theme::AccentColors &themesAccentColors() {
		return _variables.themesAccentColors;
	}

private:
	struct Variables {
		Variables();

		Window::Theme::AccentColors themesAccentColors;
	};

	Variables _variables;

};

} // namespace Core
