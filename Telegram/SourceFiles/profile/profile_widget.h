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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "window/section_widget.h"

class ScrollArea;

namespace Profile {

class SectionMemento;
class InnerWidget;
class FixedBar;
class Widget final : public Window::SectionWidget {
	Q_OBJECT

public:
	Widget(QWidget *parent, PeerData *peer);

	PeerData *peer() const;
	PeerData *peerForDialogs() const override {
		return peer();
	}

	bool hasTopBarShadow() const override {
		return _fixedBarShadow->isFullyShown();
	}

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;

	void setInnerFocus() override;

	void updateAdaptiveLayout() override;

	bool showInternal(const Window::SectionMemento *memento) override;
	std_::unique_ptr<Window::SectionMemento> createMemento() const override;

	void setInternalState(const SectionMemento *memento);

protected:
	void resizeEvent(QResizeEvent *e) override;

	void showAnimatedHook() override;
	void showFinishedHook() override;

private slots:
	void onScroll();

private:
	friend class SectionMemento;

	ChildWidget<ScrollArea> _scroll;
	ChildWidget<InnerWidget> _inner;
	ChildWidget<FixedBar> _fixedBar;
	ChildWidget<ToggleableShadow> _fixedBarShadow;

};

} // namespace Profile
