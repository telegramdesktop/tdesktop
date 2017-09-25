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

#include <rpl/producer.h>
#include "ui/rp_widget.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {

enum class Wrap;

namespace Profile {

class Memento;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		rpl::producer<Wrap> &&wrapValue,
		not_null<Window::Controller*> controller,
		not_null<PeerData*> peer);

	not_null<PeerData*> peer() const {
		return _peer;
	}

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return RpWidget::resizeToWidth(newWidth);
	}

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	object_ptr<RpWidget> setupContent(
		RpWidget *parent,
		rpl::producer<Wrap> &&wrapValue) const;
	object_ptr<RpWidget> setupDetails(RpWidget *parent) const;
	object_ptr<RpWidget> setupSharedMedia(RpWidget *parent) const;
	object_ptr<RpWidget> setupMuteToggle(RpWidget *parent) const;
	object_ptr<RpWidget> setupInfo(RpWidget *parent) const;
	void setupUserButtons(
		Ui::VerticalLayout *wrap,
		not_null<UserData*> user) const;
	object_ptr<RpWidget> setupUserActions(
		RpWidget *parent,
		not_null<UserData*> user) const;

	object_ptr<RpWidget> createSkipWidget(RpWidget *parent) const;
	object_ptr<Ui::SlideWrap<RpWidget>> createSlideSkipWidget(
		RpWidget *parent) const;

	bool canHideDetailsEver() const;
	rpl::producer<bool> canHideDetails() const;

	not_null<Window::Controller*> _controller;
	not_null<PeerData*> _peer;

	int _minHeight = 0;

	object_ptr<RpWidget> _content;

};

} // namespace Profile
} // namespace Info
