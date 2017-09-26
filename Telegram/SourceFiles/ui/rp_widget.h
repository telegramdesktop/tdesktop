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
#pragma once

#include <rpl/event_stream.h>
#include <rpl/map.h>
#include <rpl/distinct_until_changed.h>

namespace Ui {

template <typename Widget>
using RpWidgetParent = std::conditional_t<
	std::is_same_v<Widget, QWidget>,
	TWidget,
	TWidgetHelper<Widget>>;

template <typename Widget>
class RpWidgetWrap : public RpWidgetParent<Widget> {
	using Parent = RpWidgetParent<Widget>;

public:
	using Parent::Parent;

	rpl::producer<QRect> geometryValue() const {
		auto &stream = eventStreams().geometry;
		return stream.events_starting_with_copy(this->geometry());
	}
	rpl::producer<QSize> sizeValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.size(); })
			| rpl::distinct_until_changed();
	}
	rpl::producer<int> heightValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.height(); })
			| rpl::distinct_until_changed();
	}
	rpl::producer<int> widthValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.width(); })
			| rpl::distinct_until_changed();
	}
	rpl::producer<QPoint> positionValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.topLeft(); })
			| rpl::distinct_until_changed();
	}
	rpl::producer<int> leftValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.left(); })
			| rpl::distinct_until_changed();
	}
	rpl::producer<int> topValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.top(); })
			| rpl::distinct_until_changed();
	}
	virtual rpl::producer<int> desiredHeightValue() const {
		return heightValue();
	}

	rpl::producer<QRect> paintRequest() const {
		return eventStreams().paint.events();
	}

	rpl::producer<> alive() const {
		return eventStreams().alive.events();
	}

	void showOn(rpl::producer<bool> &&shown) {
		std::move(shown)
			| rpl::start([this](bool visible) {
				this->setVisible(visible);
			}, lifetime());
	}

	rpl::lifetime &lifetime() {
		return _lifetime.data;
	}

protected:
	bool event(QEvent *event) final override {
		switch (event->type()) {
		case QEvent::Move:
		case QEvent::Resize:
			if (auto streams = _eventStreams.get()) {
				auto that = weak(this);
				streams->geometry.fire_copy(this->geometry());
				if (!that) {
					return true;
				}
			}
			break;

		case QEvent::Paint:
			if (auto streams = _eventStreams.get()) {
				auto that = weak(this);
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
	virtual bool eventHook(QEvent *event) {
		return Parent::event(event);
	}

private:
	struct EventStreams {
		rpl::event_stream<QRect> geometry;
		rpl::event_stream<QRect> paint;
		rpl::event_stream<> alive;
	};
	struct LifetimeHolder {
		LifetimeHolder(QWidget *parent) {
			parent->setGeometry(0, 0, 0, 0);
		}
		rpl::lifetime data;
	};

	EventStreams &eventStreams() const {
		if (!_eventStreams) {
			_eventStreams = std::make_unique<EventStreams>();
		}
		return *_eventStreams;
	}

	mutable std::unique_ptr<EventStreams> _eventStreams;

	LifetimeHolder _lifetime = { this };

};

class RpWidget : public RpWidgetWrap<QWidget> {
public:
	using RpWidgetWrap<QWidget>::RpWidgetWrap;

};

} // namespace Ui
