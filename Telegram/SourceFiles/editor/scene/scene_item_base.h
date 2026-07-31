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

	void setUndoable(bool undoable);
	[[nodiscard]] bool undoable() const;

	virtual void save(SaveState state);
	virtual void restore(SaveState state);
	virtual bool hasState(SaveState state) const;
private:
	int _number = 0;
	Status _status = Status::Normal;
	bool _undoable = true;
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
		bool contentMargins = true;
	};

	struct Placement {
		QPointF position;
		float64 rotation = 0.;
		float64 scale = 1.;
		float64 zValue = 0.;
		float64 size = 0.;
		float64 aspectRatio = 1.;
		float64 bend = 0.;
		bool flipped = false;

		friend inline bool operator==(
			const Placement &,
			const Placement &) = default;
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

	[[nodiscard]] virtual Placement placement() const;
	virtual void applyPlacement(const Placement &placement);

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
	void raiseToTop();

	QRectF contentRect() const;
	QRectF innerRect() const;
	float64 size() const;
	float64 horizontalSize() const;
	float64 verticalSize() const;
	void setAspectRatio(float64 aspectRatio);
	void applyStretch(
		float64 horizontal,
		float64 vertical,
		bool allowBelowMinimum = false);
	[[nodiscard]] bool fitsMinimumSize() const;
	void setVerticalMinimumEnabled(bool enabled);
	[[nodiscard]] bool isHandling() const;
	[[nodiscard]] float64 scaledHandleSize() const;
	void paintHandle(QPainter *p, const QRectF &rect, bool hasFocus) const;

	virtual void performFlip();
	virtual std::shared_ptr<ItemBase> duplicate(Data data) const = 0;
private:
	HandleType handleType(const QPointF &pos) const;
	QRectF rightHandleRect() const;
	QRectF leftHandleRect() const;
	[[nodiscard]] float64 verticalMinimum() const;
	void updateVerticalSize();
	void updatePens(QPen pen);
	void handleActionKey(not_null<QKeyEvent*> e);

	Data generateData() const;
	void applyData(const Data &data);

	const std::shared_ptr<float64> _lastZ;
	const QSize _imageSize;
	const bool _contentMargins;

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
	bool _verticalMinimumEnabled = true;

};

} // namespace Editor
