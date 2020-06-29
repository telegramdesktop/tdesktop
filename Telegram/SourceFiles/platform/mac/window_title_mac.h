/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_window_title.h"

namespace Ui {
class PlainShadow;
} // namespace Ui

namespace Platform {

class MainWindow;

class TitleWidget : public Window::TitleWidget {
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
