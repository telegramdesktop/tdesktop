/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/event_filter.h"
#include "ui/effects/animations.h"

namespace Ui {

class FlatLabel;

class SkeletonAnimation final {
public:
	explicit SkeletonAnimation(not_null<FlatLabel*> label);

	void start();
	void stop();
	[[nodiscard]] bool active() const;

private:
	void recompute();
	base::EventFilterResult paint();

	const not_null<FlatLabel*> _label;
	Ui::Animations::Basic _animation;
	std::vector<int> _lineWidths;
	bool _active = false;
	rpl::lifetime _lifetime;

};

} // namespace Ui
