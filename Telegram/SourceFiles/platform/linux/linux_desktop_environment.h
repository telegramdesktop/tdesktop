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
	Cinnamon,
	KDE3,
	KDE4,
	KDE5,
	Unity,
	XFCE,
	MATE,
	LXDE,
};

Type Get();

inline bool IsGnome() {
	return Get() == Type::Gnome;
}

inline bool IsCinnamon() {
	return Get() == Type::Cinnamon;
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

inline bool IsMATE() {
	return Get() == Type::MATE;
}

inline bool IsLXDE() {
	return Get() == Type::LXDE;
}

inline bool IsGtkBased() {
	return IsGnome()
		|| IsCinnamon()
		|| IsUnity()
		|| IsMATE()
		|| IsXFCE()
		|| IsLXDE();
}

} // namespace DesktopEnvironment
} // namespace Platform
