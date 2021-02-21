/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QGraphicsScene>

namespace Editor {

struct PhotoEditorMode {
	enum class Mode {
		Transform,
		Paint,
	} mode = Mode::Transform;

	enum class Action {
		None,
		Save,
		Discard,
	} action = Action::None;
};

struct PhotoModifications {
	int angle = 0;
	bool flipped = false;
	QRect crop;
	std::shared_ptr<QGraphicsScene> paint = nullptr;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const;
	~PhotoModifications();

};

struct EditorData {
	enum class CropType {
		Rect,
		Ellipse,
	};

	CropType cropType = CropType::Rect;
	bool keepAspectRatio = false;
};

struct Brush {
	float sizeRatio = 0.;
	QColor color;
};

[[nodiscard]] QImage ImageModified(
	QImage image,
	const PhotoModifications &mods);

} // namespace Editor
