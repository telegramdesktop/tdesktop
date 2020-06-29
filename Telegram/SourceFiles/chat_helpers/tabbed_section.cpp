/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/tabbed_section.h"

#include "chat_helpers/tabbed_selector.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"

namespace ChatHelpers {

object_ptr<Window::SectionWidget> TabbedMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto result = object_ptr<TabbedSection>(parent, controller);
	result->setGeometry(geometry);
	return result;
}

TabbedSection::TabbedSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, _selector(controller->tabbedSelector()) {
	_selector->setParent(this);
	_selector->setRoundRadius(0);
	_selector->setGeometry(rect());
	_selector->showStarted();
	_selector->show();
	_selector->setAfterShownCallback(nullptr);
	_selector->setBeforeHidingCallback(nullptr);

	setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void TabbedSection::beforeHiding() {
	_selector->beforeHiding();
}

void TabbedSection::afterShown() {
	_selector->afterShown();
}

void TabbedSection::resizeEvent(QResizeEvent *e) {
	_selector->setGeometry(rect());
}

void TabbedSection::showFinishedHook() {
	afterShown();
}

bool TabbedSection::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return false;
}

bool TabbedSection::floatPlayerHandleWheelEvent(QEvent *e) {
	return _selector->floatPlayerHandleWheelEvent(e);
}

QRect TabbedSection::floatPlayerAvailableRect() {
	return _selector->floatPlayerAvailableRect();
}

TabbedSection::~TabbedSection() {
	beforeHiding();
	controller()->takeTabbedSelectorOwnershipFrom(this);
}

} // namespace ChatHelpers
