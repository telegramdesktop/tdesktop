/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <base/unique_qptr.h>
#include <editor/photo_editor_inner_common.h>
#include <editor/scene/scene_item_base.h>

#include <QGraphicsScene>

class QGraphicsSceneMouseEvent;
class QGraphicsTextItem;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

class ItemCanvas;
class ItemShape;
class ItemText;
class NumberedItem;

class Scene final : public QGraphicsScene {
public:
	using ItemPtr = std::shared_ptr<NumberedItem>;

	struct PendingShape {
		ShapeType shape = ShapeType::Circle;
		QColor color;
		float64 strokeWidth = 1.;
		int defaultSize = 0;
		bool fill = false;
		int rotation = 0;
		bool flipped = false;
	};

	Scene(const QRectF &rect);
	~Scene();
	void applyBrush(const QColor &color, float64 size, Brush::Tool tool);
	void setBlurSource(Fn<QImage(QRect)> source);
	void setTextDefaults(const QColor &color, float64 fontSize, int style);

	void setPendingShape(std::optional<PendingShape> pending);
	void updatePendingShapeBrush(const QColor &color, float64 strokeWidth);
	[[nodiscard]] bool hasPendingShape() const;
	[[nodiscard]] rpl::producer<bool> pendingShapeStates() const;

	[[nodiscard]] std::vector<ItemPtr> items(
		Qt::SortOrder order = Qt::DescendingOrder) const;
	void addItem(ItemPtr item);
	void removeItem(not_null<QGraphicsItem*> item);
	void removeItem(const ItemPtr &item);
	[[nodiscard]] rpl::producer<> addsItem() const;
	[[nodiscard]] rpl::producer<> removesItem() const;

	[[nodiscard]] std::shared_ptr<float64> lastZ() const;

	void updateZoom(float64 zoom);

	void cancelDrawing();
	void cancelTextEditing();

	void startTextEditing(ItemText *item);
	void createTextAtCenter(int rotation);
	void setTextColor(const QColor &color);
	void setSelectedTextColor(const QColor &color);
	void setSelectedShapeBrush(const QColor &color, float64 strokeWidth);

	[[nodiscard]] rpl::producer<QColor> textColorRequests() const;
	[[nodiscard]] rpl::producer<QColor> textItemSelections() const;
	[[nodiscard]] rpl::producer<> textItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> textEditStates() const;
	[[nodiscard]] rpl::producer<QColor> shapeItemSelections() const;
	[[nodiscard]] rpl::producer<> shapeItemDeselections() const;

	[[nodiscard]] bool hasUndo() const;
	[[nodiscard]] bool hasRedo() const;

	void performUndo();
	void performRedo();

	void save(SaveState state);
	void restore(SaveState state);

	void clearRedoList();
protected:
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
private:
	struct CapturedPlacement {
		std::shared_ptr<ItemBase> item;
		ItemBase::Placement placement;
	};

	void removeIf(Fn<bool(const ItemPtr &)> proj);
	void capturePlacements();
	void commitPlacements();
	void startShapeDrawing(const QPointF &position);
	void updateShapeDrawing(
		const QPointF &position,
		Qt::KeyboardModifiers modifiers);
	void applyDraftFrame(float64 width, float64 height);
	void finishShapeDrawing(bool apply);
	[[nodiscard]] std::shared_ptr<ItemShape> createShape(
		int size,
		const QPointF &center) const;
	void finishTextEditing(bool save, bool notify = true);
	void setTextEditing(bool editing, bool notify = true);
	void setupTextProxy(
		QGraphicsTextItem *proxy,
		const QColor &color,
		float64 fontSize);

	const std::shared_ptr<ItemCanvas> _canvas;
	const std::shared_ptr<float64> _lastZ;
	Fn<QImage(QRect)> _blurSource;

	std::vector<ItemPtr> _items;
	std::unordered_map<QGraphicsItem*, ItemPtr> _itemsByPointer;
	std::vector<CapturedPlacement> _capturedPlacements;

	float64 _lastLineZ = 0.;
	float64 _currentZoom = 1.;
	int _itemNumber = 0;

	QColor _textColor;
	float64 _textFontSize = 0.;
	int _textStyle = 0;
	int _textEditStyle = 0;

	struct {
		std::weak_ptr<NumberedItem> item;
		base::unique_qptr<QGraphicsTextItem> proxy;
	} _textEdit;

	struct {
		std::optional<PendingShape> pending;
		std::shared_ptr<ItemShape> item;
		QPointF start;
		bool dragging = false;
		bool moved = false;
		bool fits = false;
	} _shapeTool;

	rpl::event_stream<> _addsItem, _removesItem;
	rpl::event_stream<QColor> _textColorRequests;
	rpl::event_stream<QColor> _textItemSelections;
	rpl::event_stream<> _textItemDeselections;
	rpl::event_stream<bool> _textEditStates;
	rpl::event_stream<QColor> _shapeItemSelections;
	rpl::event_stream<> _shapeItemDeselections;
	rpl::event_stream<bool> _pendingShapeStates;
	ItemText *_selectedTextItem = nullptr;
	ItemShape *_selectedShapeItem = nullptr;
	bool _textEditing = false;
	int _textEditGeneration = 0;
	rpl::lifetime _lifetime;

};

} // namespace Editor
