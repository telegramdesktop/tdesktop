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

namespace Platform {
namespace DesktopEnvironment {

enum class Type {
	Other,
	Gnome,
	KDE3,
	KDE4,
	KDE5,
	Unity,
	XFCE,
	Pantheon,
	Awesome,
};

Type Get();

inline bool IsGnome() {
	return Get() == Type::Gnome;
}

inline bool IsKDE3() {
	return Get() == Type::KDE3;
}

inline bool IsKDE4() {
	return Get() == Type::KDE4;
}

inline bool IsKDE5() {
	return Get() == Type::KDE5;
}

inline bool IsKDE() {
	return IsKDE3() || IsKDE4() || IsKDE5();
}

inline bool IsUnity() {
	return Get() == Type::Unity;
}

inline bool IsXFCE() {
	return Get() == Type::XFCE;
}

inline bool IsPantheon() {
	return Get() == Type::Pantheon;
}

inline bool IsAwesome() {
	return Get() == Type::Awesome;
}

bool TryQtTrayIcon();
bool PreferAppIndicatorTrayIcon();
bool TryUnityCounter();

} // namespace DesktopEnvironment
} // namespace Platform
