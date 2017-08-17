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

#include "window/section_widget.h"

namespace ChatHelpers {

class TabbedSelector;

class TabbedSection : public Window::AbstractSectionWidget {
public:
	TabbedSection(QWidget *parent, not_null<Window::Controller*> controller);
	TabbedSection(QWidget *parent, not_null<Window::Controller*> controller, object_ptr<TabbedSelector> selector);

	void beforeHiding();
	void afterShown();
	void setCancelledCallback(base::lambda<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

	object_ptr<TabbedSelector> takeSelector();
	QPointer<TabbedSelector> getSelector() const;

	void stickersInstalled(uint64 setId);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) override;
	QRect rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<TabbedSelector> _selector;
	base::lambda<void()> _cancelledCallback;

};

} // namespace ChatHelpers
