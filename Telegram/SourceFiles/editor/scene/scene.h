/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <editor/photo_editor_inner_common.h>
#include <editor/scene/scene_item_base.h>
#include "ui/effects/animations.h"

#include <QGraphicsScene>

class QGraphicsSceneMouseEvent;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Editor {

class ItemCanvas;
class ItemShape;
class ItemText;
class NumberedItem;
class TextEditController;
class VideoClip;
struct AudioTrack;

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
	void setCanvasRect(const QRectF &rect);
	[[nodiscard]] QRectF canvasRect() const;
	void setStickyGuides(std::optional<float64> x, std::optional<float64> y);
	void applyBrush(const QColor &color, float64 size, Brush::Tool tool);
	void setBlurSource(Fn<QImage(QRect)> source);
	void setTextDefaults(
		const QColor &color,
		float64 fontSize,
		TextStyle style,
		TextTypeface typeface,
		TextAlignment alignment);
	void applyTextPrefs(const TextPrefs &prefs);
	void noteTextItemPrefs(not_null<ItemText*> item);

	void setPendingShape(std::optional<PendingShape> pending);
	void updatePendingShapeBrush(const QColor &color, float64 strokeWidth);
	[[nodiscard]] bool hasPendingShape() const;
	[[nodiscard]] rpl::producer<bool> pendingShapeStates() const;

	[[nodiscard]] std::vector<ItemPtr> items(
		Qt::SortOrder order = Qt::DescendingOrder) const;
	[[nodiscard]] bool hasAnimatedItems() const;
	[[nodiscard]] bool hasAnimatedResult() const;
	[[nodiscard]] bool hasSoundResult() const;
	void releaseAnimations();

	void setAudio(std::shared_ptr<AudioTrack> audio);
	[[nodiscard]] std::shared_ptr<AudioTrack> audio() const;
	[[nodiscard]] rpl::producer<> audioChanges() const;
	void setAudioSelected(bool selected);
	[[nodiscard]] bool audioSelected() const;
	[[nodiscard]] rpl::producer<bool> audioSelectedChanges() const;
	[[nodiscard]] bool canEqualizeDurations() const;
	void equalizeDurations();
	void matchDurations(crl::time duration);
	[[nodiscard]] bool durationsLinked() const;
	void setDurationsLinked(bool linked);
	[[nodiscard]] rpl::producer<> durationsLinkChanges() const;
	void addItem(ItemPtr item);
	void removeItem(not_null<QGraphicsItem*> item);
	void removeItem(const ItemPtr &item);
	void videoClipChanged(not_null<NumberedItem*> item);
	[[nodiscard]] rpl::producer<> addsItem() const;
	[[nodiscard]] rpl::producer<> removesItem() const;

	[[nodiscard]] std::shared_ptr<float64> lastZ() const;
	[[nodiscard]] ItemPtr itemShared(QGraphicsItem *item) const;
	[[nodiscard]] float64 currentZoom() const;

	void updateZoom(float64 zoom);

	void cancelDrawing();
	void cancelTextEditing();

	void startTextEditing(ItemText *item);
	void createTextAtCenter(int rotation, bool flipped);
	void setTextColor(const QColor &color);
	void setSelectedTextColor(const QColor &color);
	void setSelectedShapeBrush(const QColor &color, float64 strokeWidth);

	[[nodiscard]] rpl::producer<QColor> textColorRequests() const;
	[[nodiscard]] rpl::producer<TextPrefs> textPrefsUsed() const;
	[[nodiscard]] rpl::producer<QColor> textItemSelections() const;
	[[nodiscard]] rpl::producer<> textItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> textEditStates() const;
	[[nodiscard]] rpl::producer<QColor> shapeItemSelections() const;
	[[nodiscard]] rpl::producer<> shapeItemDeselections() const;
	[[nodiscard]] auto videoClipSelections() const
		-> rpl::producer<std::shared_ptr<VideoClip>>;

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
	class StickyGuidesItem;
	struct CapturedPlacement {
		std::shared_ptr<ItemBase> item;
		ItemBase::Placement placement;
	};
	struct StickyGuide {
		Ui::Animations::Simple animation;
		float64 position = 0.;
		bool shown = false;
	};

	void removeIf(Fn<bool(const ItemPtr &)> proj);
	void setStickyGuide(
		Qt::Orientation orientation,
		std::optional<float64> position);
	void hideStickyGuides();
	[[nodiscard]] float64 stickyGuideMargin() const;
	[[nodiscard]] QRectF stickyGuideRect(Qt::Orientation orientation) const;
	void paintStickyGuide(QPainter &p, Qt::Orientation orientation) const;
	void capturePlacements();
	void refreshVideoClipSelection();
	void updateVideoClipsSound();
	void checkDurationsLink();
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

	const std::shared_ptr<ItemCanvas> _canvas;
	const std::shared_ptr<float64> _lastZ;
	const std::unique_ptr<StickyGuidesItem> _stickyGuides;
	const std::unique_ptr<TextEditController> _textEdit;
	Fn<QImage(QRect)> _blurSource;

	std::vector<ItemPtr> _items;
	std::unordered_map<QGraphicsItem*, ItemPtr> _itemsByPointer;
	std::vector<CapturedPlacement> _capturedPlacements;
	QRectF _canvasRect;
	StickyGuide _stickyGuideX;
	StickyGuide _stickyGuideY;

	float64 _lastLineZ = 0.;
	float64 _currentZoom = 1.;
	int _itemNumber = 0;

	struct {
		std::optional<PendingShape> pending;
		std::shared_ptr<ItemShape> item;
		QPointF start;
		bool dragging = false;
		bool moved = false;
		bool fits = false;
	} _shapeTool;

	rpl::event_stream<> _addsItem, _removesItem;
	rpl::event_stream<QColor> _textItemSelections;
	rpl::event_stream<> _textItemDeselections;
	rpl::event_stream<QColor> _shapeItemSelections;
	rpl::event_stream<> _shapeItemDeselections;
	rpl::event_stream<bool> _pendingShapeStates;
	rpl::event_stream<std::shared_ptr<VideoClip>> _videoClipSelections;
	rpl::event_stream<> _audioChanges;
	rpl::event_stream<bool> _audioSelectedChanges;
	rpl::event_stream<> _durationsLinkChanges;
	ItemText *_selectedTextItem = nullptr;
	ItemShape *_selectedShapeItem = nullptr;
	VideoClip *_selectedVideoClip = nullptr;
	std::shared_ptr<AudioTrack> _audio;
	std::shared_ptr<AudioTrack> _keptAudio;
	std::shared_ptr<AudioTrack> _savedAudio;
	bool _audioSelected = false;
	bool _durationsLinked = false;
	bool _keptDurationsLinked = false;
	bool _savedDurationsLinked = false;
	rpl::lifetime _lifetime;

};

} // namespace Editor
