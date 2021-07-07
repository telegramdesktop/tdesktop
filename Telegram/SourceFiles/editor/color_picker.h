/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/photo_editor_inner_common.h"
#include "ui/effects/animations.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

class ColorPicker final {
public:
	struct OutlinedStop {
		std::optional<int> stopPos = std::nullopt;
		std::optional<int> prevStopPos = std::nullopt;
		std::optional<int> nextStopPos = std::nullopt;
	};

	ColorPicker(not_null<Ui::RpWidget*> parent, const Brush &savedBrush);

	void moveLine(const QPoint &position);
	void setVisible(bool visible);
	bool preventHandleKeyPress() const;

	rpl::producer<Brush> saveBrushRequests() const;

private:
	void paintCircle(Painter &p);
	void paintOutline(Painter &p, const QRectF &rect);
	QColor positionToColor(int x) const;
	int colorToPosition(const QColor &color) const;
	int circleHeight(float64 progress = 0.) const;
	void updateMousePosition(const QPoint &pos, float64 progress);

	const QColor _circleColor;
	const int _width;
	const int _lineHeight;

	const base::unique_qptr<Ui::RpWidget> _colorLine;
	const base::unique_qptr<Ui::RpWidget> _canvasForCircle;

	const QGradientStops _gradientStops;
	const OutlinedStop _outlinedStop;
	const QBrush _gradientBrush;

	struct {
		QPoint pos;
		bool pressed = false;
	} _down;
	Brush _brush;

	Ui::Animations::Simple _circleAnimation;

	rpl::event_stream<Brush> _saveBrushRequests;

};

} // namespace Editor
