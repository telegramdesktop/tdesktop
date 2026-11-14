/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace twidget {
class SandboxRemoteStream;
} // namespace twidget

namespace Platform::WidgetHelper {

struct Completion {
	QString detail;
	bool reaped = false;
};

struct Launched {
	std::shared_ptr<twidget::SandboxRemoteStream> stream;
	FnMut<Completion(crl::time exitWait, crl::time killWait)> complete;
	QString error;
	int64 pid = 0;
};

[[nodiscard]] bool Enabled();
[[nodiscard]] Launched Launch(const QStringList &arguments);
[[nodiscard]] std::shared_ptr<twidget::SandboxRemoteStream> Adopt(
	const QString &channel);
[[nodiscard]] QString Lockdown();

} // namespace Platform::WidgetHelper

#if !defined Q_OS_MAC && !defined Q_OS_WIN && !defined Q_OS_LINUX

namespace Platform::WidgetHelper {

inline bool Enabled() {
	return false;
}

inline Launched Launch(const QStringList &arguments) {
	return {
		.error = u"the widget helper is not available on this platform"_q,
	};
}

inline std::shared_ptr<twidget::SandboxRemoteStream> Adopt(
		const QString &channel) {
	return nullptr;
}

inline QString Lockdown() {
	return u"the widget helper cannot lock down on this platform"_q;
}

} // namespace Platform::WidgetHelper

#endif // !Q_OS_MAC && !Q_OS_WIN && !Q_OS_LINUX

#ifdef Q_OS_WIN

namespace Platform::WidgetHelper {

[[nodiscard]] Fn<QString()> PrepareProbe();

} // namespace Platform::WidgetHelper

#endif // Q_OS_WIN
