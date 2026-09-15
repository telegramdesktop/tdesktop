/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_message.h"

#include "editor/editor_message_render.h"
#include "editor/editor_message_source.h"
#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QCoreApplication>
#include <QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

constexpr auto kMaxRatio = 8;

[[nodiscard]] bool OnMainThread() {
	return QThread::currentThread() == QCoreApplication::instance()->thread();
}

[[nodiscard]] int RatioFor(float64 width, int logicalWidth) {
	return std::clamp(int(std::ceil(width / logicalWidth)), 1, kMaxRatio);
}

} // namespace

ItemMessage::ItemMessage(
	std::shared_ptr<MessageSource> source,
	std::unique_ptr<MessageRenderer> renderer,
	ItemBase::Data data)
: ItemBase(std::move(data))
, _source(std::move(source))
, _renderer(std::move(renderer)) {
	attachRenderer();
}

ItemMessage::~ItemMessage() = default;

void ItemMessage::attachRenderer() {
	_renderer->setRepaintCallback([=] { scheduleRefresh(); });
	_image = _renderer->render(1);
	_ratio = _image.isNull() ? 0 : 1;
	updateSize();
}

void ItemMessage::scheduleRefresh() {
	if (_refreshScheduled) {
		return;
	}
	_refreshScheduled = true;
	crl::on_main(crl::guard(this, [=] {
		_refreshScheduled = false;
		refresh();
	}));
}

void ItemMessage::refresh() {
	if (!_renderer->ready()) {
		return;
	}
	auto image = _renderer->render(std::max(_ratio, 1));
	if (image.isNull()) {
		return;
	}
	_image = std::move(image);
	_ratio = std::max(_ratio, 1);
	updateSize();
	update();
}

void ItemMessage::updateSize() {
	const auto size = _renderer->size();
	if (size.isEmpty() || (size == _size)) {
		return;
	}
	_size = size;
	setAspectRatio(_size.height() / float64(_size.width()));
}

int ItemMessage::neededRatio(not_null<QPainter*> p) const {
	if (_size.isEmpty()) {
		return 1;
	}
	const auto &transform = p->worldTransform();
	const auto device = p->device();
	const auto scale = std::hypot(transform.m11(), transform.m12())
		* (device ? device->devicePixelRatio() : 1.);
	return RatioFor(visibleRect().width() * scale, _size.width());
}

void ItemMessage::ensureRatio(int ratio) {
	if (ratio <= _ratio || !_renderer->ready() || !OnMainThread()) {
		return;
	}
	auto image = _renderer->render(ratio);
	if (!image.isNull()) {
		_image = std::move(image);
		_ratio = ratio;
	}
}

void ItemMessage::save(SaveState state) {
	ItemBase::save(state);
	if (!_size.isEmpty()) {
		ensureRatio(RatioFor(visibleRect().width(), _size.width()));
	}
}

void ItemMessage::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	ensureRatio(neededRatio(p));
	if (!_image.isNull()) {
		p->setRenderHint(QPainter::SmoothPixmapTransform);
		p->drawImage(visibleRect(), _image);
	}
	ItemBase::paint(p, option, w);
}

int ItemMessage::type() const {
	return Type;
}

const std::shared_ptr<MessageSource> &ItemMessage::source() const {
	return _source;
}

void ItemMessage::setSource(std::shared_ptr<MessageSource> source) {
	_source = std::move(source);
	_renderer = std::make_unique<MessageRenderer>(_source);
	_ratio = 0;
	attachRenderer();
	update();
}

void ItemMessage::setEditCallback(EditCallback callback) {
	_edit = std::move(callback);
}

bool ItemMessage::editable() const {
	return _edit && _source->link().has_value();
}

QRectF ItemMessage::visibleRect() const {
	return fittedRect(_size);
}

bool ItemMessage::flippable() const {
	return false;
}

void ItemMessage::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	if (!editable()) {
		return;
	}
	menu->addAction(
		tr::lng_menu_formatting_link_edit(tr::now),
		[=] { _edit(this); },
		&st::mediaMenuIconEdit);
}

void ItemMessage::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	if (editable()) {
		_edit(this);
	} else {
		ItemBase::mouseDoubleClickEvent(event);
	}
}

std::shared_ptr<ItemBase> ItemMessage::duplicate(ItemBase::Data data) const {
	auto result = std::make_shared<ItemMessage>(
		_source,
		std::make_unique<MessageRenderer>(_source),
		std::move(data));
	result->_edit = _edit;
	return result;
}

} // namespace Editor
