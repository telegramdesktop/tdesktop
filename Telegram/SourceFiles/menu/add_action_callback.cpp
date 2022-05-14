/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/add_action_callback.h"

namespace Menu {

MenuCallback::MenuCallback(MenuCallback::Callback callback)
: _callback(std::move(callback)) {
}

QAction *MenuCallback::operator()(Args &&args) const {
	return _callback(std::move(args));
}

QAction *MenuCallback::operator()(
		const QString &text,
		Fn<void()> handler,
		const style::icon *icon) const {
	return _callback({ text, std::move(handler), icon, nullptr });
}

} // namespace Menu
