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
#include "info/media/info_media_widget.h"

#include "info/media/info_media_inner_widget.h"
#include "ui/widgets/scroll_area.h"

namespace Info {
namespace Media {

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		Wrap wrap,
		not_null<Window::Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(
		parent,
		wrap,
		controller,
		App::peer(_peerId),
		_type);
	result->setInternalState(geometry, this);
	return std::move(result);
}

Widget::Widget(
	QWidget *parent,
	Wrap wrap,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer,
	Type type)
: ContentWidget(parent, wrap, controller, peer) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(this, peer, type));
}

Section Widget::section() const {
	return Section(type());
}

Widget::Type Widget::type() const {
	return _inner->type();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (auto mediaMemento = dynamic_cast<Memento*>(memento.get())) {
		if (mediaMemento->peerId() == peer()->id) {
			restoreState(mediaMemento);
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
	auto result = std::make_unique<Memento>(peer()->id, type());
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

} // namespace Media
} // namespace Info
