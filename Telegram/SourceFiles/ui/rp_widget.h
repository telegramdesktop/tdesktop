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
#include "base/unique_qptr.h"

namespace Ui {
namespace details {

template <typename Value>
class AttachmentOwner : public QObject {
public:
	template <typename OtherValue>
	AttachmentOwner(QObject *parent, OtherValue &&value)
	: QObject(parent)
	, _value(std::forward<OtherValue>(value)) {
	}

private:
	Value _value;

};

} // namespace details

template <typename Widget, typename ...Args>
inline base::unique_qptr<Widget> CreateObject(Args &&...args) {
	return base::make_unique_q<Widget>(
		nullptr,
		std::forward<Args>(args)...);
}

template <typename Widget, typename Parent, typename ...Args>
inline Widget *CreateChild(
		Parent *parent,
		Args &&...args) {
	Expects(parent != nullptr);
	return base::make_unique_q<Widget>(
		parent,
		std::forward<Args>(args)...).release();
}

template <typename Value>
inline void AttachAsChild(not_null<QObject*> parent, Value &&value) {
	using PlainValue = std::decay_t<Value>;
	CreateChild<details::AttachmentOwner<PlainValue>>(
		parent.get(),
		std::forward<Value>(value));
}

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

	auto geometryValue() const {
		auto &stream = eventStreams().geometry;
		return stream.events_starting_with_copy(this->geometry());
	}
	auto sizeValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.size(); })
			| rpl::distinct_until_changed();
	}
	auto heightValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.height(); })
			| rpl::distinct_until_changed();
	}
	auto widthValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.width(); })
			| rpl::distinct_until_changed();
	}
	auto positionValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.topLeft(); })
			| rpl::distinct_until_changed();
	}
	auto leftValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.left(); })
			| rpl::distinct_until_changed();
	}
	auto topValue() const {
		return geometryValue()
			| rpl::map([](QRect &&value) { return value.top(); })
			| rpl::distinct_until_changed();
	}
	virtual rpl::producer<int> desiredHeightValue() const {
		return heightValue();
	}
	auto shownValue() const {
		auto &stream = eventStreams().shown;
		return stream.events_starting_with(!this->isHidden());
	}

	auto paintRequest() const {
		return eventStreams().paint.events();
	}

	auto alive() const {
		return eventStreams().alive.events();
	}

	void setVisible(bool visible) final override {
		auto wasVisible = !this->isHidden();
		Parent::setVisible(visible);
		auto nowVisible = !this->isHidden();
		if (nowVisible != wasVisible) {
			if (auto streams = _eventStreams.get()) {
				streams->shown.fire_copy(nowVisible);
			}
		}
	}

	template <typename Error, typename Generator>
	void showOn(rpl::producer<bool, Error, Generator> &&shown) {
		std::move(shown)
			| rpl::start_with_next([this](bool visible) {
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
		rpl::event_stream<bool> shown;
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
