/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_desktop_environment.h"

#include <QDBusInterface>

namespace Platform {
namespace DesktopEnvironment {
namespace {

QString GetEnv(const char *name) {
	auto result = getenv(name);
	auto value = result ? QString::fromLatin1(result) : QString();
	LOG(("Getting DE, %1: '%2'").arg(name).arg(value));
	return value;
}

Type Compute() {
	auto xdgCurrentDesktop = GetEnv("XDG_CURRENT_DESKTOP").toLower();
	auto list = xdgCurrentDesktop.split(':', QString::SkipEmptyParts);
	auto desktopSession = GetEnv("DESKTOP_SESSION").toLower();
	auto kdeSession = GetEnv("KDE_SESSION_VERSION");
	if (!list.isEmpty()) {
		if (list.contains("unity")) {
			// gnome-fallback sessions set XDG_CURRENT_DESKTOP to Unity
			// DESKTOP_SESSION can be gnome-fallback or gnome-fallback-compiz
			if (desktopSession.indexOf(qstr("gnome-fallback")) >= 0) {
				return Type::Gnome;
			}
			return Type::Unity;
		} else if (list.contains("xfce")) {
			return Type::XFCE;
		} else if (list.contains("pantheon")) {
			return Type::Pantheon;
		} else if (list.contains("gnome")) {
			if (list.contains("ubuntu"))
				return Type::Ubuntu;

			return Type::Gnome;
		} else if (list.contains("kde")) {
			if (kdeSession == qstr("5")) {
				return Type::KDE5;
			}
			return Type::KDE4;
		}
	}

	if (!desktopSession.isEmpty()) {
		if (desktopSession == qstr("gnome") || desktopSession == qstr("mate")) {
			return Type::Gnome;
		} else if (desktopSession == qstr("kde4") || desktopSession == qstr("kde-plasma")) {
			return Type::KDE4;
		} else if (desktopSession == qstr("kde")) {
			// This may mean KDE4 on newer systems, so we have to check.
			if (!kdeSession.isEmpty()) {
				return Type::KDE4;
			}
			return Type::KDE3;
		} else if (desktopSession.indexOf(qstr("xfce")) >= 0 || desktopSession == qstr("xubuntu")) {
			return Type::XFCE;
		} else if (desktopSession == qstr("awesome")) {
			return Type::Awesome;
		}
	}

	// Fall back on some older environment variables.
	// Useful particularly in the DESKTOP_SESSION=default case.
	if (!GetEnv("GNOME_DESKTOP_SESSION_ID").isEmpty()) {
		return Type::Gnome;
	} else if (!GetEnv("KDE_FULL_SESSION").isEmpty()) {
		if (!kdeSession.isEmpty()) {
			return Type::KDE4;
		}
		return Type::KDE3;
	}

	return Type::Other;
}

Type ComputeAndLog() {
	auto result = Compute();
	auto name = [result]() -> QString {
		switch (result) {
		case Type::Other: return "Other";
		case Type::Gnome: return "Gnome";
		case Type::KDE3: return "KDE3";
		case Type::KDE4: return "KDE4";
		case Type::KDE5: return "KDE5";
		case Type::Ubuntu: return "Ubuntu";
		case Type::Unity: return "Unity";
		case Type::XFCE: return "XFCE";
		case Type::Pantheon: return "Pantheon";
		case Type::Awesome: return "Awesome";
		}
		return QString::number(static_cast<int>(result));
	};
	LOG(("DE: %1").arg(name()));
	return result;
}

} // namespace

// Thanks Chromium.
Type Get() {
	static const auto result = ComputeAndLog();
	return result;
}

bool TryQtTrayIcon() {
	return !IsPantheon() && !IsAwesome();
}

bool PreferAppIndicatorTrayIcon() {
	return IsXFCE() || IsUnity() || IsUbuntu() ||
	       (IsGnome() && QDBusInterface("org.kde.StatusNotifierWatcher", "/").isValid());
}

bool TryUnityCounter() {
	return IsUnity() || IsPantheon() || IsUbuntu();
}

} // namespace DesktopEnvironment
} // namespace Platform
