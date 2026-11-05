/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

struct StoryDraftsBoxArgs {
	PeerData *peer = nullptr;
	Fn<void(Fn<void()> close)> chooseMedia;
};

void ShowStoryDraftsBox(
	not_null<Window::SessionController*> controller,
	StoryDraftsBoxArgs args = {});
