/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_section_widget.h"

#include "window/window_adaptive.h"
#include "window/window_connecting_widget.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "info/info_content_widget.h"
#include "info/info_wrap_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_memento.h"
#include "info/info_controller.h"

namespace Info {

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> window,
	Wrap wrap,
	not_null<Memento*> memento)
: Window::SectionWidget(parent, window)
, _content(this, window, wrap, memento) {
	init();
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> window,
	Wrap wrap,
	not_null<MoveMemento*> memento)
: Window::SectionWidget(parent, window)
, _content(memento->takeContent(this, wrap)) {
	init();
}

void SectionWidget::init() {
	Expects(_connecting == nullptr);

	sizeValue(
	) | rpl::start_with_next([wrap = _content.data()](QSize size) {
		auto wrapGeometry = QRect{ { 0, 0 }, size };
		auto additionalScroll = 0;
		wrap->updateGeometry(wrapGeometry, additionalScroll);
	}, _content->lifetime());

	_connecting = std::make_unique<Window::ConnectionState>(
		_content.data(),
		&controller()->session().account(),
		controller()->adaptive().oneColumnValue());

	_content->contentChanged(
	) | rpl::start_with_next([=] {
		_connecting->raise();
	}, _connecting->lifetime());
}

Dialogs::RowDescriptor SectionWidget::activeChat() const {
	return _content->activeChat();
}

bool SectionWidget::hasTopBarShadow() const {
	return _content->hasTopBarShadow();
}

QPixmap SectionWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	return _content->grabForShowAnimation(params);
}

void SectionWidget::doSetInnerFocus() {
	_content->setInnerFocus();
}

void SectionWidget::showFinishedHook() {
	_topBarSurrogate.destroy();
	_content->showFast();
}

void SectionWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBarSurrogate = _content->createTopBarSurrogate(this);
}

bool SectionWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return _content->showInternal(memento, params);
}

std::shared_ptr<Window::SectionMemento> SectionWidget::createMemento() {
	return _content->createMemento();
}

object_ptr<Ui::LayerWidget> SectionWidget::moveContentToLayer(
		QRect bodyGeometry) {
	if (_content->controller()->wrap() != Wrap::Narrow
		|| width() < LayerWidget::MinimalSupportedWidth()) {
		return nullptr;
	}
	return MoveMemento(
		std::move(_content)).createLayer(
			controller(),
			bodyGeometry);
}

bool SectionWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _content->floatPlayerHandleWheelEvent(e);
}

QRect SectionWidget::floatPlayerAvailableRect() {
	return _content->floatPlayerAvailableRect();
}

} // namespace Info
