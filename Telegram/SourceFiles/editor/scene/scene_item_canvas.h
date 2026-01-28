/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/painter.h"

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
	void updateZoom(float64 zoom);

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
	struct StrokePoint {
		QPointF pos;
		float64 pressure = 1.0;
		int64 time = 0;
	};

	void computeContentRect(const QPointF &p);
	void addStrokePoint(const QPointF &point, int64 time);
	void drawIncrementalStroke();
	std::vector<StrokePoint> smoothStroke(
		const std::vector<StrokePoint> &points) const;
	void renderSegment(
		const std::vector<StrokePoint> &points,
		int startIdx);

	bool _drawing = false;
	std::vector<StrokePoint> _currentStroke;
	int _lastRenderedIndex = 0;
	float64 _zoom = 1.0;
	int64 _lastPointTime = 0;

	std::unique_ptr<PainterHighQualityEnabler> _hq;
	std::unique_ptr<Painter> _p;

	QRectF _rectToUpdate;
	QRectF _contentRect;
	QMarginsF _brushMargins;

	QPointF _lastPoint;

	QPixmap _pixmap;
	QPainterPath _currentPath;

	struct {
		float size = 1.;
		QColor color;
	} _brushData;

	rpl::event_stream<Content> _grabContentRequests;

};

} // namespace Editor
