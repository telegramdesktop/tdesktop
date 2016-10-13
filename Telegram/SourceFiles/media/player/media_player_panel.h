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

#include "ui/effects/rect_shadow.h"

class ScrollArea;

namespace Ui {
class GradientShadow;
} // namespace Ui

namespace Media {
namespace Player {

class CoverWidget;
class ListWidget;

class Panel : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	enum class Layout {
		Full,
		OnlyPlaylist,
	};
	Panel(QWidget *parent, Layout layout);

	void setLayout(Layout layout);
	bool overlaps(const QRect &globalRect);

	void otherEnter();
	void otherLeave();

	using PinCallback = base::lambda_unique<void()>;
	void setPinCallback(PinCallback &&callback);

	void ui_repaintHistoryItem(const HistoryItem *item);
	void itemRemoved(HistoryItem *item);

	~Panel();

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

	void onListHeightUpdated();

	void onWindowActiveChanged();

private:
	void updateSize();
	void appearanceCallback();
	void hidingFinished();
	int contentLeft() const;
	int contentWidth() const {
		return width() - contentLeft();
	}
	int contentHeight() const;

	void startAnimation();

	bool _hiding = false;

	QPixmap _cache;
	FloatAnimation _a_appearance;

	QTimer _hideTimer, _showTimer;

	Ui::RectShadow _shadow;
	ChildWidget<CoverWidget> _cover = { nullptr };
	ChildWidget<ScrollArea> _scroll;
	ChildWidget<Ui::GradientShadow> _scrollShadow;

};

} // namespace Clip
} // namespace Media
