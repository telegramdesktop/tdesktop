/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_story_areas.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_item_link.h"
#include "editor/scene/scene_item_location.h"
#include "editor/scene/scene_item_reaction.h"

namespace Editor {
namespace {

constexpr auto kLocationRadiusRatio = 0.2;

[[nodiscard]] Data::StoryArea AreaOf(not_null<ItemBase*> item, QSize canvas) {
	const auto content = item->contentRect();
	const auto scale = item->scale();
	const auto center = item->pos();
	const auto width = std::min(
		content.width() * scale / canvas.width(),
		1.);
	const auto height = std::min(
		content.height() * scale / canvas.height(),
		1.);
	const auto x = std::clamp(center.x() / canvas.width(), 0., 1.);
	const auto y = std::clamp(center.y() / canvas.height(), 0., 1.);
	return {
		.geometry = QRectF(x - width / 2., y - height / 2., width, height),
		.rotation = item->rotation(),
	};
}

} // namespace

StoryAreas CollectStoryAreas(not_null<const Scene*> scene, QSize canvas) {
	auto result = StoryAreas();
	if (canvas.isEmpty()) {
		return result;
	}
	for (const auto &item : scene->items(Qt::AscendingOrder)) {
		if (!item->isNormalStatus()) {
			continue;
		} else if (item->type() == ItemLink::Type) {
			const auto link = static_cast<ItemLink*>(item.get());
			result.urls.push_back({
				.area = AreaOf(link, canvas),
				.url = link->link().url,
			});
		} else if (item->type() == ItemReaction::Type) {
			const auto reaction = static_cast<ItemReaction*>(item.get());
			result.reactions.push_back({
				.area = AreaOf(reaction, canvas),
				.reaction = reaction->reaction(),
				.flipped = reaction->flipped(),
				.dark = reaction->dark(),
			});
		} else if (item->type() == ItemLocation::Type) {
			const auto location = static_cast<ItemLocation*>(item.get());
			const auto &data = location->location();
			const auto point = Data::LocationPoint::FromCoordinates(
				data.lat,
				data.lon);
			if (!point) {
				continue;
			}
			auto area = AreaOf(location, canvas);
			area.radius = kLocationRadiusRatio
				* location->contentRect().height()
				* location->scale()
				/ canvas.width()
				* 100.;
			result.locations.push_back({
				.area = std::move(area),
				.point = *point,
				.title = data.title,
				.address = data.address,
				.countryIso2 = data.countryIso2,
				.city = data.city,
				.provider = data.provider,
				.venueId = data.venueId,
				.venueType = data.venueType,
				.queryId = data.queryId,
				.inlineResultId = data.inlineResultId,
			});
		}
	}
	return result;
}

} // namespace Editor
