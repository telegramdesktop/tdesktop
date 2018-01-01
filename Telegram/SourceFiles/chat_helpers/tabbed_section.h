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
#include "window/section_memento.h"

namespace ChatHelpers {

class TabbedSelector;

class TabbedMemento : public Window::SectionMemento {
public:
	TabbedMemento(
		object_ptr<TabbedSelector> selector,
		base::lambda<void(object_ptr<TabbedSelector>)> returnMethod);
	TabbedMemento(TabbedMemento &&other) = default;
	TabbedMemento &operator=(TabbedMemento &&other) = default;

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) override;

	~TabbedMemento();

private:
	object_ptr<TabbedSelector> _selector;
	base::lambda<void(object_ptr<TabbedSelector>)> _returnMethod;

};

class TabbedSection : public Window::SectionWidget {
public:
	TabbedSection(
		QWidget *parent,
		not_null<Window::Controller*> controller);
	TabbedSection(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		object_ptr<TabbedSelector> selector,
		base::lambda<void(object_ptr<TabbedSelector>)> returnMethod);

	void beforeHiding();
	void afterShown();
	void setCancelledCallback(base::lambda<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

	object_ptr<TabbedSelector> takeSelector();
	QPointer<TabbedSelector> getSelector() const;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool forceAnimateBack() const override {
		return true;
	}
	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e) override;
	QRect rectForFloatPlayer() const override;

	~TabbedSection();

protected:
	void resizeEvent(QResizeEvent *e) override;

	void showFinishedHook() override {
		afterShown();
	}

private:
	object_ptr<TabbedSelector> _selector;
	base::lambda<void()> _cancelledCallback;
	base::lambda<void(object_ptr<TabbedSelector>)> _returnMethod;

};

} // namespace ChatHelpers
