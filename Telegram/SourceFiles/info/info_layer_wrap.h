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

#include "layerwidget.h"

namespace Window {
class Controller;
} // namespace Window

namespace Info {

class Memento;
class ContentWidget;
class TopBar;

class LayerWrap : public LayerWidget {
public:
	LayerWrap(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento);

	void showFinished() override;
	void parentResized() override;

protected:
	void paintEvent(QPaintEvent *e) override;

	void setRoundedCorners(bool roundedCorners);

private:
	object_ptr<TopBar> createTopBar(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento);
	object_ptr<ContentWidget> createContent(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento);

	void resizeToWidth(int newWidth, int newContentLeft);
	void resizeToDesiredHeight();

	object_ptr<TopBar> _topBar;
	object_ptr<ContentWidget> _content;

	int _desiredHeight = 0;
	bool _roundedCorners = false;

};

} // namespace Info
