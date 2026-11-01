/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_story.h"

namespace Editor {

class Scene;

struct StoryAreas {
	std::vector<Data::SuggestedReaction> reactions;
	std::vector<Data::UrlArea> urls;
	std::vector<Data::StoryLocation> locations;
	std::vector<Data::WeatherArea> weather;
	std::vector<Data::ChannelPost> channelPosts;
};

[[nodiscard]] StoryAreas CollectStoryAreas(
	not_null<const Scene*> scene,
	QSize canvas);

} // namespace Editor
