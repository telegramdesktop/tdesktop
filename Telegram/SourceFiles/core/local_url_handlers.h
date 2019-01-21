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

namespace Core {

struct LocalUrlHandler {
	QString expression;
	Fn<bool(
		const qthelp::RegularExpressionMatch &match,
		const QVariant &context)> handler;
};

const std::vector<LocalUrlHandler> &LocalUrlHandlers();

} // namespace Core
