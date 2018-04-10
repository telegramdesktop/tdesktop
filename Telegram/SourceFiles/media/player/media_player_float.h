/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Window {
class Controller;
} // namespace Window

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip

namespace Player {

class Float : public Ui::RpWidget, private base::Subscriber {
public:
	Float(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<HistoryItem*> item,
		base::lambda<void(bool visible)> toggleCallback,
		base::lambda<void(bool closed)> draggedCallback);

	HistoryItem *item() const {
		return _item;
	}
	void setOpacity(float64 opacity) {
		if (_opacity != opacity) {
			_opacity = opacity;
			update();
		}
	}
	float64 countOpacityByParent() const {
		return outRatio();
	}
	bool isReady() const {
		return (getReader() != nullptr);
	}
	void detach();
	bool detached() const {
		return !_item;
	}
	bool dragged() const {
		return _drag;
	}
	void resetMouseState() {
		_down = false;
		if (_drag) {
			finishDrag(false);
		}
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	float64 outRatio() const;
	Clip::Reader *getReader() const;
	Clip::Playback *getPlayback() const;
	void repaintItem();
	void prepareShadow();
	bool hasFrame() const;
	bool fillFrame();
	QRect getInnerRect() const;
	void finishDrag(bool closed);

	not_null<Window::Controller*> _controller;
	HistoryItem *_item = nullptr;
	base::lambda<void(bool visible)> _toggleCallback;

	float64 _opacity = 1.;

	QPixmap _shadow;
	QImage _frame;
	bool _down = false;
	QPoint _downPoint;

	bool _drag = false;
	QPoint _dragLocalPoint;
	base::lambda<void(bool closed)> _draggedCallback;

};

} // namespace Player
} // namespace Media
