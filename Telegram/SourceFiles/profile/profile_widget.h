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

namespace Ui {
class ScrollArea;
class PlainShadow;
template <typename Widget>
class WidgetFadeWrap;
} // namespace Ui

namespace Profile {

class SectionMemento;
class InnerWidget;
class FixedBar;
class Widget final : public Window::SectionWidget {
	Q_OBJECT

public:
	Widget(QWidget *parent, not_null<Window::Controller*> controller, PeerData *peer);

	PeerData *peer() const;
	PeerData *peerForDialogs() const override {
		return peer();
	}

	bool hasTopBarShadow() const override;

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;

	bool showInternal(not_null<Window::SectionMemento*> memento) override;
	std::unique_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(const QRect &geometry, not_null<SectionMemento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) override;
	QRect rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) override;

protected:
	void resizeEvent(QResizeEvent *e) override;

	void showAnimatedHook() override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private slots:
	void onScroll();

private:
	void updateScrollState();
	void updateAdaptiveLayout();
	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::WidgetFadeWrap<Ui::PlainShadow>> _fixedBarShadow;

};

} // namespace Profile
