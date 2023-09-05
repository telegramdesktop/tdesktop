/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/effects/reaction_fly_animation.h"
#include "ui/rp_widget.h"

namespace Ui {

struct ReactionFlyCenter;

class EmojiFlyAnimation {
public:
	EmojiFlyAnimation(
		not_null<RpWidget*> body,
		not_null<Data::Reactions*> owner,
		Ui::ReactionFlyAnimationArgs &&args,
		Fn<void()> repaint,
		Fn<QColor()> textColor,
		Data::CustomEmojiSizeTag tag);

	[[nodiscard]] not_null<Ui::RpWidget*> layer();
	[[nodiscard]] bool finished() const;

	void repaint();
	bool paintBadgeFrame(not_null<QWidget*> widget);
	[[nodiscard]] ReactionFlyCenter grabBadgeCenter();

private:
	const int _flySize = 0;
	Fn<QColor()> _textColor;
	Ui::ReactionFlyAnimation _fly;
	Ui::RpWidget _layer;
	QRect _area;
	bool _areaUpdated = false;
	QPointer<QWidget> _target;

};

} // namespace Ui
