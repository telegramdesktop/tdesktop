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

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace AdminLog {

class FixedBar;
class InnerWidget;
class SectionMemento;

class Widget final : public Window::SectionWidget {
public:
	Widget(QWidget *parent, gsl::not_null<Window::Controller*> controller, gsl::not_null<ChannelData*> channel);

	gsl::not_null<ChannelData*> channel() const;
	PeerData *peerForDialogs() const override {
		return channel();
	}

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;

	bool showInternal(const Window::SectionMemento *memento) override;
	std::unique_ptr<Window::SectionMemento> createMemento() const override;

	void setInternalState(const QRect &geometry, gsl::not_null<const SectionMemento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) override;
	QRect rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) override;

	// Empty "flags" means all events. Empty "admins" means all admins.
	void applyFilter(MTPDchannelAdminLogEventsFilter::Flags flags, const std::vector<gsl::not_null<UserData*>> &admins);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook() override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void onScroll();
	void updateAdaptiveLayout();
	void saveState(gsl::not_null<SectionMemento*> memento) const;
	void restoreState(gsl::not_null<const SectionMemento*> memento);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::PlainShadow> _fixedBarShadow;
	object_ptr<Ui::FlatButton> _whatIsThis;

};

class SectionMemento : public Window::SectionMemento {
public:
	SectionMemento(gsl::not_null<ChannelData*> channel) : _channel(channel) {
	}

	object_ptr<Window::SectionWidget> createWidget(QWidget *parent, gsl::not_null<Window::Controller*> controller, const QRect &geometry) const override;

	gsl::not_null<ChannelData*> getChannel() const {
		return _channel;
	}
	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int getScrollTop() const {
		return _scrollTop;
	}

private:
	gsl::not_null<ChannelData*> _channel;
	int _scrollTop = 0;

};

} // namespace AdminLog
