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

#include "window/section_memento.h"

namespace Profile {

class Widget;

class SectionMemento : public Window::SectionMemento {
public:
	SectionMemento(PeerData *peer) : _peer(peer) {
	}

	object_ptr<Window::SectionWidget> createWidget(QWidget *parent, not_null<Window::Controller*> controller, const QRect &geometry) override;

	PeerData *getPeer() const {
		return _peer;
	}
	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int getScrollTop() const {
		return _scrollTop;
	}

private:
	PeerData *_peer;
	int _scrollTop = 0;

};

} // namespace Window
