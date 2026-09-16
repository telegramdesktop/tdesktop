/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene.h"

#include "editor/photo_editor_common.h"
#include "editor/scene/scene_item_animated.h"
#include "editor/scene/scene_item_canvas.h"
#include "editor/scene/scene_item_line.h"
#include "editor/scene/scene_item_shape.h"
#include "editor/scene/scene_item_text.h"
#include "editor/scene/scene_text_editing.h"
#include "editor/video/video_clip.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "styles/style_editor.h"

#include <QGraphicsSceneMouseEvent>
#include <QtMath>

namespace Editor {

class ItemAction : public NumberedItem {
public:
	using NumberedItem::NumberedItem;

	virtual void apply() = 0;
	virtual void revert() = 0;

	ItemAction *asAction() override {
		return this;
	}

	QRectF boundingRect() const override {
		return QRectF();
	}

	void paint(
			QPainter *,
			const QStyleOptionGraphicsItem *,
			QWidget *) override {
	}

	bool hasState(SaveState state) const override {
		const auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
		return saved.saved;
	}

	void save(SaveState state) override {
		auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
		saved = {
			.saved = true,
			.status = status(),
		};
	}

	void restore(SaveState state) override {
		if (!hasState(state)) {
			return;
		}
		const auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
		setStatus(saved.status);
	}

private:
	struct {
		bool saved = false;
		NumberedItem::Status status = Status::Normal;
	} _saved, _keeped;
};

namespace {

using ItemPtr = std::shared_ptr<NumberedItem>;

class ItemEraser final : public ItemAction {
public:
	struct Target {
		std::shared_ptr<ItemLine> item;
		QPixmap before;
	};

	ItemEraser(
		QPixmap mask,
		QPointF maskPos,
		std::vector<Target> targets)
	: _mask(std::move(mask))
	, _maskPos(maskPos)
	, _targets(std::move(targets)) {
	}

	void apply() override {
		for (const auto &target : _targets) {
			target.item->applyEraser(_mask, _maskPos);
		}
	}

	void revert() override {
		for (const auto &target : _targets) {
			target.item->setPixmap(target.before);
		}
	}

	void restore(SaveState state) override {
		if (!hasState(state)) {
			return;
		}
		ItemAction::restore(state);
		if (isNormalStatus()) {
			apply();
		} else if (isUndidStatus()) {
			revert();
		}
	}

private:
	QPixmap _mask;
	QPointF _maskPos;
	std::vector<Target> _targets;
};

class ItemPlacement final : public ItemAction {
public:
	struct Target {
		std::shared_ptr<ItemBase> item;
		ItemBase::Placement before;
		ItemBase::Placement after;
	};

	explicit ItemPlacement(std::vector<Target> targets)
	: _targets(std::move(targets)) {
	}

	void apply() override {
		for (const auto &target : _targets) {
			target.item->applyPlacement(target.after);
		}
	}

	void revert() override {
		for (const auto &target : _targets) {
			target.item->applyPlacement(target.before);
		}
	}

private:
	std::vector<Target> _targets;
};

bool SkipMouseEvent(not_null<QGraphicsSceneMouseEvent*> event) {
	return event->isAccepted() || (event->button() == Qt::RightButton);
}

constexpr auto kShapeDragThreshold = 4.;
constexpr auto kShapeSnapAngle = 45.;
constexpr auto kDraftShapeOpacity = 0.5;
constexpr auto kStickyGuideDuration = crl::time(150);
constexpr auto kItemsBaseZ = 9000.;

} // namespace

class Scene::StickyGuidesItem final : public QGraphicsItem {
public:
	explicit StickyGuidesItem(not_null<Scene*> scene)
	: _scene(scene) {
		setAcceptedMouseButtons(Qt::NoButton);
		setZValue(kItemsBaseZ - 1.);
	}

	QRectF boundingRect() const override {
		return _scene->canvasRect() + Margins(_scene->stickyGuideMargin());
	}

	void paint(
			QPainter *p,
			const QStyleOptionGraphicsItem *,
			QWidget *) override {
		_scene->paintStickyGuide(*p, Qt::Horizontal);
		_scene->paintStickyGuide(*p, Qt::Vertical);
	}

	void updateGeometry() {
		prepareGeometryChange();
	}

private:
	const not_null<Scene*> _scene;

};

Scene::Scene(const QRectF &rect)
: QGraphicsScene(rect)
, _canvas(std::make_shared<ItemCanvas>())
, _lastZ(std::make_shared<float64>(kItemsBaseZ))
, _stickyGuides(std::make_unique<StickyGuidesItem>(this))
, _textEdit(std::make_unique<TextEditController>(this)) {
	QGraphicsScene::addItem(_canvas.get());
	QGraphicsScene::addItem(_stickyGuides.get());

	_canvas->grabContentRequests(
	) | rpl::on_next([=](ItemCanvas::Content &&content) {
		if (content.clear) {
			auto mask = std::move(content.pixmap);
			if (mask.isNull()) {
				return;
			}
			const auto maskPos = content.position;
			const auto maskSize = mask.size()
				/ float64(mask.devicePixelRatio());
			const auto maskRect = QRectF(maskPos, maskSize);
			auto targets = std::vector<ItemEraser::Target>();
			const auto hits = QGraphicsScene::items(
				maskRect,
				Qt::IntersectsItemBoundingRect,
				Qt::DescendingOrder);
			for (auto *raw : hits) {
				const auto it = _itemsByPointer.find(raw);
				if (it == end(_itemsByPointer)) {
					continue;
				}
				const auto &item = it->second;
				if (!item->isNormalStatus()) {
					continue;
				}
				const auto line = std::dynamic_pointer_cast<ItemLine>(item);
				if (!line) {
					continue;
				}
				auto before = line->pixmap();
				if (!line->applyEraser(mask, maskPos)) {
					continue;
				}
				targets.push_back({
					.item = line,
					.before = std::move(before),
				});
			}
			if (!targets.empty()) {
				const auto eraser = std::make_shared<ItemEraser>(
					std::move(mask),
					maskPos,
					std::move(targets));
				addItem(eraser);
				_canvas->setZValue(++_lastLineZ);
			}
			return;
		}
		if (content.blur) {
			auto mask = std::move(content.pixmap);
			if (mask.isNull() || !_blurSource) {
				return;
			}
			const auto maskPos = content.position;
			const auto maskSize = mask.size()
				/ float64(mask.devicePixelRatio());
			const auto sourceRect = QRectF(maskPos, maskSize);
			const auto expandedRect = sourceRect.toAlignedRect().adjusted(
				-st::photoEditorBlurRadius,
				-st::photoEditorBlurRadius,
				st::photoEditorBlurRadius,
				st::photoEditorBlurRadius);
			const auto captureRect = expandedRect.intersected(
				sceneRect().toAlignedRect());
			if (captureRect.isEmpty()) {
				return;
			}
			auto source = _blurSource(captureRect);
			if (source.isNull()) {
				return;
			}
			const auto sourceDpr = source.devicePixelRatio();
			if (source.format() != QImage::Format_ARGB32_Premultiplied) {
				source = source.convertToFormat(
					QImage::Format_ARGB32_Premultiplied);
				source.setDevicePixelRatio(sourceDpr);
			}
			const auto canvasVisible = _canvas->isVisible();
			_canvas->setVisible(false);
			_stickyGuides->setVisible(false);
			{
				auto p = QPainter(&source);
				render(
					&p,
					QRectF(QPointF(), QSizeF(captureRect.size())),
					QRectF(captureRect),
					Qt::IgnoreAspectRatio);
			}
			_stickyGuides->setVisible(true);
			_canvas->setVisible(canvasVisible);
			auto blurred = Images::BlurLargeImage(
				std::move(source),
				st::photoEditorBlurRadius);
			if (blurred.isNull()) {
				return;
			}
			blurred.setDevicePixelRatio(sourceDpr);
			auto result = QImage(
				mask.size(),
				QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(mask.devicePixelRatio());
			result.fill(Qt::transparent);
			{
				auto p = QPainter(&result);
				p.drawImage(
					QRectF(QPointF(), maskSize),
					blurred,
					QRectF(
						sourceRect.x() - captureRect.x(),
						sourceRect.y() - captureRect.y(),
						sourceRect.width(),
						sourceRect.height()));
				p.setCompositionMode(
					QPainter::CompositionMode_DestinationIn);
				p.drawPixmap(0, 0, mask);
			}
			auto blurPixmap = QPixmap::fromImage(std::move(result));
			const auto item = std::make_shared<ItemLine>(
				std::move(blurPixmap));
			item->setPos(maskPos);
			addItem(item);
			_canvas->setZValue(++_lastLineZ);
			return;
		}
		const auto item = std::make_shared<ItemLine>(
			std::move(content.pixmap));
		item->setPos(content.position);
		addItem(item);
		_canvas->setZValue(++_lastLineZ);
	}, _lifetime);

	QObject::connect(
		this,
		&QGraphicsScene::selectionChanged,
		[=] {
			const auto selected = selectedItems();
			if (!selected.empty()) {
				setAudioSelected(false);
			}
			auto *textItem = (ItemText*)(nullptr);
			auto *shapeItem = (ItemShape*)(nullptr);
			if (selected.size() == 1) {
				if (selected.front()->type() == ItemText::Type) {
					textItem = static_cast<ItemText*>(selected.front());
				} else if (selected.front()->type() == ItemShape::Type) {
					shapeItem = static_cast<ItemShape*>(selected.front());
				}
			}
			if (textItem != _selectedTextItem) {
				_selectedTextItem = textItem;
				if (textItem) {
					_textItemSelections.fire_copy(textItem->color());
				} else {
					_textItemDeselections.fire({});
				}
			}
			if (shapeItem != _selectedShapeItem) {
				_selectedShapeItem = shapeItem;
				if (shapeItem) {
					_shapeItemSelections.fire_copy(shapeItem->color());
				} else {
					_shapeItemDeselections.fire({});
				}
			}
			refreshVideoClipSelection();
		});
}

void Scene::refreshVideoClipSelection() {
	const auto selected = selectedItems();
	auto clip = std::shared_ptr<VideoClip>();
	if (selected.size() == 1) {
		if (const auto item = itemShared(selected.front())) {
			if (const auto raw = item->videoClip()) {
				clip = std::shared_ptr<VideoClip>(item, raw);
			}
		}
	}
	if (clip.get() == _selectedVideoClip) {
		return;
	}
	_selectedVideoClip = clip.get();
	updateVideoClipsSound();
	_videoClipSelections.fire(std::move(clip));
}

void Scene::cancelDrawing() {
	_textEdit->finishEditing(false);
	_canvas->cancelDrawing();
}

void Scene::cancelTextEditing() {
	_textEdit->finishEditing(false, false);
}

void Scene::addItem(ItemPtr item) {
	if (!item) {
		return;
	}
	item->setNumber(_itemNumber++);
	const auto raw = item.get();
	_items.push_back(std::move(item));
	_itemsByPointer.emplace(raw, _items.back());
	if (raw->scene() != this) {
		QGraphicsScene::addItem(raw);
	}
	if (raw->videoClip()) {
		checkDurationsLink();
	}
	_addsItem.fire({});
}

void Scene::removeItem(not_null<QGraphicsItem*> item) {
	const auto it = ranges::find_if(_items, [&](const ItemPtr &i) {
		return i.get() == item;
	});
	if (it == end(_items)) {
		return;
	}
	removeItem(*it);
}

void Scene::removeItem(const ItemPtr &item) {
	item->setStatus(NumberedItem::Status::Removed);
	if (item->videoClip()) {
		checkDurationsLink();
	}
	_removesItem.fire({});
}

void Scene::videoClipChanged(not_null<NumberedItem*> item) {
	checkDurationsLink();
	if (item->isSelected()) {
		refreshVideoClipSelection();
	}
}

void Scene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	setAudioSelected(false);
	if (_shapeTool.pending) {
		if (event->button() == Qt::LeftButton) {
			event->accept();
			startShapeDrawing(event->scenePos());
			return;
		} else if (event->button() == Qt::RightButton) {
			event->accept();
			if (_shapeTool.dragging) {
				finishShapeDrawing(false);
			} else {
				setPendingShape(std::nullopt);
			}
			return;
		}
	}
	if (_textEdit->editing()
		&& !_textEdit->proxyContains(event->scenePos())) {
		_textEdit->finishEditing(true);
		QGraphicsScene::mousePressEvent(event);
		capturePlacements();
		return;
	}

	QGraphicsScene::mousePressEvent(event);
	capturePlacements();
	if (SkipMouseEvent(event)
		|| !_canvas->drawableRect().contains(event->scenePos())) {
		return;
	}
	_canvas->handleMousePressEvent(event);
}

void Scene::setCanvasRect(const QRectF &rect) {
	if (_canvasRect == rect) {
		return;
	}
	_stickyGuides->updateGeometry();
	_canvasRect = rect;
	_canvas->setCanvasRect(canvasRect());
}

QRectF Scene::canvasRect() const {
	return _canvasRect.isNull() ? sceneRect() : _canvasRect;
}

void Scene::setStickyGuides(
		std::optional<float64> x,
		std::optional<float64> y) {
	setStickyGuide(Qt::Horizontal, x);
	setStickyGuide(Qt::Vertical, y);
}

void Scene::setStickyGuide(
		Qt::Orientation orientation,
		std::optional<float64> position) {
	auto &guide = (orientation == Qt::Horizontal)
		? _stickyGuideX
		: _stickyGuideY;
	const auto shown = position.has_value();
	if (!shown && !guide.shown) {
		return;
	}
	const auto was = stickyGuideRect(orientation);
	if (shown) {
		guide.position = *position;
	}
	if (guide.shown != shown) {
		guide.shown = shown;
		guide.animation.start(
			[=] { _stickyGuides->update(stickyGuideRect(orientation)); },
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			kStickyGuideDuration);
	}
	_stickyGuides->update(was);
	_stickyGuides->update(stickyGuideRect(orientation));
}

void Scene::hideStickyGuides() {
	for (const auto guide : { &_stickyGuideX, &_stickyGuideY }) {
		guide->animation.stop();
		guide->shown = false;
	}
	_stickyGuides->update();
}

float64 Scene::stickyGuideMargin() const {
	const auto zoom = (_currentZoom > 0.) ? _currentZoom : 1.;
	return st::photoEditorStickyLineWidth / zoom;
}

QRectF Scene::stickyGuideRect(Qt::Orientation orientation) const {
	const auto &guide = (orientation == Qt::Horizontal)
		? _stickyGuideX
		: _stickyGuideY;
	const auto canvas = canvasRect();
	const auto margin = stickyGuideMargin();
	return (orientation == Qt::Horizontal)
		? QRectF(
			guide.position - margin,
			canvas.top(),
			margin * 2,
			canvas.height())
		: QRectF(
			canvas.left(),
			guide.position - margin,
			canvas.width(),
			margin * 2);
}

void Scene::paintStickyGuide(
		QPainter &p,
		Qt::Orientation orientation) const {
	const auto &guide = (orientation == Qt::Horizontal)
		? _stickyGuideX
		: _stickyGuideY;
	const auto opacity = guide.animation.value(guide.shown ? 1. : 0.);
	if (opacity <= 0.) {
		return;
	}
	const auto canvas = canvasRect();
	auto color = QColor(Qt::white);
	color.setAlphaF(opacity);
	p.setPen(QPen(color, stickyGuideMargin()));
	if (orientation == Qt::Horizontal) {
		p.drawLine(
			QPointF(guide.position, canvas.top()),
			QPointF(guide.position, canvas.bottom()));
	} else {
		p.drawLine(
			QPointF(canvas.left(), guide.position),
			QPointF(canvas.right(), guide.position));
	}
}

void Scene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	if (_shapeTool.dragging && (event->button() == Qt::LeftButton)) {
		event->accept();
		finishShapeDrawing(true);
		return;
	}
	QGraphicsScene::mouseReleaseEvent(event);
	commitPlacements();
	if (SkipMouseEvent(event) || _textEdit->editing()) {
		return;
	}
	_canvas->handleMouseReleaseEvent(event);
}

void Scene::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
	if (_shapeTool.dragging) {
		event->accept();
		updateShapeDrawing(event->scenePos(), event->modifiers());
		return;
	}
	QGraphicsScene::mouseMoveEvent(event);
	if (SkipMouseEvent(event) || _textEdit->editing()) {
		return;
	}
	_canvas->handleMouseMoveEvent(event);
}

void Scene::setPendingShape(std::optional<PendingShape> pending) {
	if (!pending && !_shapeTool.pending) {
		return;
	}
	if (_shapeTool.dragging) {
		finishShapeDrawing(false);
	}
	const auto was = _shapeTool.pending.has_value();
	_shapeTool.pending = std::move(pending);
	const auto now = _shapeTool.pending.has_value();
	if (now) {
		_textEdit->finishEditing(true);
		clearSelection();
		clearFocus();
		setAudioSelected(false);
	}
	if (was != now) {
		_pendingShapeStates.fire_copy(now);
	}
}

void Scene::updatePendingShapeBrush(
		const QColor &color,
		float64 strokeWidth) {
	if (!_shapeTool.pending) {
		return;
	}
	_shapeTool.pending->color = color;
	_shapeTool.pending->strokeWidth = strokeWidth;
	if (const auto item = _shapeTool.item.get()) {
		item->setColor(color);
		item->setStrokeWidth(strokeWidth);
	}
}

bool Scene::hasPendingShape() const {
	return _shapeTool.pending.has_value();
}

rpl::producer<bool> Scene::pendingShapeStates() const {
	return _pendingShapeStates.events();
}

std::shared_ptr<ItemShape> Scene::createShape(
		int size,
		const QPointF &center) const {
	const auto &pending = *_shapeTool.pending;
	auto data = ItemBase::Data{
		.initialZoom = (_currentZoom > 0.) ? _currentZoom : 1.,
		.zPtr = _lastZ,
		.size = size,
		.x = int(center.x()),
		.y = int(center.y()),
		.flipped = pending.flipped,
		.rotation = pending.rotation,
		.imageSize = sceneRect().size().toSize(),
	};
	return std::make_shared<ItemShape>(
		pending.shape,
		pending.color,
		pending.strokeWidth,
		pending.fill,
		std::move(data));
}

void Scene::startShapeDrawing(const QPointF &position) {
	if (_shapeTool.item) {
		finishShapeDrawing(false);
	}
	clearSelection();
	cancelDrawing();

	_shapeTool.start = position;
	_shapeTool.dragging = true;
	_shapeTool.moved = false;
	_shapeTool.fits = false;
	_shapeTool.item = createShape(0, position);
	_shapeTool.item->setVisible(false);
	QGraphicsScene::addItem(_shapeTool.item.get());
}

void Scene::updateShapeDrawing(
		const QPointF &position,
		Qt::KeyboardModifiers modifiers) {
	const auto item = _shapeTool.item.get();
	if (!item || !_shapeTool.pending) {
		return;
	}
	const auto delta = position - _shapeTool.start;
	const auto length = std::hypot(delta.x(), delta.y());
	if (!_shapeTool.moved) {
		const auto zoom = (_currentZoom > 0.) ? _currentZoom : 1.;
		if (length * zoom < kShapeDragThreshold) {
			return;
		}
		_shapeTool.moved = true;
		item->setVisible(true);
		item->setSelected(true);
	}
	const auto &pending = *_shapeTool.pending;
	const auto radians = pending.rotation * M_PI / 180.;
	const auto cosine = std::cos(radians);
	const auto sine = std::sin(radians);
	auto width = delta.x() * cosine + delta.y() * sine;
	auto height = delta.y() * cosine - delta.x() * sine;
	const auto shift = modifiers.testFlag(Qt::ShiftModifier);
	if (pending.shape == ShapeType::Arrow) {
		const auto mirror = pending.flipped ? -1. : 1.;
		const auto degrees = mirror
			* std::atan2(height, mirror * width)
			* 180.
			/ M_PI;
		item->setRotation(pending.rotation + (shift
			? (base::SafeRound(degrees / kShapeSnapAngle) * kShapeSnapAngle)
			: degrees));
		item->setPos(_shapeTool.start + delta / 2.);
		applyDraftFrame(length, 0.);
		return;
	}
	if (shift) {
		const auto aspectRatio = item->defaultAspectRatio();
		const auto side = std::max(
			std::abs(width),
			std::abs(height) / aspectRatio);
		width = (width < 0.) ? -side : side;
		height = ((height < 0.) ? -side : side) * aspectRatio;
	}
	item->setPos(_shapeTool.start
		+ QPointF(cosine, sine) * (width / 2.)
		+ QPointF(-sine, cosine) * (height / 2.));
	applyDraftFrame(std::abs(width), std::abs(height));
}

void Scene::applyDraftFrame(float64 width, float64 height) {
	const auto item = _shapeTool.item.get();
	_shapeTool.fits = item->applyDraftFrame(width, height);
	item->setOpacity(_shapeTool.fits ? 1. : kDraftShapeOpacity);
}

void Scene::finishShapeDrawing(bool apply) {
	auto item = base::take(_shapeTool.item);
	const auto moved = base::take(_shapeTool.moved);
	const auto fits = base::take(_shapeTool.fits);
	_shapeTool.dragging = false;
	if (!item) {
		return;
	}
	if (!apply || (moved && !fits)) {
		item->setSelected(false);
		QGraphicsScene::removeItem(item.get());
		return;
	}
	if (!moved) {
		const auto size = _shapeTool.pending
			? _shapeTool.pending->defaultSize
			: 0;
		item->applyFrame(size, size * item->defaultAspectRatio());
		item->setVisible(true);
		item->setSelected(true);
	}
	addItem(item);
	item->setFocus();
	setPendingShape(std::nullopt);
}

void Scene::applyBrush(const QColor &color, float64 size, Brush::Tool tool) {
	_canvas->applyBrush(color, size, tool);
}

void Scene::setTextDefaults(
		const QColor &color,
		float64 fontSize,
		TextStyle style,
		TextTypeface typeface,
		TextAlignment alignment) {
	_textEdit->setDefaults(color, fontSize, style, typeface, alignment);
}

void Scene::applyTextPrefs(const TextPrefs &prefs) {
	_textEdit->applyPrefs(prefs);
}

void Scene::noteTextItemPrefs(not_null<ItemText*> item) {
	_textEdit->noteItemPrefs(item);
}

void Scene::setTextColor(const QColor &color) {
	_textEdit->setColor(color);
}

void Scene::setSelectedTextColor(const QColor &color) {
	for (auto *item : selectedItems()) {
		if (item->type() == ItemText::Type) {
			static_cast<ItemText*>(item)->setColor(color);
		}
	}
}

void Scene::setSelectedShapeBrush(
		const QColor &color,
		float64 strokeWidth) {
	for (auto *item : selectedItems()) {
		if (item->type() == ItemShape::Type) {
			const auto shape = static_cast<ItemShape*>(item);
			shape->setColor(color);
			shape->setStrokeWidth(strokeWidth);
		}
	}
}

rpl::producer<QColor> Scene::textColorRequests() const {
	return _textEdit->colorRequests();
}

rpl::producer<TextPrefs> Scene::textPrefsUsed() const {
	return _textEdit->prefsUsed();
}

rpl::producer<QColor> Scene::textItemSelections() const {
	return _textItemSelections.events();
}

rpl::producer<> Scene::textItemDeselections() const {
	return _textItemDeselections.events();
}

rpl::producer<bool> Scene::textEditStates() const {
	return _textEdit->editStates();
}

rpl::producer<QColor> Scene::shapeItemSelections() const {
	return _shapeItemSelections.events();
}

rpl::producer<> Scene::shapeItemDeselections() const {
	return _shapeItemDeselections.events();
}

auto Scene::videoClipSelections() const
-> rpl::producer<std::shared_ptr<VideoClip>> {
	return _videoClipSelections.events();
}

void Scene::setBlurSource(Fn<QImage(QRect)> source) {
	_blurSource = std::move(source);
}

rpl::producer<> Scene::addsItem() const {
	return _addsItem.events();
}

rpl::producer<> Scene::removesItem() const {
	return _removesItem.events();
}

std::vector<ItemPtr> Scene::items(
		Qt::SortOrder order) const {
	auto copyItems = _items;

	ranges::sort(copyItems, [&](ItemPtr a, ItemPtr b) {
		const auto numA = a->number();
		const auto numB = b->number();
		return (order == Qt::AscendingOrder) ? (numA < numB) : (numA > numB);
	});

	return copyItems;
}

bool Scene::hasAnimatedItems() const {
	for (const auto &item : _items) {
		const auto animated = item->isNormalStatus()
			? item->asAnimated()
			: nullptr;
		if (animated && animated->animated() && animated->hasContent()) {
			return true;
		}
	}
	return false;
}

bool Scene::hasAnimatedResult() const {
	return (_audio != nullptr) || hasAnimatedItems();
}

void Scene::updateVideoClipsSound() {
	const auto apply = [&](bool enabled) {
		for (const auto &item : _items) {
			const auto clip = item->videoClip();
			if (clip && ((clip == _selectedVideoClip) == enabled)) {
				clip->setSoundEnabled(enabled);
			}
		}
	};
	apply(false);
	apply(true);
}

bool Scene::hasSoundResult() const {
	if (_audio && (_audio->volume > 0.)) {
		return true;
	}
	for (const auto &item : _items) {
		const auto clip = item->videoClip();
		if (clip
			&& item->isNormalStatus()
			&& clip->animated()
			&& clip->sounding()) {
			return true;
		}
	}
	return false;
}

void Scene::releaseAnimations() {
	for (const auto &item : _items) {
		if (const auto animated = item->asAnimated()) {
			animated->releasePlayers();
		}
	}
}

void Scene::setAudio(std::shared_ptr<AudioTrack> audio) {
	if (audio && audio->empty()) {
		audio = nullptr;
	}
	if (_audio == audio) {
		return;
	}
	_audio = std::move(audio);
	if (!_audio) {
		setAudioSelected(false);
	}
	_audioChanges.fire({});
	checkDurationsLink();
}

std::shared_ptr<AudioTrack> Scene::audio() const {
	return _audio;
}

rpl::producer<> Scene::audioChanges() const {
	return _audioChanges.events();
}

void Scene::setAudioSelected(bool selected) {
	if (selected && !_audio) {
		return;
	} else if (_audioSelected == selected) {
		return;
	}
	if (selected) {
		setPendingShape(std::nullopt);
		_textEdit->finishEditing(true);
		clearSelection();
		clearFocus();
	}
	_audioSelected = selected;
	_audioSelectedChanges.fire_copy(selected);
}

bool Scene::audioSelected() const {
	return _audioSelected;
}

rpl::producer<bool> Scene::audioSelectedChanges() const {
	return _audioSelectedChanges.events();
}

bool Scene::canEqualizeDurations() const {
	if (!_audio) {
		return false;
	}
	for (const auto &item : _items) {
		if (item->isNormalStatus() && item->videoClip()) {
			return true;
		}
	}
	return false;
}

void Scene::equalizeDurations() {
	auto shortest = _audio ? _audio->length() : crl::time(0);
	for (const auto &item : _items) {
		const auto clip = item->isNormalStatus()
			? item->videoClip()
			: nullptr;
		if (clip) {
			const auto loop = clip->loopDuration();
			if (loop > 0 && (!shortest || loop < shortest)) {
				shortest = loop;
			}
		}
	}
	matchDurations(shortest);
}

void Scene::matchDurations(crl::time duration) {
	auto clips = std::vector<VideoClip*>();
	auto shortest = duration;
	if (_audio) {
		shortest = std::min(shortest, _audio->duration - _audio->from);
	}
	for (const auto &item : _items) {
		const auto clip = item->isNormalStatus()
			? item->videoClip()
			: nullptr;
		const auto full = clip ? clip->duration() : crl::time(0);
		if (full > 0) {
			shortest = std::min(shortest, full - clip->trim().from);
			clips.push_back(clip);
		}
	}
	if (shortest <= 0) {
		return;
	}
	for (const auto clip : clips) {
		const auto from = clip->trim().from;
		clip->setTrim({ from, from + shortest });
	}
	if (_audio) {
		_audio->till = _audio->from + shortest;
	}
}

bool Scene::durationsLinked() const {
	return _durationsLinked;
}

void Scene::setDurationsLinked(bool linked) {
	if (_durationsLinked == linked) {
		return;
	}
	_durationsLinked = linked;
	if (linked) {
		equalizeDurations();
	}
}

rpl::producer<> Scene::durationsLinkChanges() const {
	return _durationsLinkChanges.events();
}

void Scene::checkDurationsLink() {
	if (_durationsLinked && !canEqualizeDurations()) {
		_durationsLinked = false;
	} else if (_durationsLinked) {
		equalizeDurations();
	}
	_durationsLinkChanges.fire({});
}

std::shared_ptr<float64> Scene::lastZ() const {
	return _lastZ;
}

Scene::ItemPtr Scene::itemShared(QGraphicsItem *item) const {
	const auto it = _itemsByPointer.find(item);
	return (it != end(_itemsByPointer)) ? it->second : nullptr;
}

float64 Scene::currentZoom() const {
	return _currentZoom;
}

void Scene::updateZoom(float64 zoom) {
	_currentZoom = zoom;
	_canvas->updateZoom(zoom);
	_stickyGuides->updateGeometry();
	for (const auto &item : items()) {
		if (item->type() >= ItemBase::Type) {
			static_cast<ItemBase*>(item.get())->updateZoom(zoom);
		}
	}
}

bool Scene::hasUndo() const {
	return ranges::any_of(_items, [](const ItemPtr &item) {
		return item->isNormalStatus() && item->undoable();
	});
}

bool Scene::hasRedo() const {
	return ranges::any_of(_items, &NumberedItem::isUndidStatus);
}

void Scene::performUndo() {
	const auto filtered = items(Qt::DescendingOrder);

	const auto it = ranges::find_if(filtered, [](const ItemPtr &item) {
		return item->isNormalStatus() && item->undoable();
	});
	if (it != filtered.end()) {
		if (const auto action = (*it)->asAction()) {
			action->revert();
		}
		(*it)->setStatus(NumberedItem::Status::Undid);
		if ((*it)->videoClip()) {
			checkDurationsLink();
		}
	}
}

void Scene::performRedo() {
	const auto filtered = items(Qt::AscendingOrder);

	const auto it = ranges::find_if(filtered, &NumberedItem::isUndidStatus);
	if (it != filtered.end()) {
		if (const auto action = (*it)->asAction()) {
			action->apply();
		}
		(*it)->setStatus(NumberedItem::Status::Normal);
		if ((*it)->videoClip()) {
			checkDurationsLink();
		}
	}
}

void Scene::capturePlacements() {
	_capturedPlacements.clear();
	for (const auto &item : _items) {
		if (item->isNormalStatus() && (item->type() >= ItemBase::Type)) {
			const auto base = std::static_pointer_cast<ItemBase>(item);
			_capturedPlacements.push_back({
				.item = base,
				.placement = base->placement(),
			});
		}
	}
}

void Scene::commitPlacements() {
	if (_capturedPlacements.empty()) {
		return;
	}
	auto targets = std::vector<ItemPlacement::Target>();
	for (auto &captured : _capturedPlacements) {
		const auto now = captured.item->placement();
		if (now != captured.placement) {
			targets.push_back({
				.item = std::move(captured.item),
				.before = captured.placement,
				.after = now,
			});
		}
	}
	_capturedPlacements.clear();
	if (!targets.empty()) {
		addItem(std::make_shared<ItemPlacement>(std::move(targets)));
	}
}

void Scene::removeIf(Fn<bool(const ItemPtr &)> proj) {
	auto copy = std::vector<ItemPtr>();
	for (const auto &item : _items) {
		const auto toRemove = proj(item);
		if (toRemove) {
			// Scene loses ownership of an item.
			// It seems for some reason this line causes a crash. =(
			// QGraphicsScene::removeItem(item.get());
			item->setSelected(false);
			item->setVisible(false);
			if (const auto animated = item->asAnimated()) {
				animated->releasePlayers();
			}
		} else {
			copy.push_back(item);
		}
	}
	_items = std::move(copy);
	_itemsByPointer.clear();
	for (const auto &item : _items) {
		_itemsByPointer.emplace(item.get(), item);
	}
}

void Scene::clearRedoList() {
	for (const auto &item : _items) {
		if (item->isUndidStatus()) {
			item->setStatus(NumberedItem::Status::Removed);
		}
	}
}

void Scene::save(SaveState state) {
	_textEdit->finishEditing(true);
	for (const auto &item : _items) {
		if (item->isNormalStatus()
			&& (item->type() == ItemText::Type)) {
			static_cast<ItemText*>(item.get())->bakeScale();
		}
	}

	removeIf([](const ItemPtr &item) {
		return item->isRemovedStatus()
			&& !item->hasState(SaveState::Keep)
			&& !item->hasState(SaveState::Save);
	});

	for (const auto &item : _items) {
		item->save(state);
	}
	auto &saved = (state == SaveState::Keep) ? _keptAudio : _savedAudio;
	saved = _audio ? std::make_shared<AudioTrack>(*_audio) : nullptr;
	auto &savedLinked = (state == SaveState::Keep)
		? _keptDurationsLinked
		: _savedDurationsLinked;
	savedLinked = _durationsLinked;
	setAudioSelected(false);
	clearSelection();
	cancelDrawing();
	hideStickyGuides();
}

void Scene::restore(SaveState state) {
	removeIf([=](const ItemPtr &item) {
		return !item->hasState(state);
	});

	for (const auto &item : _items) {
		item->restore(state);
	}
	const auto &saved = (state == SaveState::Keep)
		? _keptAudio
		: _savedAudio;
	setAudioSelected(false);
	setAudio(saved ? std::make_shared<AudioTrack>(*saved) : nullptr);
	_durationsLinked = (state == SaveState::Keep)
		? _keptDurationsLinked
		: _savedDurationsLinked;
	checkDurationsLink();
	clearSelection();
	cancelDrawing();
}

void Scene::createTextAtCenter(int rotation, bool flipped) {
	_textEdit->createAtCenter(rotation, flipped);
}

void Scene::startTextEditing(ItemText *item) {
	_textEdit->startEditing(item);
}

Scene::~Scene() {
	disconnect(this, &QGraphicsScene::selectionChanged, nullptr, nullptr);
	cancelTextEditing();
	if (const auto pending = base::take(_shapeTool.item)) {
		QGraphicsScene::removeItem(pending.get());
	}
	QGraphicsScene::removeItem(_canvas.get());
	QGraphicsScene::removeItem(_stickyGuides.get());
	for (const auto &item : items()) {
		QGraphicsScene::removeItem(item.get());
	}
}

} // namespace Editor
