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
#include "chat_helpers/tabbed_section.h"

#include "styles/style_chat_helpers.h"
#include "chat_helpers/tabbed_selector.h"

namespace ChatHelpers {

TabbedSection::TabbedSection(QWidget *parent, not_null<Window::Controller*> controller) : TabbedSection(parent, controller, object_ptr<TabbedSelector>(this, controller)) {
}

TabbedSection::TabbedSection(QWidget *parent, not_null<Window::Controller*> controller, object_ptr<TabbedSelector> selector) : Window::AbstractSectionWidget(parent, controller)
, _selector(std::move(selector)) {
	resize(st::emojiPanWidth, st::emojiPanMaxHeight);

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
	_selector->setAfterShownCallback(base::lambda<void(SelectorTab)>());
	_selector->setBeforeHidingCallback(base::lambda<void(SelectorTab)>());

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

void TabbedSection::stickersInstalled(uint64 setId) {
	_selector->stickersInstalled(setId);
}

bool TabbedSection::wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) {
	return _selector->wheelEventFromFloatPlayer(e);
}

QRect TabbedSection::rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) {
	return _selector->rectForFloatPlayer();
}

} // namespace ChatHelpers
