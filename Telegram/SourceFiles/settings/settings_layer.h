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
#include "ui/rp_widget.h"

namespace Ui {
class ScrollArea;
class IconButton;
class FadeShadow;
} // namespace Ui

namespace Settings {

class FixedBar;
class LayerInner : public Ui::RpWidget {
public:
	LayerInner(QWidget *parent) : RpWidget(parent) {
	}

	virtual void resizeToWidth(int newWidth, int contentLeft) {
		TWidget::resizeToWidth(newWidth);
	}

};

class Layer : public Window::LayerWidget {
public:
	Layer();

	void setCloseClickHandler(base::lambda<void()> callback);
	void resizeToWidth(int newWidth, int newContentLeft);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	template <typename Widget>
	QPointer<Widget> setInnerWidget(object_ptr<Widget> widget) {
		auto result = QPointer<Widget>(widget);
		doSetInnerWidget(std::move(widget));
		return result;
	}

	void setTitle(const QString &title);
	void setRoundedCorners(bool roundedCorners) {
		_roundedCorners = roundedCorners;
	}

private:
	void doSetInnerWidget(object_ptr<LayerInner> widget);

	virtual void resizeUsingInnerHeight(int newWidth, int innerHeight) {
		resize(newWidth, height());
	}

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<LayerInner> _inner;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::IconButton> _fixedBarClose;
	object_ptr<Ui::FadeShadow> _fixedBarShadow;

	bool _roundedCorners = false;

};

} // namespace Settings
