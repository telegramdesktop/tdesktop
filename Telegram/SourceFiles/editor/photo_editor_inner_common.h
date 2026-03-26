/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Editor {

class Scene;

struct PhotoEditorMode {
	enum class Mode {
		Transform,
		Paint,
		Out,
	} mode = Mode::Transform;

	enum class Action {
		None,
		Save,
		Discard,
	} action = Action::None;
};

struct Brush {
	enum class Tool : uchar {
		Pen,
		Arrow,
		Marker,
		Eraser,
	};
	float64 sizeRatio = 0.;
	QColor color;
	Tool tool = Tool::Pen;
};

enum class SaveState {
	Save,
	Keep,
};

} // namespace Editor
