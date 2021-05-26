/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_adaptive.h"

#include "history/history_item.h"
#include "data/data_media_types.h"
#include "data/data_session.h"

namespace Window {

AdaptiveModern::AdaptiveModern() = default;

void AdaptiveModern::setWindowLayout(WindowLayout value) {
	_layout = value;
}

void AdaptiveModern::setChatLayout(ChatLayout value) {
	_chatLayout = value;
}

rpl::producer<> AdaptiveModern::changed() const {
	return rpl::merge(
		_chatLayout.changes() | rpl::to_empty,
		_layout.changes() | rpl::to_empty);
}

rpl::producer<bool> AdaptiveModern::oneColumnValue() const {
	return _layout.value(
	) | rpl::map([=] {
		return isOneColumn();
	});
}

rpl::producer<AdaptiveModern::ChatLayout> AdaptiveModern::chatLayoutValue() const {
	return _chatLayout.value();
}

bool AdaptiveModern::isOneColumn() const {
	return _layout.current() == WindowLayout::OneColumn;
}

bool AdaptiveModern::isNormal() const {
	return _layout.current() == WindowLayout::Normal;
}

bool AdaptiveModern::isThreeColumn() const {
	return _layout.current() == WindowLayout::ThreeColumn;
}

} // namespace Window
