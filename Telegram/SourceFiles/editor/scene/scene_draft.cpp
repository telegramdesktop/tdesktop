/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_draft.h"

#include "mtproto/core_types.h"
#include "data/data_document.h"
#include "editor/editor_message_source.h"
#include "editor/scene/scene.h"
#include "editor/scene/scene_item_image.h"
#include "editor/scene/scene_item_line.h"
#include "editor/scene/scene_item_link.h"
#include "editor/scene/scene_item_location.h"
#include "editor/scene/scene_item_message.h"
#include "editor/scene/scene_item_reaction.h"
#include "editor/scene/scene_item_shape.h"
#include "editor/scene/scene_item_sticker.h"
#include "editor/scene/scene_item_text.h"
#include "editor/scene/scene_item_video.h"
#include "editor/scene/scene_item_weather.h"
#include "history/history_item.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Editor {
namespace {

constexpr auto kNoFile = -1;

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
	result.flipped = (array.size() > 7) && (array.at(7).toInt() == 1);
	return result;
}

[[nodiscard]] QJsonArray SerializePoint(QPointF point) {
	return QJsonArray{ point.x(), point.y() };
}

[[nodiscard]] QPointF DeserializePoint(const QJsonArray &array) {
	return (array.size() == 2)
		? QPointF(array.at(0).toDouble(), array.at(1).toDouble())
		: QPointF();
}

[[nodiscard]] QString SerializeColor(const QColor &color) {
	return color.isValid() ? color.name(QColor::HexRgb) : QString();
}

[[nodiscard]] QColor DeserializeColor(const QString &name) {
	return QColor(name);
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

[[nodiscard]] std::optional<bool> DeserializeDark(int value) {
	return (value < 0) ? std::nullopt : std::optional<bool>(value == 1);
}

} // namespace

bool SceneItems::empty() const {
	return stickers.empty()
		&& texts.empty()
		&& shapes.empty()
		&& images.empty()
		&& lines.empty()
		&& messages.empty()
		&& videos.empty()
		&& links.empty()
		&& reactions.empty()
		&& locations.empty()
		&& weather.empty();
}

SceneItems CollectSceneItems(
		not_null<const Scene*> scene,
		const SceneDraftFiles &files) {
	auto result = SceneItems();
	auto storeImage = [&](const QImage &image) {
		return files.image ? files.image(image) : kNoFile;
	};
	auto storeFile = [&](const QString &path,
			const QByteArray &content,
			const QString &suffix) {
		return files.file ? files.file(path, content, suffix) : kNoFile;
	};
	for (const auto &item : scene->items(Qt::AscendingOrder)) {
		if (!item->isNormalStatus()) {
			continue;
		} else if (item->type() == ItemLine::Type) {
			const auto line = static_cast<ItemLine*>(item.get());
			const auto index = storeImage(line->pixmap().toImage());
			if (index >= 0) {
				result.lines.push_back({
					.file = QString::number(index),
					.position = line->pos(),
				});
			}
			continue;
		}
		const auto base = static_cast<ItemBase*>(item.get());
		const auto placement = base->placement();
		if (item->type() == ItemSticker::Type) {
			const auto sticker = static_cast<ItemSticker*>(item.get());
			result.stickers.push_back({
				.documentId = sticker->sticker()->id,
				.placement = placement,
			});
		} else if (item->type() == ItemText::Type) {
			const auto text = static_cast<ItemText*>(item.get());
			result.texts.push_back({
				.text = text->text(),
				.color = text->color(),
				.fontSize = text->fontSize(),
				.style = text->textStyle(),
				.typeface = text->typeface(),
				.alignment = text->alignment(),
				.placement = placement,
			});
		} else if (item->type() == ItemShape::Type) {
			const auto shape = static_cast<ItemShape*>(item.get());
			result.shapes.push_back({
				.shape = shape->shapeType(),
				.color = shape->color(),
				.strokeWidth = shape->strokeWidth(),
				.fill = shape->fill(),
				.bend = shape->bend(),
				.placement = placement,
			});
		} else if (item->type() == ItemImage::Type) {
			const auto image = static_cast<ItemImage*>(item.get());
			const auto index = storeImage(image->pixmap().toImage());
			if (index >= 0) {
				result.images.push_back({
					.file = QString::number(index),
					.placement = placement,
				});
			}
		} else if (item->type() == ItemMessage::Type) {
			const auto message = static_cast<ItemMessage*>(item.get());
			const auto &source = message->source();
			const auto history = source->item();
			if (!history) {
				continue;
			}
			const auto fullId = history->fullId();
			result.messages.push_back({
				.peerBare = fullId.peer.value,
				.msgBare = fullId.msg.bare,
				.dark = message->dark(),
				.placement = placement,
			});
		} else if (item->type() == ItemVideo::Type) {
			const auto video = static_cast<ItemVideo*>(item.get());
			const auto clip = video->videoClip();
			if (!clip) {
				continue;
			}
			const auto &source = *clip->source();
			if (source.path.isEmpty() && source.content.isEmpty()) {
				continue;
			}
			const auto index = storeFile(
				source.path,
				source.content,
				source.round ? u"webm"_q : u"mp4"_q);
			if (index < 0) {
				continue;
			}
			const auto trim = clip->trim();
			result.videos.push_back({
				.file = QString::number(index),
				.duration = clip->duration(),
				.round = source.round,
				.hasAudio = clip->hasAudio(),
				.from = trim.from,
				.till = trim.till,
				.volume = clip->volume(),
				.placement = placement,
			});
		} else if (item->type() == ItemLink::Type) {
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

QJsonArray SerializeSceneItems(const SceneItems &items) {
	auto result = QJsonArray();
	const auto placement = [](const auto &item) {
		return SerializePlacement(item.placement);
	};
	for (const auto &sticker : items.stickers) {
		result.append(QJsonObject{
			{ u"kind"_q, u"sticker"_q },
			{ u"doc"_q, qint64(sticker.documentId) },
			{ u"p"_q, placement(sticker) },
		});
	}
	for (const auto &text : items.texts) {
		result.append(QJsonObject{
			{ u"kind"_q, u"text"_q },
			{ u"text"_q, text.text },
			{ u"color"_q, SerializeColor(text.color) },
			{ u"size"_q, text.fontSize },
			{ u"style"_q, int(text.style) },
			{ u"typeface"_q, int(text.typeface) },
			{ u"align"_q, int(text.alignment) },
			{ u"p"_q, placement(text) },
		});
	}
	for (const auto &shape : items.shapes) {
		result.append(QJsonObject{
			{ u"kind"_q, u"shape"_q },
			{ u"shape"_q, int(shape.shape) },
			{ u"color"_q, SerializeColor(shape.color) },
			{ u"stroke"_q, shape.strokeWidth },
			{ u"fill"_q, shape.fill },
			{ u"bend"_q, shape.bend },
			{ u"p"_q, placement(shape) },
		});
	}
	for (const auto &image : items.images) {
		result.append(QJsonObject{
			{ u"kind"_q, u"image"_q },
			{ u"file"_q, image.file },
			{ u"p"_q, placement(image) },
		});
	}
	for (const auto &line : items.lines) {
		result.append(QJsonObject{
			{ u"kind"_q, u"line"_q },
			{ u"file"_q, line.file },
			{ u"pos"_q, SerializePoint(line.position) },
		});
	}
	for (const auto &message : items.messages) {
		result.append(QJsonObject{
			{ u"kind"_q, u"message"_q },
			{ u"peer"_q, QString::number(message.peerBare) },
			{ u"msgid"_q, message.msgBare },
			{ u"dark"_q, message.dark
				? (*message.dark ? 1 : 0)
				: -1 },
			{ u"p"_q, placement(message) },
		});
	}
	for (const auto &video : items.videos) {
		result.append(QJsonObject{
			{ u"kind"_q, u"video"_q },
			{ u"file"_q, video.file },
			{ u"duration"_q, qint64(video.duration) },
			{ u"round"_q, video.round },
			{ u"audio"_q, video.hasAudio },
			{ u"from"_q, qint64(video.from) },
			{ u"till"_q, qint64(video.till) },
			{ u"vol"_q, video.volume },
			{ u"p"_q, placement(video) },
		});
	}
	for (const auto &link : items.links) {
		result.append(QJsonObject{
			{ u"kind"_q, u"link"_q },
			{ u"preview"_q, SerializeLinkPreview(link.preview) },
			{ u"style"_q, int(link.style) },
			{ u"p"_q, placement(link) },
		});
	}
	for (const auto &reaction : items.reactions) {
		result.append(QJsonObject{
			{ u"kind"_q, u"reaction"_q },
			{ u"reaction"_q, SerializeReactionId(reaction.reaction) },
			{ u"dark"_q, reaction.dark },
			{ u"p"_q, placement(reaction) },
		});
	}
	for (const auto &location : items.locations) {
		result.append(QJsonObject{
			{ u"kind"_q, u"location"_q },
			{ u"loc"_q, SerializeLocation(location.location) },
			{ u"style"_q, int(location.style) },
			{ u"p"_q, placement(location) },
		});
	}
	for (const auto &weather : items.weather) {
		result.append(QJsonObject{
			{ u"kind"_q, u"weather"_q },
			{ u"weather"_q, SerializeWeather(weather.weather) },
			{ u"p"_q, placement(weather) },
		});
	}
	return result;
}

SceneItems ParseSceneItems(
		const QJsonArray &items,
		Fn<QString(int index)> file) {
	auto result = SceneItems();
	for (const auto &value : items) {
		const auto object = value.toObject();
		const auto kind = object[u"kind"_q].toString();
		const auto placement = DeserializePlacement(object[u"p"_q].toArray());
		const auto path = [&]() -> QString {
			const auto index = object[u"file"_q].toString().toInt();
			return (index >= 0) ? file(index) : QString();
		};
		if (kind == u"sticker"_q) {
			result.stickers.push_back({
				.documentId = DocumentId(object[u"doc"_q].toInteger()),
				.placement = placement,
			});
		} else if (kind == u"text"_q) {
			result.texts.push_back({
				.text = object[u"text"_q].toString(),
				.color = DeserializeColor(object[u"color"_q].toString()),
				.fontSize = object[u"size"_q].toDouble(),
				.style = TextStyle(object[u"style"_q].toInt()),
				.typeface = TextTypeface(object[u"typeface"_q].toInt()),
				.alignment = TextAlignment(object[u"align"_q].toInt()),
				.placement = placement,
			});
		} else if (kind == u"shape"_q) {
			result.shapes.push_back({
				.shape = ShapeType(object[u"shape"_q].toInt()),
				.color = DeserializeColor(object[u"color"_q].toString()),
				.strokeWidth = object[u"stroke"_q].toDouble(1.),
				.fill = object[u"fill"_q].toBool(),
				.bend = object[u"bend"_q].toDouble(),
				.placement = placement,
			});
		} else if (kind == u"image"_q) {
			result.images.push_back({
				.file = path(),
				.placement = placement,
			});
		} else if (kind == u"line"_q) {
			result.lines.push_back({
				.file = path(),
				.position = DeserializePoint(object[u"pos"_q].toArray()),
			});
		} else if (kind == u"message"_q) {
			result.messages.push_back({
				.peerBare = object[u"peer"_q].toString().toULongLong(),
				.msgBare = object[u"msgid"_q].toInteger(),
				.dark = DeserializeDark(object[u"dark"_q].toInt(-1)),
				.placement = placement,
			});
		} else if (kind == u"video"_q) {
			result.videos.push_back({
				.file = path(),
				.duration = object[u"duration"_q].toInteger(),
				.round = object[u"round"_q].toBool(),
				.hasAudio = object[u"audio"_q].toBool(),
				.from = object[u"from"_q].toInteger(),
				.till = object[u"till"_q].toInteger(),
				.volume = object[u"vol"_q].toDouble(1.),
				.placement = placement,
			});
		} else if (kind == u"link"_q) {
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
