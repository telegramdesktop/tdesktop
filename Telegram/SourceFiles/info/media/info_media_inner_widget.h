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

#include "ui/rp_widget.h"
#include "info/media/info_media_widget.h"

namespace Ui {
class SettingsSlider;
class VerticalLayout;
} // namespace Ui

namespace Info {
namespace Media {

class Memento;
class ListWidget;

class InnerWidget final : public Ui::RpWidget {
public:
	using Type = Widget::Type;
	InnerWidget(
		QWidget *parent,
		rpl::producer<Wrap> &&wrap,
		not_null<Window::Controller*> controller,
		not_null<PeerData*> peer,
		Type type);

	not_null<PeerData*> peer() const;
	Type type() const;

	bool showInternal(not_null<Memento*> memento);

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	rpl::producer<int> scrollToRequests() const {
		return _scrollToRequests.events();
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	int recountHeight();
	void refreshHeight();
	void setupOtherTypes(rpl::producer<Wrap> &&wrap);
	void createOtherTypes();
	void createTypeButtons();
	void createTabs();
	void switchToTab(Memento &&memento);

	not_null<Window::Controller*> controller() const;

	object_ptr<ListWidget> setupList(
		not_null<Window::Controller*> controller,
		not_null<PeerData*> peer,
		Type type);

	bool _inResize = false;

	Ui::SettingsSlider *_otherTabs = nullptr;
	object_ptr<Ui::VerticalLayout> _otherTypes = { nullptr };
	object_ptr<Ui::PlainShadow> _otherTabsShadow = { nullptr };
	object_ptr<ListWidget> _list = { nullptr };

	rpl::event_stream<int> _scrollToRequests;

};

} // namespace Media
} // namespace Info
