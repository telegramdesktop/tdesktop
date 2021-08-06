/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/photo_editor_inner_common.h"

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
	enum class Status {
		Normal,
		Undid,
		Removed,
	};

	enum { Type = UserType + 1 };
	using QGraphicsItem::QGraphicsItem;

	int type() const override;
	void setNumber(int number);
	[[nodiscard]] int number() const;

	[[nodiscard]] Status status() const;
	void setStatus(Status status);
	[[nodiscard]] bool isNormalStatus() const;
	[[nodiscard]] bool isUndidStatus() const;
	[[nodiscard]] bool isRemovedStatus() const;

	virtual void save(SaveState state);
	virtual void restore(SaveState state);
	virtual bool hasState(SaveState state) const;
private:
	int _number = 0;
	Status _status = Status::Normal;
};

class ItemBase : public NumberedItem {
public:
	enum { Type = UserType + 2 };

	struct Data {
		float64 initialZoom = 0.;
		std::shared_ptr<float64> zPtr;
		int size = 0;
		int x = 0;
		int y = 0;
		bool flipped = false;
		int rotation = 0;
		QSize imageSize;
	};

	ItemBase(Data data);
	QRectF boundingRect() const override;
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;

	bool flipped() const;
	void setFlip(bool value);

	void updateZoom(float64 zoom);

	bool hasState(SaveState state) const override;
	void save(SaveState state) override;
	void restore(SaveState state) override;
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
	virtual std::shared_ptr<ItemBase> duplicate(Data data) const = 0;
private:
	HandleType handleType(const QPointF &pos) const;
	QRectF rightHandleRect() const;
	QRectF leftHandleRect() const;
	bool isHandling() const;
	void updateVerticalSize();
	void updatePens(QPen pen);
	void handleActionKey(not_null<QKeyEvent*> e);

	Data generateData() const;
	void applyData(const Data &data);

	const std::shared_ptr<float64> _lastZ;
	const QSize _imageSize;

	struct {
		QPen select;
		QPen selectInactive;
		QPen handle;
		QPen handleInactive;
	} _pens;

	base::unique_qptr<Ui::PopupMenu> _menu;

	struct {
		Data data;
		float64 zValue = 0.;
		NumberedItem::Status status;
	} _saved, _keeped;

	struct {
		int min = 0;
		int max = 0;
	} _sizeLimits;
	float64 _scaledHandleSize = 1.0;
	QMarginsF _scaledInnerMargins;

	float64 _horizontalSize = 0;
	float64 _verticalSize = 0;
	float64 _aspectRatio = 1.0;
	HandleType _handle = HandleType::None;

	bool _flipped = false;

};

} // namespace Editor
