/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_draft.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_item_link.h"
#include "editor/scene/scene_item_location.h"
#include "editor/scene/scene_item_reaction.h"
#include "editor/scene/scene_item_weather.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Editor {
namespace {

[[nodiscard]] QJsonArray SerializePlacement(
		const ItemBase::Placement &placement) {
	return QJsonArray{
		placement.position.x(),
		placement.position.y(),
		placement.rotation,
		placement.scale,
		placement.zValue,
		placement.size,
		placement.aspectRatio,
		placement.flipped ? 1 : 0,
	};
}

[[nodiscard]] ItemBase::Placement DeserializePlacement(
		const QJsonArray &array) {
	auto result = ItemBase::Placement();
	if (array.size() < 7) {
		return result;
	}
	result.position = QPointF(
		array.at(0).toDouble(),
		array.at(1).toDouble());
	result.rotation = array.at(2).toDouble();
	result.scale = array.at(3).toDouble(1.);
	result.zValue = array.at(4).toDouble();
	result.size = array.at(5).toDouble();
	result.aspectRatio = array.at(6).toDouble(1.);
	result.flipped = (array.at(7).toInt() == 1);
	return result;
}

[[nodiscard]] QJsonObject SerializeReactionId(Data::ReactionId id) {
	if (const auto custom = id.custom()) {
		return { { u"custom"_q, qint64(custom) } };
	}
	return { { u"emoji"_q, id.emoji() } };
}

[[nodiscard]] Data::ReactionId DeserializeReactionId(
		const QJsonObject &object) {
	const auto custom = DocumentId(object[u"custom"_q].toInteger());
	if (custom) {
		return Data::ReactionId{ custom };
	}
	return Data::ReactionId{ object[u"emoji"_q].toString() };
}

[[nodiscard]] QJsonObject SerializeLinkPreview(const LinkPreview &preview) {
	return {
		{ u"url"_q, preview.url },
		{ u"name"_q, preview.name },
		{ u"above"_q, preview.captionAbove },
		{ u"large"_q, preview.largePhoto },
		{ u"preview"_q, preview.preview },
		{ u"dark"_q, preview.dark },
	};
}

[[nodiscard]] LinkPreview DeserializeLinkPreview(const QJsonObject &object) {
	return {
		.url = object[u"url"_q].toString(),
		.name = object[u"name"_q].toString(),
		.captionAbove = object[u"above"_q].toBool(true),
		.largePhoto = object[u"large"_q].toBool(),
		.preview = object[u"preview"_q].toBool(true),
		.dark = object[u"dark"_q].toBool(),
	};
}

[[nodiscard]] QJsonObject SerializeLocation(const LocationData &data) {
	return {
		{ u"lat"_q, data.lat },
		{ u"lon"_q, data.lon },
		{ u"title"_q, data.title },
		{ u"address"_q, data.address },
		{ u"country"_q, data.countryIso2 },
		{ u"city"_q, data.city },
		{ u"provider"_q, data.provider },
		{ u"venueId"_q, data.venueId },
		{ u"venueType"_q, data.venueType },
		{ u"queryId"_q, qint64(data.queryId) },
		{ u"inlineResultId"_q, data.inlineResultId },
	};
}

[[nodiscard]] LocationData DeserializeLocation(const QJsonObject &object) {
	return {
		.lat = object[u"lat"_q].toDouble(),
		.lon = object[u"lon"_q].toDouble(),
		.title = object[u"title"_q].toString(),
		.address = object[u"address"_q].toString(),
		.countryIso2 = object[u"country"_q].toString(),
		.city = object[u"city"_q].toString(),
		.provider = object[u"provider"_q].toString(),
		.venueId = object[u"venueId"_q].toString(),
		.venueType = object[u"venueType"_q].toString(),
		.queryId = uint64(object[u"queryId"_q].toInteger()),
		.inlineResultId = object[u"inlineResultId"_q].toString(),
	};
}

[[nodiscard]] QString SerializeColor(const QColor &color) {
	return color.isValid() ? color.name(QColor::HexRgb) : QString();
}

[[nodiscard]] QColor DeserializeColor(const QString &name) {
	return QColor(name);
}

[[nodiscard]] QJsonObject SerializeWeather(const WeatherData &data) {
	return {
		{ u"emoji"_q, data.emoji },
		{ u"celsius"_q, data.celsius },
		{ u"color"_q, SerializeColor(data.color) },
	};
}

[[nodiscard]] WeatherData DeserializeWeather(const QJsonObject &object) {
	return {
		.emoji = object[u"emoji"_q].toString(),
		.celsius = object[u"celsius"_q].toDouble(),
		.color = DeserializeColor(object[u"color"_q].toString()),
	};
}

[[nodiscard]] LinkStyle DeserializeStyle(int value) {
	return ((value >= 0) && (value <= int(LinkStyle::Plain)))
		? LinkStyle(value)
		: LinkStyle::Framed;
}

} // namespace

SceneWidgets CollectSceneWidgets(not_null<const Scene*> scene) {
	auto result = SceneWidgets();
	for (const auto &item : scene->items(Qt::AscendingOrder)) {
		if (!item->isNormalStatus()) {
			continue;
		}
		const auto base = static_cast<ItemBase*>(item.get());
		const auto placement = base->placement();
		if (item->type() == ItemLink::Type) {
			const auto link = static_cast<ItemLink*>(item.get());
			result.links.push_back({
				.preview = link->link(),
				.style = link->style(),
				.placement = placement,
			});
		} else if (item->type() == ItemReaction::Type) {
			const auto reaction = static_cast<ItemReaction*>(item.get());
			result.reactions.push_back({
				.reaction = reaction->reaction(),
				.dark = reaction->dark(),
				.placement = placement,
			});
		} else if (item->type() == ItemLocation::Type) {
			const auto location = static_cast<ItemLocation*>(item.get());
			result.locations.push_back({
				.location = location->location(),
				.style = location->style(),
				.placement = placement,
			});
		} else if (item->type() == ItemWeather::Type) {
			const auto weather = static_cast<ItemWeather*>(item.get());
			result.weather.push_back({
				.weather = weather->weather(),
				.placement = placement,
			});
		}
	}
	return result;
}

QJsonArray SerializeSceneWidgets(const SceneWidgets &widgets) {
	auto result = QJsonArray();
	for (const auto &link : widgets.links) {
		result.append(QJsonObject{
			{ u"kind"_q, u"link"_q },
			{ u"preview"_q, SerializeLinkPreview(link.preview) },
			{ u"style"_q, int(link.style) },
			{ u"p"_q, SerializePlacement(link.placement) },
		});
	}
	for (const auto &reaction : widgets.reactions) {
		result.append(QJsonObject{
			{ u"kind"_q, u"reaction"_q },
			{ u"reaction"_q, SerializeReactionId(reaction.reaction) },
			{ u"dark"_q, reaction.dark },
			{ u"p"_q, SerializePlacement(reaction.placement) },
		});
	}
	for (const auto &location : widgets.locations) {
		result.append(QJsonObject{
			{ u"kind"_q, u"location"_q },
			{ u"loc"_q, SerializeLocation(location.location) },
			{ u"style"_q, int(location.style) },
			{ u"p"_q, SerializePlacement(location.placement) },
		});
	}
	for (const auto &weather : widgets.weather) {
		result.append(QJsonObject{
			{ u"kind"_q, u"weather"_q },
			{ u"weather"_q, SerializeWeather(weather.weather) },
			{ u"p"_q, SerializePlacement(weather.placement) },
		});
	}
	return result;
}

SceneWidgets ParseSceneWidgets(const QJsonArray &widgets) {
	auto result = SceneWidgets();
	for (const auto &value : widgets) {
		const auto object = value.toObject();
		const auto kind = object[u"kind"_q].toString();
		const auto placement = DeserializePlacement(object[u"p"_q].toArray());
		if (kind == u"link"_q) {
			result.links.push_back({
				.preview = DeserializeLinkPreview(
					object[u"preview"_q].toObject()),
				.style = DeserializeStyle(object[u"style"_q].toInt()),
				.placement = placement,
			});
		} else if (kind == u"reaction"_q) {
			result.reactions.push_back({
				.reaction = DeserializeReactionId(
					object[u"reaction"_q].toObject()),
				.dark = object[u"dark"_q].toBool(),
				.placement = placement,
			});
		} else if (kind == u"location"_q) {
			result.locations.push_back({
				.location = DeserializeLocation(object[u"loc"_q].toObject()),
				.style = DeserializeStyle(object[u"style"_q].toInt()),
				.placement = placement,
			});
		} else if (kind == u"weather"_q) {
			result.weather.push_back({
				.weather = DeserializeWeather(
					object[u"weather"_q].toObject()),
				.placement = placement,
			});
		}
	}
	return result;
}

} // namespace Editor
