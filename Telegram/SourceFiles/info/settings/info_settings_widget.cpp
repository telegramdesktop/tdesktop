/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/settings/info_settings_widget.h"

#include "info/info_memento.h"
#include "info/info_controller.h"
#include "settings/settings_common.h"
#include "ui/ui_utility.h"

namespace Info {
namespace Settings {

Memento::Memento(not_null<UserData*> self, Type type)
: ContentMemento(Tag{ self })
, _type(type) {
}

Section Memento::section() const {
	return Section(_type);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(
		parent,
		controller);
	result->setInternalState(geometry, this);
	return result;
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _self(controller->key().settingsSelf())
, _type(controller->section().settingsType())
, _inner(setInnerWidget(
	::Settings::CreateSection(
		_type,
		this,
		controller->parentController()))) {
	_inner->sectionShowOther(
	) | rpl::start_with_next([=](Type type) {
		controller->showSettings(type);
	}, _inner->lifetime());

	controller->setCanSaveChanges(_inner->sectionCanSaveChanges());
}

Widget::~Widget() = default;

not_null<UserData*> Widget::self() const {
	return _self;
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	//if (const auto myMemento = dynamic_cast<Memento*>(memento.get())) {
	//	Assert(myMemento->self() == self());

	//	if (_inner->showInternal(myMemento)) {
	//		return true;
	//	}
	//}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

rpl::producer<bool> Widget::canSaveChanges() const {
	return _inner->sectionCanSaveChanges();
}

void Widget::saveChanges(FnMut<void()> done) {
	_inner->sectionSaveChanges(std::move(done));
}

rpl::producer<bool> Widget::desiredShadowVisibility() const {
	return (_type == Type::Main || _type == Type::Information)
		? ContentWidget::desiredShadowVisibility()
		: rpl::single(true);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(self(), _type);
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
}

void Widget::restoreState(not_null<Memento*> memento) {
	const auto scrollTop = memento->scrollTop();
	scrollTopRestore(memento->scrollTop());
}

} // namespace Settings
} // namespace Info
