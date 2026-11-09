/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace twidget {
class IntlProvider;
} // namespace twidget

namespace Platform {

[[nodiscard]] std::shared_ptr<twidget::IntlProvider> CreateSystemIntl(
	const QString &fallbackLocale);

} // namespace Platform

#ifndef Q_OS_WIN

namespace Platform {

inline std::shared_ptr<twidget::IntlProvider> CreateSystemIntl(
		const QString &fallbackLocale) {
	return nullptr;
}

} // namespace Platform

#endif // !Q_OS_WIN
