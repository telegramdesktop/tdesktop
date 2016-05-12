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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/slide_animation.h"

class ScrollArea;

namespace Profile {

class InnerWidget;
class Widget final : public TWidget {
	Q_OBJECT

public:
	Widget(QWidget *parent, PeerData *peer);

	PeerData *peer() const;

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(const QRect &newGeometry, int topDelta);

	void showAnimated(SlideDirection direction, const QPixmap &oldContentCache);

	void setInnerFocus();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	// QWidget::update() method is overloaded and we need template deduction.
	void repaintCallback() {
		update();
	}
	void showFinished();

	ChildWidget<ScrollArea> _scroll;
	ChildWidget<InnerWidget> _inner;
	ChildWidget<PlainShadow> _sideShadow;

	std_::unique_ptr<SlideAnimation> _showAnimation;

	// Saving here topDelta in resizeWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

};

} // namespace Profile
