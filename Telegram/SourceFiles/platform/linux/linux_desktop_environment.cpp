/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_desktop_environment.h"

#include "platform/linux/specific_linux.h"
#include "base/qt_adapters.h"

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
	auto list = xdgCurrentDesktop.split(':', base::QStringSkipEmptyParts);
	auto desktopSession = GetEnv("DESKTOP_SESSION").toLower();
	auto slash = desktopSession.lastIndexOf('/');
	auto kdeSession = GetEnv("KDE_SESSION_VERSION");

	// DESKTOP_SESSION can contain a path
	if (slash != -1) {
		desktopSession = desktopSession.mid(slash + 1);
	}

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
		} else if (list.contains("gnome")) {
			return Type::Gnome;
		} else if (list.contains("x-cinnamon")) {
			return Type::Cinnamon;
		} else if (list.contains("kde")) {
			if (kdeSession == qstr("5")) {
				return Type::KDE5;
			}
			return Type::KDE4;
		} else if (list.contains("mate")) {
			return Type::MATE;
		} else if (list.contains("lxde")) {
			return Type::LXDE;
		}
	}

	if (!desktopSession.isEmpty()) {
		if (desktopSession == qstr("gnome")) {
			return Type::Gnome;
		} else if (desktopSession == qstr("cinnamon")) {
			return Type::Cinnamon;
		} else if (desktopSession == qstr("kde4") || desktopSession == qstr("kde-plasma")) {
			return Type::KDE4;
		} else if (desktopSession == qstr("kde")) {
			// This may mean KDE4 on newer systems, so we have to check.
			if (!kdeSession.isEmpty()) {
				return Type::KDE4;
			}
			return Type::KDE3;
		} else if (desktopSession == qstr("xfce")) {
			return Type::XFCE;
		} else if (desktopSession == qstr("mate")) {
			return Type::MATE;
		} else if (desktopSession == qstr("lxde")) {
			return Type::LXDE;
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
		case Type::Cinnamon: return "Cinnamon";
		case Type::KDE3: return "KDE3";
		case Type::KDE4: return "KDE4";
		case Type::KDE5: return "KDE5";
		case Type::Unity: return "Unity";
		case Type::XFCE: return "XFCE";
		case Type::MATE: return "MATE";
		case Type::LXDE: return "LXDE";
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

} // namespace DesktopEnvironment
} // namespace Platform
