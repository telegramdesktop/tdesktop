/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
