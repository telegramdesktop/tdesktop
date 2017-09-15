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

#include <rpl/event_stream.h>
#include "window/section_widget.h"

namespace Ui {
class PlainShadow;
class SettingsSlider;
} // namespace Ui

namespace Info {
namespace Profile {
class Widget;
} // namespace Profile

namespace Media {
class Widget;
} // namespace Media

class Memento;
class MoveMemento;
class ContentWidget;

class SideWrap final : public Window::SectionWidget {
public:
	SideWrap(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento);
	SideWrap(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<MoveMemento*> memento);

	not_null<PeerData*> peer() const;
	PeerData *peerForDialogs() const override {
		return peer();
	}

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento) override;
	std::unique_ptr<Window::SectionMemento> createMemento() override;

	rpl::producer<int> desiredHeightValue() const override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(
		QEvent *e,
		Window::Column myColumn,
		Window::Column playerColumn) override;
	QRect rectForFloatPlayer(
		Window::Column myColumn,
		Window::Column playerColumn) const override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void doSetInnerFocus() override;

private:
	enum class Tab {
		Profile,
		Media,
		None,
	};
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);
	void restoreState(not_null<MoveMemento*> memento);

	QRect contentGeometry() const;
	rpl::producer<int> desiredHeightForContent() const;

	void setupTabs();
	void showTab(Tab tab);
	void setCurrentTab(Tab tab);
	void showContent(object_ptr<ContentWidget> content);
	object_ptr<ContentWidget> createContent(Tab tab);
	object_ptr<Profile::Widget> createProfileWidget();
	object_ptr<Media::Widget> createMediaWidget();

	object_ptr<Ui::PlainShadow> _tabsShadow = { nullptr };
	object_ptr<Ui::SettingsSlider> _tabs = { nullptr };
	object_ptr<ContentWidget> _content = { nullptr };
	Tab _tab = Tab::Profile;

	rpl::event_stream<rpl::producer<int>> _desiredHeights;

	rpl::lifetime _lifetime;

};

} // namespace Info
