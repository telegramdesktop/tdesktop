/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {

enum class SlideDirection {
	FromRight,
	FromLeft,
};

class SlideAnimation {
public:
	void paintContents(Painter &p, const QRect &update) const;

	void setDirection(SlideDirection direction);
	void setPixmaps(const QPixmap &oldContentCache, const QPixmap &newContentCache);
	void setTopBarShadow(bool enabled);
	void setWithFade(bool withFade);

	using RepaintCallback = base::lambda<void()>;
	void setRepaintCallback(RepaintCallback &&callback);

	using FinishedCallback = base::lambda<void()>;
	void setFinishedCallback(FinishedCallback &&callback);

	void start();

	static const anim::transition &transition() {
		return anim::easeOutCirc;
	}

private:
	void animationCallback();

	SlideDirection _direction = SlideDirection::FromRight;
	bool _topBarShadowEnabled = false;
	bool _withFade = false;

	mutable Animation _animation;
	QPixmap _cacheUnder, _cacheOver;

	RepaintCallback _repaintCallback;
	FinishedCallback _finishedCallback;

};

} // namespace Window
