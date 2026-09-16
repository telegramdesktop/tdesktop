/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_base.h"

#include "editor/scene/scene.h"
#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "ui/painter.h"
#include "styles/style_editor.h"
#include "styles/style_menu_icons.h"

#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>
#include <QtWidgets/QApplication>
#include <QtMath>

namespace Editor {
namespace {

constexpr auto kSnapAngle = 45.;

const auto kDuplicateSequence = QKeySequence("ctrl+d");
const auto kFlipSequence = QKeySequence("ctrl+s");
const auto kDeleteSequence = QKeySequence("delete");
const auto kBackspaceSequence = QKeySequence("backspace");

constexpr auto kMinSizeRatio = 0.05;

constexpr auto kStickyDuration = crl::time(150);
constexpr auto kStickyLines = 5;

auto Normalized(float64 angle) {
	return angle
		+ ((std::abs(angle) < 360) ? 0 : (-360 * (angle < 0 ? -1 : 1)));
}

[[nodiscard]] float64 StickyStart(
		const QRectF &rect,
		Qt::Orientation orientation) {
	return (orientation == Qt::Horizontal) ? rect.left() : rect.top();
}

[[nodiscard]] float64 StickyLength(
		const QRectF &rect,
		Qt::Orientation orientation) {
	return (orientation == Qt::Horizontal) ? rect.width() : rect.height();
}

[[nodiscard]] float64 StickyLine(
		const QRectF &canvas,
		Qt::Orientation orientation,
		int line) {
	return StickyStart(canvas, orientation)
		+ StickyLength(canvas, orientation) * line / (kStickyLines - 1);
}

} // namespace

int NumberedItem::type() const {
	return NumberedItem::Type;
}

int NumberedItem::number() const {
	return _number;
}

void NumberedItem::setNumber(int number) {
	_number = number;
}

NumberedItem::Status NumberedItem::status() const {
	return _status;
}

bool NumberedItem::isNormalStatus() const {
	return _status == Status::Normal;
}

bool NumberedItem::isUndidStatus() const {
	return _status == Status::Undid;
}

bool NumberedItem::isRemovedStatus() const {
	return _status == Status::Removed;
}

ItemAction *NumberedItem::asAction() {
	return nullptr;
}

ItemAnimated *NumberedItem::asAnimated() {
	return nullptr;
}

VideoClip *NumberedItem::videoClip() {
	return nullptr;
}

void NumberedItem::save(SaveState state) {
}

void NumberedItem::restore(SaveState state) {
}

bool NumberedItem::hasState(SaveState state) const {
	return false;
}

void NumberedItem::setStatus(Status status) {
	if (status != _status) {
		_status = status;
		setVisible(status == Status::Normal);
	}
}

void NumberedItem::setUndoable(bool undoable) {
	_undoable = undoable;
}

bool NumberedItem::undoable() const {
	return _undoable;
}

ItemBase::ItemBase(Data data)
: _lastZ(data.zPtr)
, _imageSize(data.imageSize)
, _maxSizeRatio(data.maxSizeRatio)
, _contentMargins(data.contentMargins)
, _horizontalSize(data.size) {
	setFlags(QGraphicsItem::ItemIsMovable
		| QGraphicsItem::ItemIsSelectable
		| QGraphicsItem::ItemIsFocusable);
	setAcceptHoverEvents(true);
	applyData(data);
}

QRectF ItemBase::boundingRect() const {
	return innerRect() + _scaledInnerMargins;
}

QRectF ItemBase::contentRect() const {
	return _contentMargins
		? (innerRect() - _scaledInnerMargins)
		: innerRect();
}

QRectF ItemBase::innerRect() const {
	const auto &hSize = _horizontalSize;
	const auto &vSize = _verticalSize;
	return QRectF(-hSize / 2, -vSize / 2, hSize, vSize);
}

QRectF ItemBase::fittedRect(QSizeF size) const {
	const auto rect = contentRect();
	if (size.isEmpty()) {
		return rect;
	}
	const auto fitted = size.scaled(rect.size(), Qt::KeepAspectRatio);
	return QRectF(rect.topLeft(), fitted).translated(
		(rect.width() - fitted.width()) / 2.,
		(rect.height() - fitted.height()) / 2.);
}

QRectF ItemBase::visibleRect() const {
	return contentRect();
}

void ItemBase::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *) {
	if (!(option->state & QStyle::State_Selected)) {
		return;
	}
	PainterHighQualityEnabler hq(*p);
	const auto hasFocus = (option->state & QStyle::State_HasFocus);
	p->setPen(hasFocus ? _pens.select : _pens.selectInactive);
	p->drawRect(innerRect());

	paintHandle(p, rightHandleRect(), hasFocus);
	paintHandle(p, leftHandleRect(), hasFocus);
}

void ItemBase::paintHandle(
		QPainter *p,
		const QRectF &rect,
		bool hasFocus) const {
	p->setPen(hasFocus ? _pens.handle : _pens.handleInactive);
	p->setBrush(st::photoEditorItemBaseHandleFg);
	p->drawEllipse(rect);
}

void ItemBase::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
	if (!dragThresholdPassed(event)) {
		return;
	}
	if (isHandling()) {
		const auto mousePos = event->pos();
		const auto shift = event->modifiers().testFlag(Qt::ShiftModifier);
		const auto isLeft = (_handle == HandleType::Left);
		if (!shift) {
			// Resize.
			const auto p = isLeft ? (mousePos * -1) : mousePos;
			const auto dx = int(2.0 * p.x());
			const auto dy = int(2.0 * p.y());
			prepareGeometryChange();
			_horizontalSize = std::clamp(
				(dx > dy ? dx : dy),
				_sizeLimits.min,
				_sizeLimits.max);
			updateVerticalSize();
		}

		// Rotate.
		const auto origin = mapToScene(boundingRect().center());
		const auto pos = mapToScene(mousePos);

		const auto diff = pos - origin;
		const auto angle = Normalized((isLeft ? 180 : 0)
			+ (std::atan2(diff.y(), diff.x()) * 180 / M_PI));
		setRotation(shift
			? (base::SafeRound(angle / kSnapAngle) * kSnapAngle)
			: angle);
	} else {
		QGraphicsItem::mouseMoveEvent(event);
		updateSticky(event->modifiers().testFlag(Qt::ShiftModifier));
	}
}

void ItemBase::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
	const auto owner = static_cast<Scene*>(scene());
	setCursor((owner && owner->hasPendingShape())
		? Qt::CrossCursor
		: isHandling()
		? Qt::ClosedHandCursor
		: (handleType(event->pos()) != HandleType::None) && isSelected()
		? Qt::OpenHandCursor
		: Qt::ArrowCursor);
	QGraphicsItem::hoverMoveEvent(event);
}

void ItemBase::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	raiseToTop();
	resetDragging();
	if (event->button() == Qt::LeftButton) {
		_handle = handleType(event->pos());
	}
	if (isHandling()) {
		setCursor(Qt::ClosedHandCursor);
	} else {
		QGraphicsItem::mousePressEvent(event);
		if (event->button() == Qt::LeftButton) {
			startStickyDrag();
		}
	}
}

void ItemBase::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	if ((event->button() == Qt::LeftButton) && isHandling()) {
		_handle = HandleType::None;
	} else {
		if (event->button() == Qt::LeftButton) {
			finishStickyDrag();
		}
		QGraphicsItem::mouseReleaseEvent(event);
	}
}

ItemBase::StickyAxis &ItemBase::stickyAxis(Qt::Orientation orientation) {
	return (orientation == Qt::Horizontal) ? _stickyX : _stickyY;
}

const ItemBase::StickyAxis &ItemBase::stickyAxis(
		Qt::Orientation orientation) const {
	return (orientation == Qt::Horizontal) ? _stickyX : _stickyY;
}

void ItemBase::startStickyDrag() {
	resetStickyAxis(Qt::Horizontal);
	resetStickyAxis(Qt::Vertical);
	_stickyDrag = { .raw = pos(), .active = true };
	notifyStickyGuides();
}

void ItemBase::resetStickyAxis(Qt::Orientation orientation) {
	auto &axis = stickyAxis(orientation);
	axis.animation.stop();
	axis.current = axis.last = Sticky();
}

void ItemBase::updateSticky(bool enabled) {
	if (!_stickyDrag.active) {
		return;
	}
	_stickyDrag.raw = pos();
	_stickyDrag.others.clear();
	if (const auto s = scene()) {
		for (const auto item : s->selectedItems()) {
			if ((item != this)
				&& (item->flags() & QGraphicsItem::ItemIsMovable)) {
				_stickyDrag.others.emplace_back(item, item->pos());
			}
		}
	}
	applyStickyState(enabled);
}

void ItemBase::applyStickyState(bool enabled) {
	if (!_stickyDrag.active) {
		return;
	}
	for (const auto orientation : { Qt::Horizontal, Qt::Vertical }) {
		applySticky(
			orientation,
			enabled ? computeSticky(orientation) : Sticky());
	}
	applyStickyPosition();
}

void ItemBase::finishStickyDrag() {
	if (!_stickyDrag.active) {
		return;
	}
	for (const auto orientation : { Qt::Horizontal, Qt::Vertical }) {
		stickyAxis(orientation).animation.stop();
	}
	applyStickyPosition();
	_stickyDrag = {};
	resetStickyAxis(Qt::Horizontal);
	resetStickyAxis(Qt::Vertical);
	notifyStickyGuides();
}

void ItemBase::applySticky(Qt::Orientation orientation, Sticky sticky) {
	auto &axis = stickyAxis(orientation);
	if (axis.current == sticky) {
		return;
	}
	const auto to = sticky.valid();
	const auto continues = !to || (sticky == axis.last);
	const auto from = continues
		? axis.animation.value(axis.current.valid() ? 1. : 0.)
		: 0.;
	axis.current = sticky;
	if (to) {
		axis.last = axis.current;
	}
	axis.animation = {};
	axis.animation.start(
		[=] { applyStickyPosition(); },
		from,
		to ? 1. : 0.,
		kStickyDuration,
		anim::easeOutCubic);
	notifyStickyGuides();
}

void ItemBase::applyStickyPosition() {
	if (!_stickyDrag.active) {
		return;
	}
	const auto offset = QPointF(
		stickyOffset(Qt::Horizontal),
		stickyOffset(Qt::Vertical));
	setPos(_stickyDrag.raw + offset);
	for (const auto &[item, raw] : _stickyDrag.others) {
		item->setPos(raw + offset);
	}
}

void ItemBase::notifyStickyGuides() {
	if (const auto owner = static_cast<Scene*>(scene())) {
		owner->setStickyGuides(
			stickyGuide(Qt::Horizontal),
			stickyGuide(Qt::Vertical));
	}
}

QRectF ItemBase::stickyBounds() const {
	return mapToScene(visibleRect()).boundingRect().translated(
		_stickyDrag.raw - pos());
}

ItemBase::Sticky ItemBase::computeSticky(Qt::Orientation orientation) const {
	struct Candidate {
		Sticky sticky;
		float64 shift = 0.;
		int priority = 0;
	};
	const auto priority = [](const Sticky &sticky) {
		const auto anchor = (sticky.anchor == StickyAnchor::Center) ? 0 : 1;
		const auto line = (sticky.line * 2 == kStickyLines - 1)
			? 0
			: (sticky.line == 0 || sticky.line == kStickyLines - 1)
			? 1
			: 2;
		return anchor * 3 + line;
	};
	auto candidates = std::vector<Candidate>();
	candidates.reserve(kStickyLines * 3);
	for (auto line = 0; line != kStickyLines; ++line) {
		for (const auto anchor : {
				StickyAnchor::Start,
				StickyAnchor::Center,
				StickyAnchor::End }) {
			const auto sticky = Sticky{ line, anchor };
			candidates.push_back({
				.sticky = sticky,
				.shift = stickyShift(orientation, sticky),
				.priority = priority(sticky),
			});
		}
	}
	ranges::sort(candidates, ranges::less(), &Candidate::priority);
	auto kept = std::vector<float64>();
	auto result = Sticky();
	auto best = _scaledStickyTrigger;
	for (const auto &candidate : candidates) {
		if (std::abs(candidate.shift) > _scaledStickyTrigger) {
			continue;
		}
		const auto overlaps = ranges::any_of(kept, [&](float64 shift) {
			return std::abs(shift - candidate.shift)
				< _scaledStickyTrigger * 2;
		});
		if (overlaps) {
			continue;
		}
		kept.push_back(candidate.shift);
		if (std::abs(candidate.shift) <= best) {
			best = std::abs(candidate.shift);
			result = candidate.sticky;
		}
	}
	return result;
}

float64 ItemBase::stickyShift(
		Qt::Orientation orientation,
		Sticky sticky) const {
	const auto owner = static_cast<Scene*>(scene());
	if (!owner || !sticky.valid()) {
		return 0.;
	}
	const auto bounds = stickyBounds();
	const auto start = StickyStart(bounds, orientation);
	const auto length = StickyLength(bounds, orientation);
	const auto anchor = (sticky.anchor == StickyAnchor::Start)
		? start
		: (sticky.anchor == StickyAnchor::Center)
		? (start + length / 2.)
		: (start + length);
	return StickyLine(owner->canvasRect(), orientation, sticky.line) - anchor;
}

float64 ItemBase::stickyOffset(Qt::Orientation orientation) const {
	const auto &axis = stickyAxis(orientation);
	const auto stuck = axis.current.valid();
	const auto sticky = stuck ? axis.current : axis.last;
	if (!sticky.valid()) {
		return 0.;
	}
	const auto progress = axis.animation.value(stuck ? 1. : 0.);
	return stickyShift(orientation, sticky) * progress;
}

std::optional<float64> ItemBase::stickyGuide(
		Qt::Orientation orientation) const {
	const auto owner = static_cast<Scene*>(scene());
	const auto sticky = stickyAxis(orientation).current;
	if (!owner || !sticky.valid()) {
		return std::nullopt;
	}
	return StickyLine(owner->canvasRect(), orientation, sticky.line);
}

void ItemBase::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
	if (scene()) {
		scene()->clearSelection();
		setSelected(true);
	}

	const auto add = [&](
			auto base,
			const QKeySequence &sequence,
			Fn<void()> callback,
			const style::icon *icon) {
		// TODO: refactor.
		const auto sequenceText = QChar('\t')
			+ sequence.toString(QKeySequence::NativeText);
		_menu->addAction(
			base(tr::now) + sequenceText,
			std::move(callback),
			icon);
	};

	_menu = base::make_unique_q<Ui::PopupMenu>(
		nullptr,
		st::photoEditorMediaMenu);
	fillContextMenu(_menu.get());
	if (!_menu->empty()) {
		_menu->addSeparator();
	}
	add(
		tr::lng_photo_editor_menu_delete,
		kDeleteSequence,
		[=] { actionDelete(); },
		&st::mediaMenuIconDelete);
	if (flippable()) {
		add(
			tr::lng_photo_editor_menu_flip,
			kFlipSequence,
			[=] { actionFlip(); },
			&st::mediaMenuIconFlip);
	}
	add(
		tr::lng_photo_editor_menu_duplicate,
		kDuplicateSequence,
		[=] { actionDuplicate(); },
		&st::mediaMenuIconCopy);

	_menu->popup(event->screenPos());
}

void ItemBase::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
}

void ItemBase::performForSelectedItems(Action action) {
	if (const auto s = scene()) {
		for (const auto item : s->selectedItems()) {
			if (const auto base = static_cast<ItemBase*>(item)) {
				(base->*action)();
			}
		}
	}
}

bool ItemBase::flippable() const {
	return true;
}

void ItemBase::actionFlip() {
	if (flippable()) {
		setFlip(!flipped());
	}
}

void ItemBase::actionDelete() {
	if (const auto s = static_cast<Scene*>(scene())) {
		s->removeItem(this);
	}
}

void ItemBase::actionDuplicate() {
	if (const auto s = static_cast<Scene*>(scene())) {
		auto data = generateData();
		data.x += int(_horizontalSize / 3);
		data.y += int(_verticalSize / 3);
		const auto newItem = duplicate(std::move(data));
		if (hasFocus()) {
			newItem->setFocus();
		}
		const auto selected = isSelected();
		newItem->setSelected(selected);
		setSelected(false);
		s->addItem(newItem);
	}
}

void ItemBase::raiseToTop() {
	setZValue((*_lastZ)++);
}

bool ItemBase::sceneEvent(QEvent *event) {
	if (event->type() == QEvent::UngrabMouse) {
		_handle = HandleType::None;
		finishStickyDrag();
	}
	return NumberedItem::sceneEvent(event);
}

void ItemBase::keyReleaseEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Shift) {
		applyStickyState(e->modifiers().testFlag(Qt::ShiftModifier));
	}
	NumberedItem::keyReleaseEvent(e);
}

void ItemBase::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Shift) {
		applyStickyState(e->modifiers().testFlag(Qt::ShiftModifier));
	} else if (e->key() == Qt::Key_Escape) {
		if (const auto s = scene()) {
			s->clearSelection();
			s->clearFocus();
			return;
		}
	}
	handleActionKey(e);
}

void ItemBase::handleActionKey(not_null<QKeyEvent*> e) {
	const auto matches = [&](const QKeySequence &sequence) {
		const auto searchKey = (e->modifiers() | e->key())
			& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
		const auto events = QKeySequence(searchKey);
		return sequence.matches(events) == QKeySequence::ExactMatch;
	};
	if (matches(kDuplicateSequence)) {
		performForSelectedItems(&ItemBase::actionDuplicate);
	} else if (matches(kDeleteSequence) || matches(kBackspaceSequence)) {
		performForSelectedItems(&ItemBase::actionDelete);
	} else if (matches(kFlipSequence)) {
		performForSelectedItems(&ItemBase::actionFlip);
	} else {
		e->ignore();
	}
}

QRectF ItemBase::rightHandleRect() const {
	return QRectF(
		(_horizontalSize / 2) - (_scaledHandleSize / 2),
		0 - (_scaledHandleSize / 2),
		_scaledHandleSize,
		_scaledHandleSize);
}

QRectF ItemBase::leftHandleRect() const {
	return QRectF(
		(-_horizontalSize / 2) - (_scaledHandleSize / 2),
		0 - (_scaledHandleSize / 2),
		_scaledHandleSize,
		_scaledHandleSize);
}

bool ItemBase::isHandling() const {
	return _handle != HandleType::None;
}

void ItemBase::resetDragging() {
	_dragging = false;
}

bool ItemBase::dragThresholdPassed(
		not_null<QGraphicsSceneMouseEvent*> event) {
	if (!_dragging) {
		const auto delta = event->screenPos()
			- event->buttonDownScreenPos(Qt::LeftButton);
		const auto distance = QApplication::startDragDistance();
		_dragging = (delta.manhattanLength() >= distance);
	}
	return _dragging;
}

float64 ItemBase::size() const {
	return _horizontalSize;
}

float64 ItemBase::horizontalSize() const {
	return _horizontalSize;
}

float64 ItemBase::verticalSize() const {
	return _verticalSize;
}

float64 ItemBase::verticalMinimum() const {
	return _verticalMinimumEnabled ? float64(_sizeLimits.min) : 1.;
}

void ItemBase::updateVerticalSize() {
	const auto verticalSize = _horizontalSize * _aspectRatio;
	const auto minimum = verticalMinimum();
	_verticalSize = std::max(verticalSize, minimum);
	if (verticalSize < minimum) {
		_horizontalSize = _verticalSize / _aspectRatio;
	}
}

void ItemBase::setVerticalMinimumEnabled(bool enabled) {
	if (_verticalMinimumEnabled != enabled) {
		_verticalMinimumEnabled = enabled;
		updateVerticalSize();
	}
}

void ItemBase::setAspectRatio(float64 aspectRatio) {
	prepareGeometryChange();
	_aspectRatio = aspectRatio;
	updateVerticalSize();
}

void ItemBase::applyStretch(
		float64 horizontal,
		float64 vertical,
		bool allowBelowMinimum) {
	prepareGeometryChange();
	_horizontalSize = std::clamp(
		horizontal,
		allowBelowMinimum ? 1. : float64(_sizeLimits.min),
		float64(_sizeLimits.max));
	_verticalSize = std::clamp(
		vertical,
		allowBelowMinimum ? 1. : verticalMinimum(),
		float64(_sizeLimits.max));
	_aspectRatio = _verticalSize / _horizontalSize;
}

bool ItemBase::fitsMinimumSize() const {
	return (_horizontalSize >= _sizeLimits.min)
		&& (_verticalSize >= verticalMinimum());
}

float64 ItemBase::scaledHandleSize() const {
	return _scaledHandleSize;
}

ItemBase::HandleType ItemBase::handleType(const QPointF &pos) const {
	return rightHandleRect().contains(pos)
		? HandleType::Right
		: leftHandleRect().contains(pos)
		? HandleType::Left
		: HandleType::None;
}

bool ItemBase::flipped() const {
	return _flipped;
}

void ItemBase::setFlip(bool value) {
	if (_flipped != value) {
		performFlip();
		_flipped = value;
	}
}

int ItemBase::type() const {
	return ItemBase::Type;
}

void ItemBase::updateZoom(float64 zoom) {
	_scaledHandleSize = st::photoEditorItemHandleSize / zoom;
	_scaledStickyTrigger = st::photoEditorStickyTrigger / zoom;
	_scaledInnerMargins = QMarginsF(
		_scaledHandleSize,
		_scaledHandleSize,
		_scaledHandleSize,
		_scaledHandleSize) * 0.5;

	const auto maxSide = std::max(
		_imageSize.width(),
		_imageSize.height());
	_sizeLimits = {
		.min = std::max(int(maxSide * kMinSizeRatio), 1),
		.max = std::max(int(maxSide * _maxSizeRatio), 1),
	};
	_horizontalSize = std::clamp(
		_horizontalSize,
		float64(_sizeLimits.min),
		float64(_sizeLimits.max));
	updateVerticalSize();

	updatePens(QPen(
		QBrush(),
		1 / zoom,
		Qt::DashLine,
		Qt::SquareCap,
		Qt::RoundJoin));
}

void ItemBase::performFlip() {
}

void ItemBase::updatePens(QPen pen) {
	_pens = {
		.select = pen,
		.selectInactive = pen,
		.handle = pen,
		.handleInactive = pen,
	};
	_pens.select.setColor(Qt::white);
	_pens.selectInactive.setColor(Qt::gray);
	_pens.handle.setColor(Qt::white);
	_pens.handleInactive.setColor(Qt::gray);
	_pens.handle.setStyle(Qt::SolidLine);
	_pens.handleInactive.setStyle(Qt::SolidLine);
}

ItemBase::Data ItemBase::generateData() const {
	return {
		.initialZoom = (st::photoEditorItemHandleSize / _scaledHandleSize),
		.zPtr = _lastZ,
		.size = int(_horizontalSize),
		.x = int(scenePos().x()),
		.y = int(scenePos().y()),
		.flipped = flipped(),
		.rotation = int(rotation()),
		.imageSize = _imageSize,
		.maxSizeRatio = _maxSizeRatio,
		.contentMargins = _contentMargins,
	};
}

ItemBase::Placement ItemBase::placement() const {
	return {
		.position = pos(),
		.rotation = rotation(),
		.scale = scale(),
		.zValue = zValue(),
		.size = _horizontalSize,
		.aspectRatio = _aspectRatio,
		.flipped = _flipped,
	};
}

void ItemBase::applyPlacement(const Placement &placement) {
	prepareGeometryChange();
	_horizontalSize = placement.size;
	_aspectRatio = placement.aspectRatio;
	updateVerticalSize();
	setPos(placement.position);
	setRotation(placement.rotation);
	setScale(placement.scale);
	setZValue(placement.zValue);
	setFlip(placement.flipped);
	update();
}

void ItemBase::applyData(const Data &data) {
	// _lastZ is const.
	// _imageSize is const.
	_horizontalSize = data.size;
	setPos(data.x, data.y);
	setZValue((*_lastZ)++);
	setFlip(data.flipped);
	setRotation(data.rotation);
	updateZoom(data.initialZoom);
	update();
}

void ItemBase::save(SaveState state) {
	const auto z = zValue();
	auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
	saved = {
		.data = generateData(),
		.zValue = z,
		.status = status(),
	};
}

void ItemBase::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	const auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
	applyData(saved.data);
	if (const auto owner = static_cast<Scene*>(scene())) {
		updateZoom(owner->currentZoom());
	}
	setZValue(saved.zValue);
	setStatus(saved.status);
}

bool ItemBase::hasState(SaveState state) const {
	const auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
	return saved.zValue;
}

} // namespace Editor
