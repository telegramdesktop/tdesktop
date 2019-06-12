/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/info_linux.h"

#include <QLocale>

namespace Platform {

QString DeviceModelPretty() {
#ifdef Q_OS_LINUX64
	return "PC 64bit";
#else // Q_OS_LINUX64
	return "PC 32bit";
#endif // Q_OS_LINUX64
}

QString SystemVersionPretty() {
	const auto result = getenv("XDG_CURRENT_DESKTOP");
	const auto value = result ? QString::fromLatin1(result) : QString();
	const auto list = value.split(':', QString::SkipEmptyParts);
	return list.isEmpty() ? "Linux" : "Linux " + list[0];
}

QString SystemCountry() {
	QLocale locale;
	return locale.name().split('_').last();
}

QString SystemLanguage() {
	QLocale locale;
	return locale.name().split('_').first();
}

QDate WhenSystemBecomesOutdated() {
	return QDate();
}

} // namespace Platform
