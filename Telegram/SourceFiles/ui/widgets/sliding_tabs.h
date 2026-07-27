/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animation_value.h"
#include "ui/rp_widget.h"

namespace Ui {

class SlideAnimation;
class VerticalLayout;

class SlidingTabs final : public RpWidget {
public:
	SlidingTabs(QWidget *parent, int count, style::color bg);
	~SlidingTabs();

	[[nodiscard]] not_null<VerticalLayout*> tab(int index) const;
	void showTab(int index, anim::type animated = anim::type::normal);

private:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

	void finishAnimating();
	void applyChildrenVisibleRange();

	const style::color _bg;
	std::vector<not_null<VerticalLayout*>> _tabs;
	std::unique_ptr<SlideAnimation> _animation;
	QRect _slideRect;
	int _shown = 0;
	int _animationHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

};

} // namespace Ui
