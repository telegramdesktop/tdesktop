/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_base.h"

namespace Editor {

class ItemShape final : public ItemBase {
public:
	enum { Type = ItemBase::Type + 3 };

	ItemShape(
		ShapeType shape,
		const QColor &color,
		float64 strokeWidth,
		bool fill,
		Data data);
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	QRectF boundingRect() const override;
	int type() const override;

	[[nodiscard]] QColor color() const;
	void setColor(const QColor &color);
	void setStrokeWidth(float64 width);
	void setBend(float64 bend);

	[[nodiscard]] float64 defaultAspectRatio() const;
	void applyFrame(float64 width, float64 height);
	[[nodiscard]] bool applyDraftFrame(float64 width, float64 height);

	void save(SaveState state) override;
	void restore(SaveState state) override;
protected:
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(Data data) const override;
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
private:
	struct ArrowPoints {
		QPointF start;
		QPointF control;
		QPointF end;
		float64 headSide = 0.;
	};

	bool applyFrameSize(float64 width, float64 height, bool draft);
	[[nodiscard]] QRectF shapeRect() const;
	[[nodiscard]] float64 direction() const;
	[[nodiscard]] QPainterPath shapePath() const;
	[[nodiscard]] QPainterPath arrowShaftPath() const;
	[[nodiscard]] QPainterPath arrowHeadPath() const;
	[[nodiscard]] float64 arrowHeadSide(float64 width) const;
	[[nodiscard]] ArrowPoints arrowPoints() const;
	[[nodiscard]] QRectF bendHandleRect() const;
	[[nodiscard]] bool overBendHandle(const QPointF &pos) const;
	void updateBend(const QPointF &pos);
	void updateArrowFrame();

	const ShapeType _shape;
	QColor _color;
	float64 _strokeWidth = 1.;
	bool _fill = false;
	float64 _bend = 0.;
	bool _bendDragging = false;
	bool _belowMinimum = false;

	struct SavedShape {
		QColor color;
		float64 strokeWidth = 0.;
		float64 bend = 0.;
		float64 aspectRatio = 1.;
		bool fill = false;
	};
	SavedShape _savedShape, _keepedShape;

};

} // namespace Editor
