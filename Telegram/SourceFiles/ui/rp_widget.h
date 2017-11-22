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
	return new Widget(parent, std::forward<Args>(args)...);
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
class RpWidgetWrap;

class RpWidgetMethods {
public:
	rpl::producer<QRect> geometryValue() const;
	rpl::producer<QSize> sizeValue() const;
	rpl::producer<int> heightValue() const;
	rpl::producer<int> widthValue() const;
	rpl::producer<QPoint> positionValue() const;
	rpl::producer<int> leftValue() const;
	rpl::producer<int> topValue() const;
	virtual rpl::producer<int> desiredHeightValue() const;
	rpl::producer<bool> shownValue() const;
	rpl::producer<QRect> paintRequest() const;
	rpl::producer<> alive() const;

	template <typename Error, typename Generator>
	void showOn(rpl::producer<bool, Error, Generator> &&shown) {
		std::move(shown)
			| rpl::start_with_next([this](bool visible) {
				callSetVisible(visible);
			}, lifetime());
	}

	rpl::lifetime &lifetime();

protected:
	bool handleEvent(QEvent *event);
	virtual bool eventHook(QEvent *event) = 0;

private:
	template <typename Widget>
	friend class RpWidgetWrap;

	struct EventStreams {
		rpl::event_stream<QRect> geometry;
		rpl::event_stream<QRect> paint;
		rpl::event_stream<bool> shown;
		rpl::event_stream<> alive;
	};
	struct Initer {
		Initer(QWidget *parent);
	};

	virtual void callSetVisible(bool visible) = 0;
	virtual QPointer<QObject> callCreateWeak() = 0;
	virtual QRect callGetGeometry() const = 0;
	virtual bool callIsHidden() const = 0;

	void visibilityChangedHook(bool wasVisible, bool nowVisible);
	EventStreams &eventStreams() const;

	mutable std::unique_ptr<EventStreams> _eventStreams;
	rpl::lifetime _lifetime;

};

template <typename Widget>
class RpWidgetWrap
	: public RpWidgetParent<Widget>
	, public RpWidgetMethods {
	using Self = RpWidgetWrap<Widget>;
	using Parent = RpWidgetParent<Widget>;

public:
	using Parent::Parent;

	void setVisible(bool visible) final override {
		auto wasVisible = !this->isHidden();
		Parent::setVisible(visible);
		visibilityChangedHook(wasVisible, !this->isHidden());
	}

	~RpWidgetWrap() {
		base::take(_lifetime);
		base::take(_eventStreams);
	}

protected:
	bool event(QEvent *event) final override {
		return handleEvent(event);
	}
	bool eventHook(QEvent *event) override {
		return Parent::event(event);
	}

private:
	void callSetVisible(bool visible) override {
		Self::setVisible(visible);
	}
	QPointer<QObject> callCreateWeak() override {
		return QPointer<QObject>((QObject*)this);
	}
	QRect callGetGeometry() const override {
		return this->geometry();
	}
	bool callIsHidden() const override {
		return this->isHidden();
	}

	Initer _initer = { this };

};

class RpWidget : public RpWidgetWrap<QWidget> {
public:
	using RpWidgetWrap<QWidget>::RpWidgetWrap;

};

} // namespace Ui
