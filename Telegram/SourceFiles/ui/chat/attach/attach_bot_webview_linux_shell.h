/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QColor>

namespace Ui::BotWebView::LinuxShell {

struct ResolvedColors {
	QColor titleBg;
	QColor bodyBg;
	QColor bottomBg;
};

#if !defined Q_OS_WIN && !defined Q_OS_MAC

[[nodiscard]] QByteArray InstallScript(const QString &shellToken);
[[nodiscard]] QByteArray MethodCallScript(
	const QByteArray &method,
	const QJsonObject &data,
	const QString &shellToken);
[[nodiscard]] QByteArray EventScript(
	const QString &event,
	const QJsonObject &data,
	const QString &shellToken);
[[nodiscard]] QJsonObject Metrics();
[[nodiscard]] QSize WindowSize(QSize contentSize);
[[nodiscard]] QJsonObject MenuPalette();
[[nodiscard]] QJsonObject ColorPayload(const ResolvedColors &colors);

#else // !Q_OS_WIN && !Q_OS_MAC

[[nodiscard]] inline QByteArray InstallScript(const QString &) {
	return {};
}

[[nodiscard]] inline QByteArray MethodCallScript(
		const QByteArray &,
		const QJsonObject &,
		const QString &) {
	return {};
}

[[nodiscard]] inline QByteArray EventScript(
		const QString &,
		const QJsonObject &,
		const QString &) {
	return {};
}

[[nodiscard]] inline QJsonObject Metrics() {
	return {};
}

[[nodiscard]] inline QSize WindowSize(QSize contentSize) {
	return contentSize;
}

[[nodiscard]] inline QJsonObject MenuPalette() {
	return {};
}

[[nodiscard]] inline QJsonObject ColorPayload(
		const ResolvedColors &) {
	return {};
}

#endif // Q_OS_WIN || Q_OS_MAC

} // namespace Ui::BotWebView::LinuxShell
