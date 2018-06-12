/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
