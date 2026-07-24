/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <base/unique_qptr.h>
#include <editor/photo_editor_inner_common.h>

#include <QGraphicsScene>

class QGraphicsSceneMouseEvent;
class QGraphicsTextItem;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

class ItemCanvas;
class ItemText;
class NumberedItem;

class Scene final : public QGraphicsScene {
public:
	using ItemPtr = std::shared_ptr<NumberedItem>;

	Scene(const QRectF &rect);
	~Scene();
	void applyBrush(const QColor &color, float64 size, Brush::Tool tool);
	void setBlurSource(Fn<QImage(QRect)> source);
	void setTextDefaults(const QColor &color, float64 fontSize, int style);

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

	[[nodiscard]] rpl::producer<QColor> textColorRequests() const;
	[[nodiscard]] rpl::producer<QColor> textItemSelections() const;
	[[nodiscard]] rpl::producer<> textItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> textEditStates() const;

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
	void removeIf(Fn<bool(const ItemPtr &)> proj);
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

	rpl::event_stream<> _addsItem, _removesItem;
	rpl::event_stream<QColor> _textColorRequests;
	rpl::event_stream<QColor> _textItemSelections;
	rpl::event_stream<> _textItemDeselections;
	rpl::event_stream<bool> _textEditStates;
	ItemText *_selectedTextItem = nullptr;
	bool _textEditing = false;
	int _textEditGeneration = 0;
	rpl::lifetime _lifetime;

};

} // namespace Editor
