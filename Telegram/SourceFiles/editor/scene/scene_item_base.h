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

#include <QGraphicsItem>

class QGraphicsSceneHoverEvent;
class QGraphicsSceneMouseEvent;
class QStyleOptionGraphicsItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

class ItemAction;
class ItemAnimated;
class VideoClip;

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
	virtual void setStatus(Status status);
	[[nodiscard]] bool isNormalStatus() const;
	[[nodiscard]] bool isUndidStatus() const;
	[[nodiscard]] bool isRemovedStatus() const;

	void setUndoable(bool undoable);
	[[nodiscard]] bool undoable() const;

	[[nodiscard]] virtual ItemAction *asAction();
	[[nodiscard]] virtual ItemAnimated *asAnimated();
	[[nodiscard]] virtual VideoClip *videoClip();

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
		float64 maxSizeRatio = 1.;
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
		float64 fontSize = 0.;
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
	void keyReleaseEvent(QKeyEvent *e) override;
	bool sceneEvent(QEvent *event) override;
	virtual void fillContextMenu(not_null<Ui::PopupMenu*> menu);

	using Action = void(ItemBase::*)();
	void performForSelectedItems(Action action);
	[[nodiscard]] virtual bool flippable() const;
	virtual void actionFlip();
	void actionDelete();
	void actionDuplicate();
	void raiseToTop();

	QRectF contentRect() const;
	QRectF innerRect() const;
	[[nodiscard]] QRectF fittedRect(QSizeF size) const;
	[[nodiscard]] virtual QRectF visibleRect() const;
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
	void resetDragging();
	[[nodiscard]] bool dragThresholdPassed(
		not_null<QGraphicsSceneMouseEvent*> event);
	[[nodiscard]] float64 scaledHandleSize() const;
	void paintHandle(QPainter *p, const QRectF &rect, bool hasFocus) const;

	virtual void performFlip();
	virtual std::shared_ptr<ItemBase> duplicate(Data data) const = 0;
private:
	enum class StickyAnchor {
		Start,
		Center,
		End,
	};
	struct Sticky {
		int line = -1;
		StickyAnchor anchor = StickyAnchor::Start;

		[[nodiscard]] bool valid() const {
			return line >= 0;
		}
		friend inline bool operator==(
			const Sticky &,
			const Sticky &) = default;
	};
	struct StickyAxis {
		Sticky current;
		Sticky last;
		Ui::Animations::Simple animation;
	};
	struct StickyDrag {
		std::vector<std::pair<QGraphicsItem*, QPointF>> others;
		QPointF raw;
		bool active = false;
	};

	HandleType handleType(const QPointF &pos) const;
	QRectF rightHandleRect() const;
	QRectF leftHandleRect() const;
	[[nodiscard]] float64 verticalMinimum() const;
	void updateVerticalSize();
	void updatePens(QPen pen);
	void handleActionKey(not_null<QKeyEvent*> e);

	[[nodiscard]] StickyAxis &stickyAxis(Qt::Orientation orientation);
	[[nodiscard]] const StickyAxis &stickyAxis(
		Qt::Orientation orientation) const;
	void startStickyDrag();
	void updateSticky(bool enabled);
	void applyStickyState(bool enabled);
	void finishStickyDrag();
	void resetStickyAxis(Qt::Orientation orientation);
	void applySticky(Qt::Orientation orientation, Sticky sticky);
	void applyStickyPosition();
	void notifyStickyGuides();
	[[nodiscard]] QRectF stickyBounds() const;
	[[nodiscard]] Sticky computeSticky(Qt::Orientation orientation) const;
	[[nodiscard]] float64 stickyShift(
		Qt::Orientation orientation,
		Sticky sticky) const;
	[[nodiscard]] float64 stickyOffset(Qt::Orientation orientation) const;
	[[nodiscard]] std::optional<float64> stickyGuide(
		Qt::Orientation orientation) const;

	Data generateData() const;
	void applyData(const Data &data);

	const std::shared_ptr<float64> _lastZ;
	const QSize _imageSize;
	const float64 _maxSizeRatio;
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
	float64 _scaledStickyTrigger = 0.;
	QMarginsF _scaledInnerMargins;
	StickyAxis _stickyX;
	StickyAxis _stickyY;
	StickyDrag _stickyDrag;

	float64 _horizontalSize = 0;
	float64 _verticalSize = 0;
	float64 _aspectRatio = 1.0;
	HandleType _handle = HandleType::None;

	bool _flipped = false;
	bool _verticalMinimumEnabled = true;
	bool _dragging = false;

};

} // namespace Editor
