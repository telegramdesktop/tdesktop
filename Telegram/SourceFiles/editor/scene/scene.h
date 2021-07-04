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

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

class ItemCanvas;
class NumberedItem;

class Scene final : public QGraphicsScene {
public:
	using ItemPtr = std::shared_ptr<QGraphicsItem>;

	Scene(const QRectF &rect);
	~Scene();
	void applyBrush(const QColor &color, float size);

	[[nodiscard]] std::vector<ItemPtr> items(
		Qt::SortOrder order = Qt::DescendingOrder) const;
	void addItem(std::shared_ptr<NumberedItem> item);
	void removeItem(not_null<QGraphicsItem*> item);
	void removeItem(const ItemPtr &item);
	[[nodiscard]] rpl::producer<> addsItem() const;
	[[nodiscard]] rpl::producer<> removesItem() const;

	[[nodiscard]] std::vector<MTPInputDocument> attachedStickers() const;

	void cancelDrawing();
protected:
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
private:
	const std::shared_ptr<ItemCanvas> _canvas;

	std::vector<ItemPtr> _items;

	float64 _lastLineZ = 0.;
	int _itemNumber = 0;

	rpl::event_stream<> _addsItem, _removesItem;
	rpl::lifetime _lifetime;

};

} // namespace Editor
