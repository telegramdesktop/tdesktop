/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"
#include "ui/rp_widget.h"

namespace Ui {

class FadeAnimation {
public:
	FadeAnimation(TWidget *widget, float64 scale = 1.);

	bool paint(Painter &p);
	void refreshCache();

	using FinishedCallback = Fn<void()>;
	void setFinishedCallback(FinishedCallback &&callback);

	using UpdatedCallback = Fn<void(float64)>;
	void setUpdatedCallback(UpdatedCallback &&callback);

	void show();
	void hide();

	void fadeIn(int duration);
	void fadeOut(int duration);

	void finish() {
		stopAnimation();
	}

	bool animating() const {
		return _animation.animating();
	}
	bool visible() const {
		return _visible;
	}

private:
	void startAnimation(int duration);
	void stopAnimation();

	void updateCallback();
	QPixmap grabContent();

	TWidget *_widget = nullptr;
	float64 _scale = 1.;

	Animation _animation;
	QSize _size;
	QPixmap _cache;
	bool _visible = false;

	FinishedCallback _finishedCallback;
	UpdatedCallback _updatedCallback;

};

} // namespace Ui
