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
template <typename Widget>
class WidgetFadeWrap;
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
class TopBar;

class NarrowWrap final : public Window::SectionWidget {
public:
	NarrowWrap(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento);
	NarrowWrap(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<MoveMemento*> memento);

	not_null<PeerData*> peer() const;
	PeerData *peerForDialogs() const override {
		return peer();
	}

	bool hasTopBarShadow() const override;

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento) override;
	std::unique_ptr<Window::SectionMemento> createMemento() override;

	rpl::producer<int> desiredHeight() const override;

	object_ptr<LayerWidget> moveContentToLayer(
		int availableWidth) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e) override;
	QRect rectForFloatPlayer() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void doSetInnerFocus() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);
	void restoreState(not_null<MoveMemento*> memento);

	QRect contentGeometry() const;
	rpl::producer<int> desiredHeightForContent() const;

	void showContent(object_ptr<ContentWidget> content);
	object_ptr<TopBar> createTopBar();

	object_ptr<Ui::WidgetFadeWrap<Ui::PlainShadow>> _topShadow = { nullptr };
	object_ptr<ContentWidget> _content = { nullptr };
	object_ptr<TopBar> _topBar = { nullptr };

	rpl::event_stream<rpl::producer<int>> _desiredHeights;

	rpl::lifetime _lifetime;

};

} // namespace Info
