/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace internal {

class GtkIntegration {
public:
	enum class Type {
		Base,
		Webview,
	};

	static QString AllowedBackends();

	static int Exec(
		Type type,
		const QString &parentDBusName,
		const QString &serviceName);

	static void Start(Type type);

	static void Autorestart(Type type);
};

} // namespace internal
} // namespace Platform
