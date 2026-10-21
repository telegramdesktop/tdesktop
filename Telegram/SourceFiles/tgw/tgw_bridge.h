/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace Tgw {

class Bridge final {
public:
	explicit Bridge(not_null<HistoryItem*> item);

	[[nodiscard]] QString openLink(const QString &url, bool telegram);

private:
	[[nodiscard]] Window::SessionController *findController() const;

	const not_null<HistoryItem*> _item;

};

} // namespace Tgw
