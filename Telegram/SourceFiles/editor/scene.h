/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <base/unique_qptr.h>

#include <QGraphicsScene>

class QGraphicsSceneMouseEvent;
class QGraphicsSceneMouseEvent;
class QGraphicsSceneMouseEvent;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

class ItemCanvas;
class NumberedItem;

class Scene final : public QGraphicsScene {
public:
	Scene(const QRectF &rect);
	~Scene();
	void applyBrush(const QColor &color, float size);

	[[nodiscard]] std::vector<QGraphicsItem*> items(
		Qt::SortOrder order = Qt::DescendingOrder) const;
	void addItem(not_null<NumberedItem*> item);
	[[nodiscard]] rpl::producer<> mousePresses() const;

protected:
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
private:
	void clearPath();
	void addLineItem();

	const not_null<ItemCanvas*> _canvas;

	QPainterPath _path;
	bool _drawing = false;

	float64 _lastLineZ = 0.;

	int _itemNumber = 0;

	struct {
		float size = 1.;
		QColor color;
	} _brushData;

	rpl::event_stream<> _mousePresses;
	rpl::lifetime _lifetime;

};

} // namespace Editor
