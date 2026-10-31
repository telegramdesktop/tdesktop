/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"
#include "editor/editor_link_pill.h"
#include "editor/scene/scene_item_base.h"
#include "editor/scene/scene_item_location.h"
#include "editor/scene/scene_item_weather.h"

#include <QtCore/QJsonArray>

namespace Editor {

class Scene;

struct SceneWidgetLink {
	LinkPreview preview;
	LinkStyle style = LinkStyle::Framed;
	ItemBase::Placement placement;
};

struct SceneWidgetReaction {
	Data::ReactionId reaction;
	bool dark = false;
	ItemBase::Placement placement;
};

struct SceneWidgetLocation {
	LocationData location;
	LinkStyle style = LinkStyle::Framed;
	ItemBase::Placement placement;
};

struct SceneWidgetWeather {
	WeatherData weather;
	ItemBase::Placement placement;
};

struct SceneWidgets {
	std::vector<SceneWidgetLink> links;
	std::vector<SceneWidgetReaction> reactions;
	std::vector<SceneWidgetLocation> locations;
	std::vector<SceneWidgetWeather> weather;

	[[nodiscard]] bool empty() const {
		return links.empty()
			&& reactions.empty()
			&& locations.empty()
			&& weather.empty();
	}

	friend inline bool operator==(
		const SceneWidgets &,
		const SceneWidgets &) = default;
};

[[nodiscard]] SceneWidgets CollectSceneWidgets(not_null<const Scene*> scene);

[[nodiscard]] QJsonArray SerializeSceneWidgets(const SceneWidgets &widgets);
[[nodiscard]] SceneWidgets ParseSceneWidgets(const QJsonArray &widgets);

} // namespace Editor
