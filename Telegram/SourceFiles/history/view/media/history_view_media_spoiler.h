/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/chat/message_bubble.h"
#include "ui/effects/animations.h"

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

} // namespace HistoryView
