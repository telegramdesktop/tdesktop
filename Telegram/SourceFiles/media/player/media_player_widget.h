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

#include "ui/boxshadow.h"

class ScrollArea;

namespace Media {
namespace Player {

class CoverWidget;
class ListWidget;

class Widget : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	Widget(QWidget *parent);

	bool overlaps(const QRect &globalRect);

	void otherEnter();
	void otherLeave();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;

	bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
	void onShowStart();
	void onHideStart();
	void onScroll();

	void onWindowActiveChanged();

private:
	void hidingFinished();
	int contentLeft() const;
	int contentWidth() const {
		return width() - contentLeft();
	}

	void startAnimation();

	bool _hiding = false;

	QPixmap _cache;
	FloatAnimation _a_appearance;

	QTimer _hideTimer, _showTimer;

	Ui::RectShadow _shadow;
	ChildWidget<CoverWidget> _cover;
	ChildWidget<ListWidget> _list = { nullptr };
	ChildWidget<ScrollArea> _scroll = { nullptr };


};

} // namespace Clip
} // namespace Media
