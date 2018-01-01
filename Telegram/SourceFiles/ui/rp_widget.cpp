/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/rp_widget.h"

namespace Ui {

rpl::producer<QRect> RpWidgetMethods::geometryValue() const {
	auto &stream = eventStreams().geometry;
	return stream.events_starting_with_copy(callGetGeometry());
}

rpl::producer<QSize> RpWidgetMethods::sizeValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.size(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetMethods::heightValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.height(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetMethods::widthValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.width(); })
		| rpl::distinct_until_changed();
}

rpl::producer<QPoint> RpWidgetMethods::positionValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.topLeft(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetMethods::leftValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.left(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetMethods::topValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.top(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetMethods::desiredHeightValue() const {
	return heightValue();
}

rpl::producer<bool> RpWidgetMethods::shownValue() const {
	auto &stream = eventStreams().shown;
	return stream.events_starting_with(!callIsHidden());
}

rpl::producer<QRect> RpWidgetMethods::paintRequest() const {
	return eventStreams().paint.events();
}

rpl::producer<> RpWidgetMethods::alive() const {
	return eventStreams().alive.events();
}

rpl::lifetime &RpWidgetMethods::lifetime() {
	return _lifetime;
}

bool RpWidgetMethods::handleEvent(QEvent *event) {
	switch (event->type()) {
	case QEvent::Move:
	case QEvent::Resize:
		if (auto streams = _eventStreams.get()) {
			auto that = callCreateWeak();
			streams->geometry.fire_copy(callGetGeometry());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::Paint:
		if (auto streams = _eventStreams.get()) {
			auto that = callCreateWeak();
			streams->paint.fire_copy(
				static_cast<QPaintEvent*>(event)->rect());
			if (!that) {
				return true;
			}
		}
		break;
	}

	return eventHook(event);
}

RpWidgetMethods::Initer::Initer(QWidget *parent) {
	parent->setGeometry(0, 0, 0, 0);
}

void RpWidgetMethods::visibilityChangedHook(bool wasVisible, bool nowVisible) {
	if (nowVisible != wasVisible) {
		if (auto streams = _eventStreams.get()) {
			streams->shown.fire_copy(nowVisible);
		}
	}
}

auto RpWidgetMethods::eventStreams() const -> EventStreams& {
	if (!_eventStreams) {
		_eventStreams = std::make_unique<EventStreams>();
	}
	return *_eventStreams;
}

} // namespace Ui
