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
		Blur,
	};
	float64 sizeRatio = 0.;
	QColor color;
	Tool tool = Tool::Pen;
};

enum class TextStyle : uchar {
	Framed,
	SemiTransparent,
	Plain,
	Opaque,
};

enum class TextTypeface : uchar {
	Default,
	Italic,
	Serif,
	Condensed,
	Monospace,
};

enum class TextAlignment : uchar {
	Center,
	Left,
	Right,
};

struct TextPrefs {
	TextStyle style = TextStyle::Plain;
	TextTypeface typeface = TextTypeface::Default;
	TextAlignment alignment = TextAlignment::Center;
	float64 sizeRatio = 0.;

	friend inline bool operator==(
		const TextPrefs &,
		const TextPrefs &) = default;
};

enum class ShapeType : uchar {
	Circle,
	Rectangle,
	Star,
	Bubble,
	Arrow,
};

struct ShapeRequest {
	enum class Action : uchar {
		Arm,
		Immediate,
		Cancel,
	};
	ShapeType shape = ShapeType::Circle;
	Action action = Action::Arm;
};

enum class SaveState {
	Save,
	Keep,
};

} // namespace Editor
