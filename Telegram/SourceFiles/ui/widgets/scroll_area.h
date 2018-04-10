/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include "ui/rp_widget.h"
#include "styles/style_widgets.h"

namespace Ui {

enum class TouchScrollState {
	Manual, // Scrolling manually with the finger on the screen
	Auto, // Scrolling automatically
	Acceleration // Scrolling automatically but a finger is on the screen
};

class ScrollArea;

struct ScrollToRequest {
	ScrollToRequest(int ymin, int ymax)
	: ymin(ymin)
	, ymax(ymax) {
	}

	int ymin = 0;
	int ymax = 0;

};

class ScrollShadow : public QWidget {
	Q_OBJECT

public:
	ScrollShadow(ScrollArea *parent, const style::ScrollArea *st);

	void paintEvent(QPaintEvent *e);

public slots:
	void changeVisibility(bool shown);

private:
	const style::ScrollArea *_st;

};

class ScrollBar : public TWidget {
	Q_OBJECT

public:
	ScrollBar(ScrollArea *parent, bool vertical, const style::ScrollArea *st);

	void recountSize();
	void updateBar(bool force = false);

	void hideTimeout(TimeMs dt);

private slots:
	void onValueChanged();
	void onRangeChanged();
	void onHideTimer();

signals:
	void topShadowVisibility(bool);
	void bottomShadowVisibility(bool);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	ScrollArea *area();

	void setOver(bool over);
	void setOverBar(bool overbar);
	void setMoving(bool moving);

	const style::ScrollArea *_st;

	bool _vertical = true;
	bool _hiding = false;
	bool _over = false;
	bool _overbar = false;
	bool _moving = false;
	bool _topSh = false;
	bool _bottomSh = false;

	QPoint _dragStart;
	QScrollBar *_connected;

	int32 _startFrom, _scrollMax;

	TimeMs _hideIn = 0;
	QTimer _hideTimer;

	Animation _a_over;
	Animation _a_barOver;
	Animation _a_opacity;

	QRect _bar;
};

class SplittedWidget : public Ui::RpWidget {
	Q_OBJECT

public:
	SplittedWidget(QWidget *parent) : RpWidget(parent) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}
	void setHeight(int32 newHeight) {
		resize(width(), newHeight);
		emit resizeOther();
	}
	void update(int x, int y, int w, int h) {
		update(QRect(x, y, w, h));
	}
	void update(const QRect&);
	void update(const QRegion&);
	void rtlupdate(const QRect &r) {
		update(myrtlrect(r));
	}
	void rtlupdate(int x, int y, int w, int h) {
		update(myrtlrect(x, y, w, h));
	}

public slots:
	void update() {
		update(0, 0, getFullWidth(), height());
	}

signals:
	void resizeOther();
	void updateOther(const QRect&);
	void updateOther(const QRegion&);

protected:
	void paintEvent(QPaintEvent *e) override; // paintEvent done through paintRegion

	int otherWidth() const {
		return _otherWidth;
	}
	int getFullWidth() const {
		return width() + otherWidth();
	}
	virtual void paintRegion(Painter &p, const QRegion &region, bool paintingOther) = 0;

private:
	int _otherWidth = 0;
	void setOtherWidth(int otherWidth) {
		_otherWidth = otherWidth;
	}
	void resize(int w, int h) {
		TWidget::resize(w, h);
	}
	friend class ScrollArea;
	friend class SplittedWidgetOther;

};

class SplittedWidgetOther;
class ScrollArea : public Ui::RpWidgetWrap<QScrollArea> {
	Q_OBJECT

public:
	ScrollArea(QWidget *parent, const style::ScrollArea &st = st::defaultScrollArea, bool handleTouch = true);

	int scrollWidth() const;
	int scrollHeight() const;
	int scrollLeftMax() const;
	int scrollTopMax() const;
	int scrollLeft() const;
	int scrollTop() const;

	template <typename Widget>
	QPointer<Widget> setOwnedWidget(object_ptr<Widget> widget) {
		auto result = QPointer<Widget>(widget);
		doSetOwnedWidget(std::move(widget));
		return result;
	}
	template <typename Widget>
	object_ptr<Widget> takeWidget() {
		return static_object_cast<Widget>(doTakeWidget());
	}

	void rangeChanged(int oldMax, int newMax, bool vertical);

	void updateBars();

	bool focusNextPrevChild(bool next) override;
	void setMovingByScrollBar(bool movingByScrollBar);

	bool viewportEvent(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	auto scrollTopValue() const {
		return _scrollTopUpdated.events_starting_with(scrollTop());
	}

	void scrollTo(ScrollToRequest request);
	void scrollToWidget(not_null<QWidget*> widget);

protected:
	bool eventFilter(QObject *obj, QEvent *e) override;

	void resizeEvent(QResizeEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void touchEvent(QTouchEvent *e);

	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

public slots:
	void scrollToY(int toTop, int toBottom = -1);
	void disableScroll(bool dis);
	void onScrolled();
	void onInnerResized();

	void onTouchTimer();
	void onTouchScrollTimer();

	void onResizeOther();
	void onUpdateOther(const QRect&);
	void onUpdateOther(const QRegion&);
	void onVerticalScroll();

signals:
	void scrolled();
	void innerResized();
	void scrollStarted();
	void scrollFinished();
	void geometryChanged();

protected:
	void scrollContentsBy(int dx, int dy) override;

private:
	void doSetOwnedWidget(object_ptr<TWidget> widget);
	object_ptr<TWidget> doTakeWidget();

	void setWidget(QWidget *widget);

	bool touchScroll(const QPoint &delta);

	void touchScrollUpdated(const QPoint &screenPos);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	bool _disabled = false;
	bool _movingByScrollBar = false;

	const style::ScrollArea &_st;
	object_ptr<ScrollBar> _horizontalBar, _verticalBar;
	object_ptr<ScrollShadow> _topShadow, _bottomShadow;
	int _horizontalValue, _verticalValue;

	bool _touchEnabled;
	QTimer _touchTimer;
	bool _touchScroll = false;
	bool _touchPress = false;
	bool _touchRightButton = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;

	TouchScrollState _touchScrollState = TouchScrollState::Manual;
	bool _touchPrevPosValid = false;
	bool _touchWaitingAcceleration = false;
	QPoint _touchSpeed;
	TimeMs _touchSpeedTime = 0;
	TimeMs _touchAccelerationTime = 0;
	TimeMs _touchTime = 0;
	QTimer _touchScrollTimer;

	bool _widgetAcceptsTouch = false;

	friend class SplittedWidgetOther;
	object_ptr<SplittedWidgetOther> _other = { nullptr };

	object_ptr<TWidget> _widget = { nullptr };

	rpl::event_stream<int> _scrollTopUpdated;

};

class SplittedWidgetOther : public TWidget {
public:
	SplittedWidgetOther(ScrollArea *parent) : TWidget(parent) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

protected:
	void paintEvent(QPaintEvent *e) override;

};

} // namespace Ui
