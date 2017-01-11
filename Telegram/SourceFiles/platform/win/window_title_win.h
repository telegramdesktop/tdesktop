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

#include "window/window_title.h"

namespace Ui {
class IconButton;
class PlainShadow;
} // namespace Ui

namespace Window {
namespace Theme {

int DefaultPreviewTitleHeight();
void DefaultPreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Theme
} // namespace Window

namespace Platform {

class TitleWidget : public Window::TitleWidget, private base::Subscriber {
	Q_OBJECT

public:
	TitleWidget(QWidget *parent);

	void init() override;

	Window::HitTestResult hitTest(const QPoint &p) const override;

public slots:
	void onWindowStateChanged(Qt::WindowState state = Qt::WindowNoState);
	void updateControlsVisibility();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateMaximizeRestoreButton();
	void updateControlsPosition();

	object_ptr<Ui::IconButton> _minimize;
	object_ptr<Ui::IconButton> _maximizeRestore;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::PlainShadow> _shadow;

	bool _maximized = false;

};

inline object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent) {
	return object_ptr<TitleWidget>(parent);
}

inline int PreviewTitleHeight() {
	return Window::Theme::DefaultPreviewTitleHeight();
}

inline void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	return Window::Theme::DefaultPreviewWindowFramePaint(preview, palette, body, outerWidth);
}

} // namespace Platform
