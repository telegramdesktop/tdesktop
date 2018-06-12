/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	void setCloseClickHandler(Fn<void()> callback);
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
	void scrollToY(int y);

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
