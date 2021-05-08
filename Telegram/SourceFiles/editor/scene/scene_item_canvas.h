/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QGraphicsItem>

class QGraphicsSceneMouseEvent;

namespace Editor {

class ItemCanvas : public QGraphicsItem {
public:
	struct Content {
		QPixmap pixmap;
		QPointF position;
	};

	ItemCanvas();
	~ItemCanvas();

	void applyBrush(const QColor &color, float size);
	void clearPixmap();
	void cancelDrawing();

	QRectF boundingRect() const override;
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;

	void handleMousePressEvent(not_null<QGraphicsSceneMouseEvent*> event);
	void handleMouseReleaseEvent(not_null<QGraphicsSceneMouseEvent*> event);
	void handleMouseMoveEvent(not_null<QGraphicsSceneMouseEvent*> event);

	[[nodiscard]] rpl::producer<Content> grabContentRequests() const;

protected:
	bool collidesWithItem(
		const QGraphicsItem *,
		Qt::ItemSelectionMode) const override;

	bool collidesWithPath(
		const QPainterPath &,
		Qt::ItemSelectionMode) const override;
private:
	void computeContentRect(const QPointF &p);
	void drawLine(const QPointF &currentPoint, const QPointF &lastPoint);

	bool _drawing = false;

	std::unique_ptr<PainterHighQualityEnabler> _hq;
	std::unique_ptr<Painter> _p;

	QRectF _rectToUpdate;
	QRectF _contentRect;
	QMarginsF _brushMargins;

	QPointF _lastPoint;

	QPixmap _pixmap;

	struct {
		float size = 1.;
		QColor color;
	} _brushData;

	rpl::event_stream<Content> _grabContentRequests;

};

} // namespace Editor
