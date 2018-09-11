/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/layer_widget.h"

namespace Ui {
class VerticalLayout;
class FadeShadow;
class FlatLabel;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Settings {

class IntroWidget;

class LayerWidget : public Window::LayerWidget {
public:
	LayerWidget(QWidget*);

	void showFinished() override;
	void parentResized() override;

	static int MinimalSupportedWidth();

protected:
	int resizeGetHeight(int newWidth) override;
	void doSetInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setupHeightConsumers();

	object_ptr<IntroWidget> _content;

	int _desiredHeight = 0;
	bool _inResize = false;
	bool _tillTop = false;
	bool _tillBottom = false;

};

} // namespace Info
