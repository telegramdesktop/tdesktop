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
[[nodiscard]] Launched Launch();
[[nodiscard]] std::shared_ptr<twidget::SandboxRemoteStream> Adopt(
	const QString &channel);

} // namespace Platform::WidgetHelper

#ifndef Q_OS_MAC

namespace Platform::WidgetHelper {

inline bool Enabled() {
	return false;
}

inline Launched Launch() {
	return {
		.error = u"the widget helper is not available on this platform"_q,
	};
}

inline std::shared_ptr<twidget::SandboxRemoteStream> Adopt(
		const QString &channel) {
	return nullptr;
}

} // namespace Platform::WidgetHelper

#endif // !Q_OS_MAC
