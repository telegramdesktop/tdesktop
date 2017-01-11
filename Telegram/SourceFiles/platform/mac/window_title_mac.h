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
class PlainShadow;
} // namespace Ui

namespace Platform {

class MainWindow;

class TitleWidget : public Window::TitleWidget, private base::Subscriber {
public:
	TitleWidget(MainWindow *parent, int height);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	object_ptr<Ui::PlainShadow> _shadow;
	QFont _font;

};

object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent);

int PreviewTitleHeight();
void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Platform
