/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

class ResizeArea : public RpWidget {
public:
	ResizeArea(QWidget *parent) : RpWidget(parent) {
		setCursor(style::cur_sizehor);
	}

	rpl::producer<int> moveLeft() const {
		return _moveLeft.events();
	}
	template <typename Callback>
	void addMoveLeftCallback(Callback &&callback) {
		moveLeft(
		) | rpl::start_with_next(
			std::forward<Callback>(callback),
			lifetime());
	}

	rpl::producer<> moveFinished() const {
		return _moveFinished.events();
	}
	template <typename Callback>
	void addMoveFinishedCallback(Callback &&callback) {
		moveFinished(
		) | rpl::start_with_next(
			std::forward<Callback>(callback),
			lifetime());
	}

	~ResizeArea() {
		moveFinish();
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			_moving = true;
			_moveStartLeft = e->pos().x();
		}
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			moveFinish();
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (_moving) {
			_moveLeft.fire(e->globalPos().x() - _moveStartLeft);
		}
	}

private:
	void moveFinish() {
		if (base::take(_moving)) {
			_moveFinished.fire({});
		}
	}

	rpl::event_stream<int> _moveLeft;
	rpl::event_stream<> _moveFinished;
	int _moveStartLeft = 0;
	bool _moving = false;

};

} // namespace Ui
