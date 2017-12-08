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
#include "info/members/info_members_widget.h"

#include "info/profile/info_profile_members.h"
#include "info/info_controller.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_info.h"

namespace Info {
namespace Members {

Memento::Memento(not_null<Controller*> controller)
: Memento(
	controller->peerId(),
	controller->migratedPeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::Members);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(
		parent,
		controller);
	result->setInternalState(geometry, this);
	return std::move(result);
}

void Memento::setState(std::unique_ptr<SavedState> state) {
	_state = std::move(state);
}

std::unique_ptr<SavedState> Memento::state() {
	return std::move(_state);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<Profile::Members>(
		this,
		controller,
		controller->peer()));
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto membersMemento = dynamic_cast<Memento*>(memento.get())) {
		restoreState(membersMemento);
		return true;
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	myEnsureResized(this);
	restoreState(memento);
}

std::unique_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_unique<Memento>(controller());
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	memento->setState(_inner->saveState());
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento->state());
	auto scrollTop = memento->scrollTop();
	scrollTopRestore(memento->scrollTop());
}

} // namespace Members
} // namespace Info

