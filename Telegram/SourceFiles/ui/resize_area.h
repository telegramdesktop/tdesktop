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
		moveLeft()
			| rpl::start_with_next(
				std::forward<Callback>(callback),
				lifetime());
	}

	rpl::producer<> moveFinished() const {
		return _moveFinished.events();
	}
	template <typename Callback>
	void addMoveFinishedCallback(Callback &&callback) {
		moveFinished()
			| rpl::start_with_next(
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
