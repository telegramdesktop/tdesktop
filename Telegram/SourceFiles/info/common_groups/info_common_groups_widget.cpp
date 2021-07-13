/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/common_groups/info_common_groups_widget.h"

#include "info/common_groups/info_common_groups_inner_widget.h"
#include "info/info_controller.h"
#include "ui/search_field_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/ui_utility.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "styles/style_info.h"

namespace Info {
namespace CommonGroups {

Memento::Memento(not_null<UserData*> user)
: ContentMemento(user, 0) {
}

Section Memento::section() const {
	return Section(Section::Type::CommonGroups);
}

not_null<UserData*> Memento::user() const {
	return peer()->asUser();
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, user());
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setListState(std::unique_ptr<PeerListState> state) {
	_listState = std::move(state);
}

std::unique_ptr<PeerListState> Memento::listState() {
	return std::move(_listState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<UserData*> user)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller,
		user));
}

not_null<UserData*> Widget::user() const {
	return _inner->user();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto groupsMemento = dynamic_cast<Memento*>(memento.get())) {
		if (groupsMemento->user() == user()) {
			restoreState(groupsMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(user());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

} // namespace CommonGroups
} // namespace Info

