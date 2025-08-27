/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/message_bubble.h"
#include "ui/effects/animations.h"

namespace Ui {
class SpoilerAnimation;
} // namespace Ui

namespace HistoryView {

struct MediaSpoiler {
	ClickHandlerPtr link;
	std::unique_ptr<Ui::SpoilerAnimation> animation;
	QImage cornerCache;
	QImage background;
	std::optional<Ui::BubbleRounding> backgroundRounding;
	Ui::Animations::Simple revealAnimation;
	bool revealed = false;
};

struct MediaSpoilerTag {
	uint64 price : 63 = 0;
	uint64 sensitive : 1 = 0;
	QImage cache;
	QColor darken;
	QColor fg;
	QColor star;
	ClickHandlerPtr link;
};

} // namespace HistoryView
