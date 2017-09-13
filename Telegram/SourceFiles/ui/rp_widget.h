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

class RpWidget : public TWidget {
public:
	RpWidget::RpWidget(QWidget *parent = nullptr) : TWidget(parent) {
		setGeometry(0, 0, 0, 0);
	}

	rpl::producer<QRect> geometryValue() const {
		auto &stream = eventFilter().geometry;
		return stream.events_starting_with_copy(geometry());
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
		return eventFilter().paint.events();
	}

	rpl::producer<> alive() const {
		return eventFilter().alive.events();
	}

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	class EventFilter : public QObject {
	public:
		EventFilter(RpWidget *parent) : QObject(parent) {
			parent->installEventFilter(this);
		}
		rpl::event_stream<QRect> geometry;
		rpl::event_stream<QRect> paint;
		rpl::event_stream<> alive;

	protected:
		bool eventFilter(QObject *object, QEvent *event) {
			auto widget = static_cast<RpWidget*>(parent());

			switch (event->type()) {
			case QEvent::Move:
			case QEvent::Resize:
				geometry.fire_copy(widget->geometry());
				break;

			case QEvent::Paint:
				paint.fire_copy(
					static_cast<QPaintEvent*>(event)->rect());
				break;
			}

			return QObject::eventFilter(object, event);
		}

	};

	EventFilter &eventFilter() const {
		if (!_eventFilter) {
			auto that = const_cast<RpWidget*>(this);
			that->_eventFilter = std::make_unique<EventFilter>(that);
		}
		return *_eventFilter;
	}

	std::unique_ptr<EventFilter> _eventFilter;

	rpl::lifetime _lifetime;

};

} // namespace Ui
