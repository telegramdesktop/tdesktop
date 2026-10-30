/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_stories.h"

namespace Data {
class Story;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

struct SendStoryBoxArgs {
	not_null<Window::SessionController*> controller;
	not_null<PeerData*> peer;
	std::optional<Api::SendStoryMedia> media;
	TextWithTags caption;
	int period = Api::kStoryDefaultPeriod;
	std::vector<Data::SuggestedReaction> reactions;
	std::vector<Data::UrlArea> urls;
	DocumentData *music = nullptr;
	Data::Story *editing = nullptr;
	Fn<void()> posted;
};

void SendStoryBox(not_null<Ui::GenericBox*> box, SendStoryBoxArgs &&args);
