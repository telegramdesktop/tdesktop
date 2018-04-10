/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class SlideAnimation {
public:
	void setSnapshots(QPixmap leftSnapshot, QPixmap rightSnapshot);

	void setOverflowHidden(bool hidden) {
		_overflowHidden = hidden;
	}

	template <typename Lambda>
	void start(bool slideLeft, Lambda &&updateCallback, float64 duration);

	void paintFrame(Painter &p, int x, int y, int outerWidth, TimeMs ms);

	bool animating() const {
		return _animation.animating();
	}

private:
	Animation _animation;
	QPixmap _leftSnapshot;
	QPixmap _rightSnapshot;
	bool _slideLeft = false;
	bool _overflowHidden = true;
	int _leftSnapshotWidth = 0;
	int _leftSnapshotHeight = 0;
	int _rightSnapshotWidth = 0;

};

template <typename Lambda>
void SlideAnimation::start(bool slideLeft, Lambda &&updateCallback, float64 duration) {
	_slideLeft = slideLeft;
	if (_slideLeft) {
		std::swap(_leftSnapshot, _rightSnapshot);
	}
	_leftSnapshotWidth = _leftSnapshot.width() / cIntRetinaFactor();
	_leftSnapshotHeight = _leftSnapshot.height() / cIntRetinaFactor();
	_rightSnapshotWidth = _rightSnapshot.width() / cIntRetinaFactor();
	_animation.start(std::forward<Lambda>(updateCallback), 0., 1., duration);
}

} // namespace Ui
