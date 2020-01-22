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
	Ubuntu,
	Unity,
	Pantheon,
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

inline bool IsUbuntu() {
	return Get() == Type::Ubuntu;
}

inline bool IsUnity() {
	return Get() == Type::Unity;
}

inline bool IsPantheon() {
	return Get() == Type::Pantheon;
}

bool TryQtTrayIcon();
bool PreferAppIndicatorTrayIcon();

} // namespace DesktopEnvironment
} // namespace Platform
