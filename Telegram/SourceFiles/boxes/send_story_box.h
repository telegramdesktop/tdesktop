/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_stories.h"
#include "data/data_story_edit_sources.h"

class PeerData;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
class Story;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

namespace style {
struct SendStoryBoxStyle;
} // namespace style

namespace Window {
class SessionController;
} // namespace Window

struct SendStoryBoxArgs {
	Window::SessionController *controller = nullptr;
	not_null<PeerData*> peer;
	std::optional<Api::SendStoryMedia> media;
	TextWithTags caption;
	int period = Api::kStoryDefaultPeriod;
	std::vector<Data::SuggestedReaction> reactions;
	std::vector<Data::UrlArea> urls;
	std::vector<Data::StoryLocation> locations;
	std::vector<Data::WeatherArea> weather;
	std::vector<Data::ChannelPost> channelPosts;
	Data::Story *editing = nullptr;
	std::shared_ptr<Data::StorySourceSave> source;
	Fn<void()> posted;
	Fn<void()> uploaded;
	std::shared_ptr<ChatHelpers::Show> show;
	const style::SendStoryBoxStyle *style = nullptr;
};

void SendStoryBox(not_null<Ui::GenericBox*> box, SendStoryBoxArgs &&args);
