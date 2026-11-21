/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_stories.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

void StoryPostingLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	Api::CanSendStory::Type type);
