/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/tabbed_section.h"

#include "styles/style_chat_helpers.h"
#include "chat_helpers/tabbed_selector.h"

namespace ChatHelpers {

TabbedMemento::TabbedMemento(
	object_ptr<TabbedSelector> selector,
	Fn<void(object_ptr<TabbedSelector>)> returnMethod)
: _selector(std::move(selector))
, _returnMethod(std::move(returnMethod)) {
}

object_ptr<Window::SectionWidget> TabbedMemento::createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto result = object_ptr<TabbedSection>(
		parent,
		controller,
		std::move(_selector),
		std::move(_returnMethod));
	result->setGeometry(geometry);
	return std::move(result);
}

TabbedMemento::~TabbedMemento() {
	if (_returnMethod && _selector) {
		_returnMethod(std::move(_selector));
	}
}

TabbedSection::TabbedSection(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: TabbedSection(
	parent,
	controller,
	object_ptr<TabbedSelector>(this, controller),
	Fn<void(object_ptr<TabbedSelector>)>()) {
}

TabbedSection::TabbedSection(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	object_ptr<TabbedSelector> selector,
	Fn<void(object_ptr<TabbedSelector>)> returnMethod)
: Window::SectionWidget(parent, controller)
, _selector(std::move(selector))
, _returnMethod(std::move(returnMethod)) {
	_selector->setParent(this);
	_selector->setRoundRadius(0);
	_selector->setGeometry(rect());
	_selector->showStarted();
	_selector->show();
	connect(_selector, &TabbedSelector::cancelled, this, [this] {
		if (_cancelledCallback) {
			_cancelledCallback();
		}
	});
	_selector->setAfterShownCallback(Fn<void(SelectorTab)>());
	_selector->setBeforeHidingCallback(Fn<void(SelectorTab)>());

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

object_ptr<TabbedSelector> TabbedSection::takeSelector() {
	_selector->beforeHiding();
	return std::move(_selector);
}

QPointer<TabbedSelector> TabbedSection::getSelector() const {
	return _selector.data();
}
bool TabbedSection::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return false;
}

bool TabbedSection::wheelEventFromFloatPlayer(QEvent *e) {
	return _selector->wheelEventFromFloatPlayer(e);
}

QRect TabbedSection::rectForFloatPlayer() const {
	return _selector->rectForFloatPlayer();
}

TabbedSection::~TabbedSection() {
	beforeHiding();
	if (_returnMethod) {
		_returnMethod(takeSelector());
	}
}

} // namespace ChatHelpers
