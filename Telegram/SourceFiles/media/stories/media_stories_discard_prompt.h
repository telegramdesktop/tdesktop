/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Media::Stories {

struct DiscardStoryArgs {
	bool draft = false;
	bool allowDraft = true;
	Fn<void()> discard;
	Fn<void()> saveDraft;
};

void ShowDiscardStoryPrompt(
	not_null<Ui::RpWidget*> parent,
	DiscardStoryArgs args);

} // namespace Media::Stories
