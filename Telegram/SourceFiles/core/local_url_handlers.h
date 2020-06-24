/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace qthelp {
class RegularExpressionMatch;
} // namespace qthelp

namespace Window {
class SessionController;
} // namespace Window

namespace Core {

struct LocalUrlHandler {
	QString expression;
	Fn<bool(
		Window::SessionController *controller,
		const qthelp::RegularExpressionMatch &match,
		const QVariant &context)> handler;
};

[[nodiscard]] const std::vector<LocalUrlHandler> &LocalUrlHandlers();
[[nodiscard]] const std::vector<LocalUrlHandler> &InternalUrlHandlers();

[[nodiscard]] QString TryConvertUrlToLocal(QString url);

[[nodiscard]] bool InternalPassportLink(const QString &url);

[[nodiscard]] bool StartUrlRequiresActivate(const QString &url);

} // namespace Core
