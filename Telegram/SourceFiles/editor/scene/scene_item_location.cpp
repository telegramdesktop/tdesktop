/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_location.h"

#include "editor/scene/scene.h"
#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_editor.h"

#include <QtWidgets/QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

constexpr auto kMaxWidthRatio = 0.9;

[[nodiscard]] QString CoordinatePart(float64 value, QChar positive, QChar negative) {
	const auto hemisphere = (value < 0) ? negative : positive;
	value = std::abs(value);
	auto degrees = int(std::floor(value));
	const auto minutesFull = (value - degrees) * 60.;
	auto minutes = int(std::floor(minutesFull));
	auto seconds = int(std::round((minutesFull - minutes) * 60.));
	if (seconds == 60) {
		seconds = 0;
		++minutes;
	}
	if (minutes == 60) {
		minutes = 0;
		++degrees;
	}
	return u"%1°%2'%3\"%4"_q
		.arg(degrees)
		.arg(minutes)
		.arg(seconds)
		.arg(hemisphere);
}

[[nodiscard]] QString LocationText(const LocationData &location) {
	return location.title.isEmpty()
		? (CoordinatePart(location.lat, 'N', 'S')
			+ u" "_q
			+ CoordinatePart(location.lon, 'E', 'W'))
		: location.title.toUpper();
}

} // namespace

ItemLocation::ItemLocation(LocationData location, ItemBase::Data data)
: ItemBase(data)
, _location(std::move(location))
, _imageSize(data.imageSize)
, _pill(MakePill(_location, _imageSize)) {
	_pill.setStyle(LinkStyle::SemiTransparent);
	setAspectRatio(_pill.size().height() / _pill.size().width());
}

LinkPill ItemLocation::MakePill(
		const LocationData &location,
		QSize imageSize) {
	return LinkPill(
		LinkPreview{ .name = LocationText(location) },
		LinkPill::DensityFor(imageSize.width()),
		int(imageSize.width() * kMaxWidthRatio),
		&st::photoEditorLocationPillIcon);
}

int ItemLocation::type() const {
	return Type;
}

const LocationData &ItemLocation::location() const {
	return _location;
}

void ItemLocation::setLocation(LocationData location) {
	_location = std::move(location);
	const auto style = _pill.style();
	const auto scale = size() / _pill.size().width();
	_pill = MakePill(_location, _imageSize);
	_pill.setStyle(style);
	applyStretch(
		_pill.size().width() * scale,
		_pill.size().height() * scale,
		false);
	update();
}

void ItemLocation::setEditCallback(EditCallback callback) {
	_edit = std::move(callback);
}

QRectF ItemLocation::visibleRect() const {
	return fittedRect(_pill.size());
}

bool ItemLocation::flippable() const {
	return false;
}

void ItemLocation::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	const auto rect = visibleRect();
	_pill.paint(*p, rect.topLeft(), rect.width() / _pill.size().width());
	ItemBase::paint(p, option, w);
}

void ItemLocation::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	_wasSelected = isSelected() && (event->button() == Qt::LeftButton);
	_styleCycled = false;
	ItemBase::mousePressEvent(event);
}

void ItemLocation::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	const auto clicked = _wasSelected
		&& (event->button() == Qt::LeftButton)
		&& !isHandling()
		&& !dragThresholdPassed(event);
	ItemBase::mouseReleaseEvent(event);
	if (clicked) {
		_styleCycled = true;
		_pill.setStyle(LinkPill::NextStyle(_pill.style()));
		update();
	}
}

void ItemLocation::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	if (base::take(_styleCycled)) {
		_pill.setStyle(LinkPill::PreviousStyle(_pill.style()));
	}
	_wasSelected = false;
	if (_edit) {
		_edit(this);
	} else {
		ItemBase::mouseDoubleClickEvent(event);
	}
}

void ItemLocation::requestPick() {
	if (const auto s = static_cast<Scene*>(scene())) {
		s->requestLocationPick(this);
	}
}

void ItemLocation::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	menu->addAction(tr::lng_story_location_change(tr::now), [=] {
		if (_edit) {
			_edit(this);
		} else {
			requestPick();
		}
	});
}

std::shared_ptr<ItemBase> ItemLocation::duplicate(ItemBase::Data data) const {
	auto result = std::make_shared<ItemLocation>(_location, std::move(data));
	result->_pill.setStyle(_pill.style());
	result->_edit = _edit;
	return result;
}

} // namespace Editor
