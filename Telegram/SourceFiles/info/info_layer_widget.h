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

#include "window/layer_widget.h"

namespace Window {
class Controller;
} // namespace Window

namespace Info {

class Memento;
class MoveMemento;
class WrapWidget;
class TopBar;

class LayerWidget : public Window::LayerWidget {
public:
	LayerWidget(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento);
	LayerWidget(
		not_null<Window::Controller*> controller,
		not_null<MoveMemento*> memento);

	void showFinished() override;
	void parentResized() override;

	bool takeToThirdSection() override;
	bool showSectionInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;

	static int MinimalSupportedWidth();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setupHeightConsumers();

	not_null<Window::Controller*> _controller;
	object_ptr<WrapWidget> _content;

	int _desiredHeight = 0;
	bool _inResize = false;
	bool _tillBottom = false;

};

} // namespace Info
