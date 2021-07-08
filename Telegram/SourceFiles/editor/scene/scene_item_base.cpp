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
#include "styles/style_editor.h"

#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>

namespace Editor {
namespace {

constexpr auto kSnapAngle = 45.;

const auto kDuplicateSequence = QKeySequence("ctrl+d");
const auto kFlipSequence = QKeySequence("ctrl+s");
const auto kDeleteSequence = QKeySequence("delete");

constexpr auto kMinSizeRatio = 0.05;
constexpr auto kMaxSizeRatio = 1.00;

auto Normalized(float64 angle) {
	return angle
		+ ((std::abs(angle) < 360) ? 0 : (-360 * (angle < 0 ? -1 : 1)));
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

ItemBase::ItemBase(Data data)
: _lastZ(data.zPtr)
, _imageSize(data.imageSize)
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
	return innerRect() - _scaledInnerMargins;
}

QRectF ItemBase::innerRect() const {
	const auto &hSize = _horizontalSize;
	const auto &vSize = _verticalSize;
	return QRectF(-hSize / 2, -vSize / 2, hSize, vSize);
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

	p->setPen(hasFocus ? _pens.handle : _pens.handleInactive);
	p->setBrush(st::photoEditorItemBaseHandleFg);
	p->drawEllipse(rightHandleRect());
	p->drawEllipse(leftHandleRect());
}

void ItemBase::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
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
			? (std::round(angle / kSnapAngle) * kSnapAngle) // Snap rotation.
			: angle);
	} else {
		QGraphicsItem::mouseMoveEvent(event);
	}
}

void ItemBase::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
	setCursor(isHandling()
		? Qt::ClosedHandCursor
		: (handleType(event->pos()) != HandleType::None) && isSelected()
		? Qt::OpenHandCursor
		: Qt::ArrowCursor);
	QGraphicsItem::hoverMoveEvent(event);
}

void ItemBase::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	setZValue((*_lastZ)++);
	if (event->button() == Qt::LeftButton) {
		_handle = handleType(event->pos());
	}
	if (isHandling()) {
		setCursor(Qt::ClosedHandCursor);
	} else {
		QGraphicsItem::mousePressEvent(event);
	}
}

void ItemBase::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	if ((event->button() == Qt::LeftButton) && isHandling()) {
		_handle = HandleType::None;
	} else {
		QGraphicsItem::mouseReleaseEvent(event);
	}
}

void ItemBase::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
	if (scene()) {
		scene()->clearSelection();
		setSelected(true);
	}

	const auto add = [&](
			auto base,
			const QKeySequence &sequence,
			Fn<void()> callback) {
		// TODO: refactor.
		const auto sequenceText = QChar('\t')
			+ sequence.toString(QKeySequence::NativeText);
		_menu->addAction(base(tr::now) + sequenceText, std::move(callback));
	};

	_menu = base::make_unique_q<Ui::PopupMenu>(nullptr);
	add(
		tr::lng_photo_editor_menu_delete,
		kDeleteSequence,
		[=] { actionDelete(); });
	add(
		tr::lng_photo_editor_menu_flip,
		kFlipSequence,
		[=] { actionFlip(); });
	add(
		tr::lng_photo_editor_menu_duplicate,
		kDuplicateSequence,
		[=] { actionDuplicate(); });

	_menu->popup(event->screenPos());
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

void ItemBase::actionFlip() {
	setFlip(!flipped());
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

void ItemBase::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
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
	} else if (matches(kDeleteSequence)) {
		performForSelectedItems(&ItemBase::actionDelete);
	} else if (matches(kFlipSequence)) {
		performForSelectedItems(&ItemBase::actionFlip);
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

float64 ItemBase::size() const {
	return _horizontalSize;
}

void ItemBase::updateVerticalSize() {
	const auto verticalSize = _horizontalSize * _aspectRatio;
	_verticalSize = std::max(
		verticalSize,
		float64(_sizeLimits.min));
	if (verticalSize < _sizeLimits.min) {
		_horizontalSize = _verticalSize / _aspectRatio;
	}
}

void ItemBase::setAspectRatio(float64 aspectRatio) {
	_aspectRatio = aspectRatio;
	updateVerticalSize();
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
	_scaledInnerMargins = QMarginsF(
		_scaledHandleSize,
		_scaledHandleSize,
		_scaledHandleSize,
		_scaledHandleSize) * 0.5;

	const auto maxSide = std::max(
		_imageSize.width(),
		_imageSize.height());
	_sizeLimits = {
		.min = int(maxSide * kMinSizeRatio),
		.max = int(maxSide * kMaxSizeRatio),
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
	};
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
	setZValue(saved.zValue);
	setStatus(saved.status);
}

bool ItemBase::hasState(SaveState state) const {
	const auto &saved = (state == SaveState::Keep) ? _keeped : _saved;
	return saved.zValue;
}

} // namespace Editor
