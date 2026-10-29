/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_story_areas.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_item_link.h"

namespace Editor {
namespace {

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
		}
	}
	return result;
}

} // namespace Editor
