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
#include "ui/peer/color_sample.h"

namespace Ui {
class RpWidget;
class Show;
} // namespace Ui

namespace Editor {

class ColorPicker final {
public:
	ColorPicker(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const std::array<Brush, 4> &savedBrushes,
		Brush::Tool savedTool);

	void moveLine(const QPoint &position);
	void setCanvasRect(const QRect &rect);
	void setVisible(bool visible);
	bool preventHandleKeyPress() const;

	rpl::producer<Brush> saveBrushRequests() const;

private:
	void paintSizeControl(QPainter &p);
	void rebuildPalette();
	void updateToolButtonsGeometry();
	void updateToolSelection(bool animated);
	void setTool(Brush::Tool tool);
	void storeCurrentBrush();
	void updateColorButtonColor(const QColor &color, bool animated);
	[[nodiscard]] QColor colorButtonColor() const;
	void updatePaletteGeometry();
	void setPaletteVisible(bool visible);
	void moveSizeControl(const QSize &size);
	void updateSizeControlExpanded();
	void updateSizeControlMousePosition(int y);
	void updateSizeControlPositionFromRatio(bool animated);
	[[nodiscard]] int sizeControlShapeTop() const;
	[[nodiscard]] int sizeControlShapeBottom() const;
	[[nodiscard]] int sizeControlTop() const;
	[[nodiscard]] int sizeControlBottom() const;
	[[nodiscard]] float sizeControlRatioFromY(int y) const;
	[[nodiscard]] int sizeControlYFromRatio(float ratio) const;
	[[nodiscard]] QRectF sizeControlHandleRect(float64 progress) const;
	[[nodiscard]] QRectF sizeControlHitRect(float64 progress) const;
	[[nodiscard]] QPainterPath sizeControlShapePath(float64 progress) const;
	[[nodiscard]] float64 sizeControlCurrentCenterX(float64 progress) const;
	[[nodiscard]] float64 sizeControlHandleSize() const;

	const not_null<Ui::RpWidget*> _parent;
	const std::shared_ptr<Ui::Show> _show;

	const base::unique_qptr<Ui::AbstractButton> _colorButton;
	const base::unique_qptr<Ui::RpWidget> _paletteWrap;
	const base::unique_qptr<Ui::RpWidget> _sizeControlHoverArea;
	const base::unique_qptr<Ui::RpWidget> _sizeControl;
	const base::unique_qptr<Ui::RpWidget> _toolSelection;
	std::vector<base::unique_qptr<Ui::AbstractButton>> _toolButtons;

	struct {
		int y = 0;
		bool pressed = false;
	} _sizeDown;
	bool _sizeHoverAreaHovered = false;
	bool _sizeControlHovered = false;
	bool _sizeControlExpanded = false;
	int _sizeControlPositionFrom = 0;
	int _sizeControlPositionTo = 0;
	QRect _canvasRect;
	QPoint _colorButtonCenter;
	bool _paletteVisible = false;
	Brush _brush;
	QColor _colorButtonFrom;
	QColor _colorButtonTo;
	std::array<Brush, 4> _toolBrushes;

	Ui::Animations::Simple _sizeControlAnimation;
	Ui::Animations::Simple _sizeControlPositionAnimation;
	Ui::Animations::Simple _colorButtonAnimation;
	Ui::Animations::Simple _toolSelectionAnimation;

	rpl::event_stream<Brush> _saveBrushRequests;

	std::vector<base::unique_qptr<Ui::ColorSample>> _paletteButtons;
	base::unique_qptr<Ui::AbstractButton> _palettePlus;
	QPoint _toolSelectionFrom;
	QPoint _toolSelectionTo;

};

} // namespace Editor
