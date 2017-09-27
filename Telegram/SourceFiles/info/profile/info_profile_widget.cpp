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
#include "info/profile/info_profile_widget.h"

#include "info/profile/info_profile_inner_widget.h"
#include "ui/widgets/scroll_area.h"

namespace Info {
namespace Profile {

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		Wrap wrap,
		not_null<Window::Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(
		parent,
		wrap,
		controller,
		App::peer(_peerId));
	result->setInternalState(geometry, this);
	return std::move(result);
}

Widget::Widget(
	QWidget *parent,
	Wrap wrap,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer)
: ContentWidget(parent, wrap, controller, peer) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		wrapValue(),
		controller,
		peer));
	_inner->move(0, 0);
	_inner->scrollToRequests()
		| rpl::start_with_next([this](Ui::ScrollToRequest request) {
			if (request.ymin < 0) {
				scrollTopRestore(
					qMin(scrollTopSave(), request.ymax));
			} else {
				scrollTo(request);
			}
		}, lifetime());
}

Section Widget::section() const {
	return Section(Section::Type::Profile);
}

void Widget::setInnerFocus() {
	_inner->setFocus();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (auto profileMemento = dynamic_cast<Memento*>(memento.get())) {
		if (profileMemento->peerId() == peer()->id) {
			restoreState(profileMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, not_null<Memento*> memento) {
	setGeometry(geometry);
	myEnsureResized(this);
	restoreState(memento);
}

std::unique_ptr<ContentMemento> Widget::createMemento() {
	auto result = std::make_unique<Memento>(peer()->id);
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

} // namespace Profile
} // namespace Info
