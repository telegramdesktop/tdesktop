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
[[nodiscard]] QString UnsupportedReason();
[[nodiscard]] Launched Launch(const QStringList &arguments);

} // namespace Platform::WidgetHelper

#if !defined Q_OS_MAC && !defined Q_OS_WIN && !defined Q_OS_LINUX

namespace Platform::WidgetHelper {

inline bool Enabled() {
	return false;
}

inline QString UnsupportedReason() {
	return u"the widget helper is not available on this platform"_q;
}

inline Launched Launch(const QStringList &arguments) {
	return { .error = UnsupportedReason() };
}

} // namespace Platform::WidgetHelper

#endif // !Q_OS_MAC && !Q_OS_WIN && !Q_OS_LINUX
