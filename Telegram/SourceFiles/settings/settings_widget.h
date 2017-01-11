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

class BoxLayerTitleShadow;

namespace Ui {
class ScrollArea;
class IconButton;
template <typename Widget>
class WidgetFadeWrap;
} // namespace Ui

namespace Settings {

class InnerWidget;
class FixedBar;

class Widget : public LayerWidget {
	Q_OBJECT

public:
	Widget(QWidget *parent);

	void parentResized() override;
	void showFinished() override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private slots:
	void onInnerHeightUpdated();
	void onScroll();

private:
	void resizeUsingInnerHeight(int newWidth, int newContentLeft);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::IconButton> _fixedBarClose;
	object_ptr<Ui::WidgetFadeWrap<BoxLayerTitleShadow>> _fixedBarShadow;

	int _contentLeft = 0;
	bool _roundedCorners = false;

};

} // namespace Settings
