/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"
#include "editor/editor_link_pill.h"
#include "editor/photo_editor_inner_common.h"
#include "editor/scene/scene_item_base.h"
#include "editor/scene/scene_item_location.h"
#include "editor/scene/scene_item_weather.h"

#include <QtCore/QJsonArray>

namespace Editor {

class Scene;

struct SceneItemSticker {
	DocumentId documentId = 0;
	QString setShortName;
	ItemBase::Placement placement;
};

struct SceneItemText {
	QString text;
	QColor color;
	float64 fontSize = 0.;
	TextStyle style = TextStyle::Plain;
	TextTypeface typeface = TextTypeface::Default;
	TextAlignment alignment = TextAlignment::Center;
	ItemBase::Placement placement;
};

struct SceneItemShape {
	ShapeType shape = ShapeType::Circle;
	QColor color;
	float64 strokeWidth = 1.;
	bool fill = false;
	float64 bend = 0.;
	ItemBase::Placement placement;
};

struct SceneItemImage {
	QString file;
	float64 dpr = 1.;
	ItemBase::Placement placement;
};

struct SceneItemLine {
	QString file;
	float64 dpr = 1.;
	float64 z = 0.;
	QPointF position;
};

struct SceneItemMessage {
	uint64 peerBare = 0;
	int64 msgBare = 0;
	std::optional<bool> dark;
	ItemBase::Placement placement;
};

struct SceneItemVideo {
	QString file;
	crl::time duration = 0;
	bool round = false;
	bool hasAudio = false;
	crl::time from = 0;
	crl::time till = 0;
	float64 volume = 1.;
	ItemBase::Placement placement;
};

struct SceneItemLink {
	LinkPreview preview;
	LinkStyle style = LinkStyle::Framed;
	ItemBase::Placement placement;
};

struct SceneItemReaction {
	Data::ReactionId reaction;
	bool dark = false;
	ItemBase::Placement placement;
};

struct SceneItemLocation {
	LocationData location;
	LinkStyle style = LinkStyle::Framed;
	ItemBase::Placement placement;
};

struct SceneItemWeather {
	WeatherData weather;
	ItemBase::Placement placement;
};

struct SceneItems {
	std::vector<SceneItemSticker> stickers;
	std::vector<SceneItemText> texts;
	std::vector<SceneItemShape> shapes;
	std::vector<SceneItemImage> images;
	std::vector<SceneItemLine> lines;
	std::vector<SceneItemMessage> messages;
	std::vector<SceneItemVideo> videos;
	std::vector<SceneItemLink> links;
	std::vector<SceneItemReaction> reactions;
	std::vector<SceneItemLocation> locations;
	std::vector<SceneItemWeather> weather;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] float64 maxZ() const;

	friend inline bool operator==(
		const SceneItems &,
		const SceneItems &) = default;
};

struct SceneDraftFiles {
	Fn<int(const QImage &image)> image;
	Fn<int(
		const QString &path,
		const QByteArray &content,
		const QString &suffix)> file;
};

[[nodiscard]] SceneItems CollectSceneItems(
	not_null<const Scene*> scene,
	const SceneDraftFiles &files);

[[nodiscard]] QJsonArray SerializeSceneItems(const SceneItems &items);
[[nodiscard]] SceneItems ParseSceneItems(
	const QJsonArray &items,
	Fn<QString(int index)> file);

[[nodiscard]] uint64 SceneFingerprint(not_null<const Scene*> scene);

} // namespace Editor
