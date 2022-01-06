/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_desktop_environment.h"

namespace Platform {
namespace DesktopEnvironment {
namespace {

QString GetEnv(const char *name) {
	const auto value = qEnvironmentVariable(name);
	LOG(("Getting DE, %1: '%2'").arg(name, value));
	return value;
}

std::vector<Type> Compute() {
	auto result = std::vector<Type>();

	const auto xdgCurrentDesktop = GetEnv(
		"XDG_CURRENT_DESKTOP").toLower().split(':', Qt::SkipEmptyParts);

	const auto desktopSession = [] {
		const auto result = GetEnv("DESKTOP_SESSION").toLower();
		const auto slash = result.lastIndexOf('/');
		// DESKTOP_SESSION can contain a path
		if (slash != -1) {
			return result.mid(slash + 1);
		}
		return result;
	}();

	for (const auto &current : xdgCurrentDesktop) {
		if (current == qstr("unity")) {
			// gnome-fallback sessions set XDG_CURRENT_DESKTOP to Unity
			// DESKTOP_SESSION can be gnome-fallback or gnome-fallback-compiz
			if (desktopSession.indexOf(qstr("gnome-fallback")) >= 0) {
				result.push_back(Type::Gnome);
			}
			result.push_back(Type::Unity);
		} else if (current == qstr("gnome")) {
			result.push_back(Type::Gnome);
		} else if (current == qstr("x-cinnamon")) {
			result.push_back(Type::Cinnamon);
		} else if (current == qstr("kde")) {
			result.push_back(Type::KDE);
		} else if (current == qstr("mate")) {
			result.push_back(Type::MATE);
		}
	}

	if (!desktopSession.isEmpty()) {
		if (desktopSession == qstr("gnome")) {
			result.push_back(Type::Gnome);
		} else if (desktopSession == qstr("cinnamon")) {
			result.push_back(Type::Cinnamon);
		} else if (desktopSession == qstr("mate")) {
			result.push_back(Type::MATE);
		}
	}

	// Fall back on some older environment variables.
	// Useful particularly in the DESKTOP_SESSION=default case.
	if (!GetEnv("GNOME_DESKTOP_SESSION_ID").isEmpty()) {
		result.push_back(Type::Gnome);
	}
	if (!GetEnv("KDE_FULL_SESSION").isEmpty()) {
		result.push_back(Type::KDE);
	}

	ranges::unique(result);
	return result;
}

std::vector<Type> ComputeAndLog() {
	const auto result = Compute();
	if (result.empty()) {
		return {};
	}
	const auto names = ranges::accumulate(
		result | ranges::views::transform([](auto type) {
			switch (type) {
			case Type::Gnome: return qsl("Gnome, ");
			case Type::Cinnamon: return qsl("Cinnamon, ");
			case Type::KDE: return qsl("KDE, ");
			case Type::Unity: return qsl("Unity, ");
			case Type::MATE: return qsl("MATE, ");
			}
			Unexpected("Type in Platform::DesktopEnvironment::ComputeAndLog");
		}),
		QString()).chopped(2);
	LOG(("DE: %1").arg(names));
	return result;
}

} // namespace

// Thanks Chromium.
std::vector<Type> Get() {
	static const auto result = ComputeAndLog();
	return result;
}

} // namespace DesktopEnvironment
} // namespace Platform
