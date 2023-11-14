/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ClickHandler;

namespace HistoryView {

[[nodiscard]] std::shared_ptr<ClickHandler> SponsoredLink(
	const QString &externalLink);

} // namespace HistoryView
