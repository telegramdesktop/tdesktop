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

#include <rpl/producer.h>
#include "info/info_memento.h"

namespace Info {
namespace CommonGroups {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(UserId userId) : _userId(userId) {
	}

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		Wrap wrap,
		not_null<Window::Controller*> controller,
		const QRect &geometry) override;

	UserId userId() const {
		return _userId;
	}

private:
	UserId _userId = 0;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		Wrap wrap,
		not_null<Window::Controller*> controller,
		not_null<UserData*> user);

	not_null<UserData*> user() const;
	Section section() const override;

	bool showInternal(
		not_null<ContentMemento*> memento) override;
	std::unique_ptr<ContentMemento> createMemento() override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	InnerWidget *_inner = nullptr;

};

} // namespace CommonGroups
} // namespace Info

