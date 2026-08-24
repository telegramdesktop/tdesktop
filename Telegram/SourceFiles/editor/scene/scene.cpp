/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene.h"

#include "editor/scene/scene_item_canvas.h"
#include "editor/scene/scene_item_line.h"
#include "editor/scene/scene_item_shape.h"
#include "editor/scene/scene_item_sticker.h"
#include "editor/scene/scene_item_text.h"
#include "editor/scene/scene_emoji_document.h"
#include "ui/image/image_prepare.h"
#include "ui/rp_widget.h"
#include "styles/style_editor.h"

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QTextCursor>
#include <QTextDocument>
#include <QtMath>

namespace Editor {
namespace {

using ItemPtr = std::shared_ptr<NumberedItem>;

class ItemAction : public NumberedItem {
public:
	using NumberedItem::NumberedItem;

	virtual void apply() = 0;
	virtual void revert() = 0;

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

constexpr auto kPaddingFactor = 0.4;
constexpr auto kMaxWidthFactor = 0.8;
constexpr auto kMinWidthFactor = 0.16;
constexpr auto kIdealWidthExtra = 2;
constexpr auto kScaleThreshold = 0.01;
constexpr auto kShapeDragThreshold = 4.;
constexpr auto kShapeSnapAngle = 45.;
constexpr auto kDraftShapeOpacity = 0.5;

class TextEditProxy final : public QGraphicsTextItem {
public:
	using QGraphicsTextItem::QGraphicsTextItem;

	Fn<void()> onFinish;
	Fn<void()> onCancel;

protected:
	void keyPressEvent(QKeyEvent *event) override {
		if (event->key() == Qt::Key_Escape) {
			fire(onCancel);
			return;
		}
		QGraphicsTextItem::keyPressEvent(event);
	}

	void focusOutEvent(QFocusEvent *event) override {
		QGraphicsTextItem::focusOutEvent(event);
		fire(onFinish);
	}

	void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
		event->accept();
	}

private:
	void fire(Fn<void()> &callback) {
		if (!callback) {
			return;
		}
		const auto cb = std::exchange(callback, nullptr);
		onFinish = nullptr;
		onCancel = nullptr;
		crl::on_main(cb);
	}
};

} // namespace

Scene::Scene(const QRectF &rect)
: QGraphicsScene(rect)
, _canvas(std::make_shared<ItemCanvas>())
, _lastZ(std::make_shared<float64>(9000.)) {
	QGraphicsScene::addItem(_canvas.get());
	_canvas->clearPixmap();

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
			{
				auto p = QPainter(&source);
				render(
					&p,
					QRectF(QPointF(), QSizeF(captureRect.size())),
					QRectF(captureRect),
					Qt::IgnoreAspectRatio);
			}
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
		});
}

void Scene::cancelDrawing() {
	if (_textEdit.proxy) {
		finishTextEditing(false);
	}
	_canvas->cancelDrawing();
}

void Scene::cancelTextEditing() {
	if (_textEdit.proxy) {
		finishTextEditing(false, false);
	}
}

void Scene::addItem(ItemPtr item) {
	if (!item) {
		return;
	}
	item->setNumber(_itemNumber++);
	if (item->scene() != this) {
		QGraphicsScene::addItem(item.get());
	}
	const auto raw = item.get();
	_items.push_back(std::move(item));
	_itemsByPointer.emplace(raw, _items.back());
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
	_removesItem.fire({});
}

void Scene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
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
	if (_textEdit.proxy) {
		const auto clickOnProxy = _textEdit.proxy->contains(
			_textEdit.proxy->mapFromScene(event->scenePos()));
		if (!clickOnProxy) {
			finishTextEditing(true);
			QGraphicsScene::mousePressEvent(event);
			capturePlacements();
			return;
		}
	}

	QGraphicsScene::mousePressEvent(event);
	capturePlacements();
	if (SkipMouseEvent(event) || !sceneRect().contains(event->scenePos())) {
		return;
	}
	_canvas->handleMousePressEvent(event);
}

void Scene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	if (_shapeTool.dragging && (event->button() == Qt::LeftButton)) {
		event->accept();
		finishShapeDrawing(true);
		return;
	}
	QGraphicsScene::mouseReleaseEvent(event);
	commitPlacements();
	if (SkipMouseEvent(event) || _textEdit.proxy) {
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
	if (SkipMouseEvent(event) || _textEdit.proxy) {
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
		if (_textEdit.proxy) {
			finishTextEditing(true);
		}
		clearSelection();
		clearFocus();
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
		int style) {
	_textColor = color;
	_textFontSize = fontSize;
	_textStyle = style;
}

void Scene::setTextColor(const QColor &color) {
	_textColor = color;
	if (_textEdit.proxy) {
		_textEdit.proxy->setDefaultTextColor(EffectiveTextColor(
			color,
			static_cast<TextStyle>(_textEditStyle)));
	}
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
	return _textColorRequests.events();
}

rpl::producer<QColor> Scene::textItemSelections() const {
	return _textItemSelections.events();
}

rpl::producer<> Scene::textItemDeselections() const {
	return _textItemDeselections.events();
}

rpl::producer<bool> Scene::textEditStates() const {
	return _textEditStates.events();
}

rpl::producer<QColor> Scene::shapeItemSelections() const {
	return _shapeItemSelections.events();
}

rpl::producer<> Scene::shapeItemDeselections() const {
	return _shapeItemDeselections.events();
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
		if (item->isNormalStatus()
			&& (item->type() == ItemSticker::Type)) {
			const auto sticker = static_cast<ItemSticker*>(item.get());
			if (sticker->animated() && !sticker->content().isEmpty()) {
				return true;
			}
		}
	}
	return false;
}

void Scene::releaseAnimations() {
	for (const auto &item : _items) {
		if (item->type() == ItemSticker::Type) {
			static_cast<ItemSticker*>(item.get())->releasePlayers();
		}
	}
}

std::shared_ptr<float64> Scene::lastZ() const {
	return _lastZ;
}

void Scene::updateZoom(float64 zoom) {
	_currentZoom = zoom;
	_canvas->updateZoom(zoom);
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
		if (const auto action = dynamic_cast<ItemAction*>(it->get())) {
			action->revert();
		}
		(*it)->setStatus(NumberedItem::Status::Undid);
	}
}

void Scene::performRedo() {
	const auto filtered = items(Qt::AscendingOrder);

	const auto it = ranges::find_if(filtered, &NumberedItem::isUndidStatus);
	if (it != filtered.end()) {
		if (const auto action = dynamic_cast<ItemAction*>(it->get())) {
			action->apply();
		}
		(*it)->setStatus(NumberedItem::Status::Normal);
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
	if (_textEdit.proxy) {
		finishTextEditing(true);
	}

	removeIf([](const ItemPtr &item) {
		return item->isRemovedStatus()
			&& !item->hasState(SaveState::Keep)
			&& !item->hasState(SaveState::Save);
	});

	for (const auto &item : _items) {
		item->save(state);
	}
	clearSelection();
	cancelDrawing();
}

void Scene::restore(SaveState state) {
	removeIf([=](const ItemPtr &item) {
		return !item->hasState(state);
	});

	for (const auto &item : _items) {
		item->restore(state);
	}
	clearSelection();
	cancelDrawing();
}

void Scene::setTextEditing(bool editing, bool notify) {
	if (_textEditing == editing) {
		return;
	}
	_textEditing = editing;
	if (notify) {
		_textEditStates.fire_copy(editing);
	}
}

void Scene::setupTextProxy(
		QGraphicsTextItem *proxy,
		const QColor &color,
		float64 fontSize) {
	proxy->setTextInteractionFlags(Qt::TextEditorInteraction);
	proxy->setDefaultTextColor(color);

	auto *emojiDoc = new EmojiDocument(proxy);
	emojiDoc->setDocumentMargin(0);
	proxy->setDocument(emojiDoc);

	auto font = QFont();
	font.setPixelSize(int(fontSize));
	font.setWeight(QFont::DemiBold);
	proxy->setFont(font);

	{
		auto option = emojiDoc->defaultTextOption();
		option.setAlignment(Qt::AlignCenter);
		emojiDoc->setDefaultTextOption(option);
	}
}

void Scene::createTextAtCenter(int rotation) {
	if (_textEdit.proxy) {
		return;
	}

	const auto generation = ++_textEditGeneration;

	clearSelection();
	cancelDrawing();
	setTextEditing(true);
	_textEditStyle = _textStyle;

	_textEdit.proxy.reset(new TextEditProxy());
	const auto proxy = _textEdit.proxy.get();
	setupTextProxy(
		proxy,
		EffectiveTextColor(
			_textColor,
			static_cast<TextStyle>(_textEditStyle)),
		_textFontSize);

	const auto emojiDoc = proxy->document();
	const auto shortSide = std::min(
		sceneRect().width(),
		sceneRect().height());
	const auto padding = int(_textFontSize * kPaddingFactor);
	const auto maxTextWidth = std::max(
		int(shortSide * kMaxWidthFactor) - 2 * padding,
		1);
	const auto minTextWidth = std::clamp(
		int(shortSide * kMinWidthFactor) - 2 * padding,
		1,
		maxTextWidth);
	const auto sceneCenter = sceneRect().center();
	const auto adjustWidth = [=] {
		emojiDoc->setTextWidth(maxTextWidth);
		const auto ideal = int(std::ceil(emojiDoc->idealWidth()));
		const auto width = std::clamp(
			ideal + kIdealWidthExtra,
			minTextWidth,
			maxTextWidth);
		proxy->setTextWidth(width);
		const auto anchor = QPointF(width / 2., 0.);
		proxy->setTransformOriginPoint(anchor);
		proxy->setPos(sceneCenter - anchor);
	};
	adjustWidth();
	proxy->setRotation(rotation);

	QObject::connect(emojiDoc, &QTextDocument::contentsChanged, [=] {
		ReplaceEmoji(emojiDoc);
		adjustWidth();
	});

	QGraphicsScene::addItem(proxy);
	proxy->setZValue((*_lastZ)++);
	proxy->setFocus();
	if (!views().isEmpty()) {
		views().first()->setFocus();
	}

	const auto raw = static_cast<TextEditProxy*>(proxy);
	raw->onFinish = crl::guard(this, [=] {
		if (generation == _textEditGeneration) {
			finishTextEditing(true);
		}
	});
	raw->onCancel = crl::guard(this, [=] {
		if (generation == _textEditGeneration) {
			finishTextEditing(false);
		}
	});

	_textEdit.item.reset();
	_textColorRequests.fire_copy(_textColor);
}

void Scene::startTextEditing(ItemText *item) {
	if (_textEdit.proxy) {
		finishTextEditing(true);
	}
	if (!item) {
		return;
	}

	const auto generation = ++_textEditGeneration;

	cancelDrawing();
	setTextEditing(true);
	_textEditStyle = int(item->textStyle());

	_textEdit.proxy.reset(new TextEditProxy());
	const auto proxy = _textEdit.proxy.get();
	setupTextProxy(
		proxy,
		EffectiveTextColor(item->color(), item->textStyle()),
		item->fontSize());

	proxy->setPlainText(item->text());
	ReplaceEmoji(proxy->document());

	const auto emojiDoc = proxy->document();
	const auto shortSide = std::min(
		sceneRect().width(),
		sceneRect().height());
	const auto padding = int(item->fontSize() * kPaddingFactor);
	const auto maxTextWidth = std::max(
		int(shortSide * kMaxWidthFactor) - 2 * padding,
		1);
	const auto minTextWidth = std::clamp(
		int(shortSide * kMinWidthFactor) - 2 * padding,
		1,
		maxTextWidth);
	const auto anchor = item->scenePos();
	const auto adjustWidth = [=] {
		emojiDoc->setTextWidth(maxTextWidth);
		const auto ideal = int(std::ceil(emojiDoc->idealWidth()));
		const auto width = std::clamp(
			ideal + kIdealWidthExtra,
			minTextWidth,
			maxTextWidth);
		proxy->setTextWidth(width);
		const auto center = proxy->boundingRect().center();
		proxy->setTransformOriginPoint(center);
		proxy->setPos(anchor - center);
	};
	adjustWidth();

	QObject::connect(emojiDoc, &QTextDocument::contentsChanged, [=] {
		ReplaceEmoji(emojiDoc);
		adjustWidth();
	});

	const auto scale = item->editScale();
	proxy->setRotation(item->rotation());
	if (std::abs(scale - 1.) > kScaleThreshold) {
		proxy->setScale(scale);
	}

	QGraphicsScene::addItem(proxy);
	proxy->setZValue((*_lastZ)++);
	proxy->setFocus();

	auto cursor = proxy->textCursor();
	cursor.select(QTextCursor::Document);
	proxy->setTextCursor(cursor);

	item->setVisible(false);

	const auto raw = static_cast<TextEditProxy*>(proxy);
	raw->onFinish = crl::guard(this, [=] {
		if (generation == _textEditGeneration) {
			finishTextEditing(true);
		}
	});
	raw->onCancel = crl::guard(this, [=] {
		if (generation == _textEditGeneration) {
			finishTextEditing(false);
		}
	});

	const auto it = _itemsByPointer.find(item);
	_textEdit.item = (it != end(_itemsByPointer))
		? it->second
		: std::weak_ptr<NumberedItem>();
	_textColorRequests.fire_copy(item->color());
}

void Scene::finishTextEditing(bool save, bool notify) {
	if (!_textEdit.proxy) {
		return;
	}

	const auto text = save
		? RecoverTextFromDocument(_textEdit.proxy->document()).trimmed()
		: QString();
	const auto proxyRect = _textEdit.proxy->boundingRect();
	const auto proxyCenter = _textEdit.proxy->mapToScene(proxyRect.center());
	const auto proxyRotation = int(_textEdit.proxy->rotation());
	const auto lockedItem = _textEdit.item.lock();
	auto *existingItem = lockedItem
		? static_cast<ItemText*>(lockedItem.get())
		: (ItemText*)(nullptr);

	const auto raw = static_cast<TextEditProxy*>(_textEdit.proxy.get());
	raw->onFinish = nullptr;
	raw->onCancel = nullptr;
	QGraphicsScene::removeItem(_textEdit.proxy.get());
	_textEdit.proxy = nullptr;
	_textEdit.item.reset();
	setTextEditing(false, notify);

	const auto defaultStyle = static_cast<TextStyle>(_textStyle);

	if (!text.isEmpty()) {
		if (existingItem) {
			existingItem->setText(text);
			existingItem->setVisible(true);
		} else {
			const auto imageSize = sceneRect().size().toSize();
			const auto contentSize = ItemText::computeContentSize(
				text,
				_textFontSize,
				imageSize,
				defaultStyle);
			const auto zoom = (_currentZoom > 0.) ? _currentZoom : 1.;
			const auto handleInflate = int(
				std::ceil(st::photoEditorItemHandleSize / zoom));
			const auto size = std::max(
				contentSize.width() + handleInflate,
				1);
			auto data = ItemBase::Data{
				.initialZoom = zoom,
				.zPtr = _lastZ,
				.size = size,
				.x = int(proxyCenter.x()),
				.y = int(proxyCenter.y()),
				.rotation = proxyRotation,
				.imageSize = imageSize,
			};
			auto item = std::make_shared<ItemText>(
				text,
				_textColor,
				_textFontSize,
				defaultStyle,
				imageSize,
				std::move(data));
			addItem(item);
		}
	} else if (existingItem) {
		if (save) {
			removeItem(existingItem);
		} else {
			existingItem->setVisible(true);
		}
	}
}

Scene::~Scene() {
	disconnect(this, &QGraphicsScene::selectionChanged, nullptr, nullptr);
	cancelTextEditing();
	if (const auto pending = base::take(_shapeTool.item)) {
		QGraphicsScene::removeItem(pending.get());
	}
	QGraphicsScene::removeItem(_canvas.get());
	for (const auto &item : items()) {
		QGraphicsScene::removeItem(item.get());
	}
}

} // namespace Editor
