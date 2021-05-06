/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

#include <QGraphicsItem>

class QGraphicsSceneHoverEvent;
class QGraphicsSceneMouseEvent;
class QStyleOptionGraphicsItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

class NumberedItem : public QGraphicsItem {
public:
	enum { Type = UserType + 1 };
	using QGraphicsItem::QGraphicsItem;

	int type() const override;
	void setNumber(int number);
	[[nodiscard]] int number() const;
private:
	int _number = 0;
};

class ItemBase : public NumberedItem {
public:

	ItemBase(
		rpl::producer<float64> zoomValue,
		std::shared_ptr<float64> zPtr,
		int size,
		int x,
		int y);
	QRectF boundingRect() const override;
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;

	bool flipped() const;
	void setFlip(bool value);
protected:
	enum HandleType {
		None,
		Left,
		Right,
	};
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
	void keyPressEvent(QKeyEvent *e) override;

	using Action = void(ItemBase::*)();
	void performForSelectedItems(Action action);
	void actionFlip();
	void actionDelete();
	void actionDuplicate();

	QRectF contentRect() const;
	QRectF innerRect() const;
	float64 size() const;
	float64 horizontalSize() const;
	float64 verticalSize() const;
	void setAspectRatio(float64 aspectRatio);

	virtual void performFlip();
	virtual std::shared_ptr<ItemBase> duplicate(
		rpl::producer<float64> zoomValue,
		std::shared_ptr<float64> zPtr,
		int size,
		int x,
		int y) const = 0;
private:
	HandleType handleType(const QPointF &pos) const;
	QRectF rightHandleRect() const;
	QRectF leftHandleRect() const;
	bool isHandling() const;
	void updateVerticalSize();
	void updatePens(QPen pen);
	void handleActionKey(not_null<QKeyEvent*> e);

	const std::shared_ptr<float64> _lastZ;

	struct {
		QPen select;
		QPen selectInactive;
		QPen handle;
		QPen handleInactive;
	} _pens;

	base::unique_qptr<Ui::PopupMenu> _menu;

	struct {
		int min = 0.;
		int max = 0.;
	} _sizeLimits;
	float64 _scaledHandleSize = 1.0;
	QMarginsF _scaledInnerMargins;

	float64 _horizontalSize = 0;
	float64 _verticalSize = 0;
	float64 _aspectRatio = 1.0;
	HandleType _handle = HandleType::None;

	bool _flipped = false;

	rpl::variable<float64> _zoom;
	rpl::lifetime _lifetime;

};

} // namespace Editor
