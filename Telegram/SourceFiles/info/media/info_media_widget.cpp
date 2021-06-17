/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_widget.h"

#include "info/media/info_media_inner_widget.h"
#include "info/info_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/search_field_controller.h"
#include "ui/ui_utility.h"
#include "data/data_peer.h"
#include "styles/style_info.h"

namespace Info {
namespace Media {

std::optional<int> TypeToTabIndex(Type type) {
	switch (type) {
	case Type::Photo: return 0;
	case Type::Video: return 1;
	case Type::File: return 2;
	}
	return std::nullopt;
}

Type TabIndexToType(int index) {
	switch (index) {
	case 0: return Type::Photo;
	case 1: return Type::Video;
	case 2: return Type::File;
	}
	Unexpected("Index in Info::Media::TabIndexToType()");
}

Memento::Memento(not_null<Controller*> controller)
: Memento(
	controller->peer(),
	controller->migratedPeerId(),
	controller->section().mediaType()) {
}

Memento::Memento(not_null<PeerData*> peer, PeerId migratedPeerId, Type type)
: ContentMemento(peer, migratedPeerId)
, _type(type) {
	_searchState.query.type = type;
	_searchState.query.peerId = peer->id;
	_searchState.query.migratedPeerId = migratedPeerId;
	if (migratedPeerId) {
		_searchState.migratedList = Storage::SparseIdsList();
	}
}

Section Memento::section() const {
	return Section(_type);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(
		parent,
		controller);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller));
	_inner->setScrollHeightValue(scrollHeightValue());
	_inner->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		scrollTo(request);
	}, _inner->lifetime());
}

rpl::producer<SelectedItems> Widget::selectedListValue() const {
	return _inner->selectedListValue();
}

void Widget::cancelSelection() {
	_inner->cancelSelection();
}

void Widget::setIsStackBottom(bool isStackBottom) {
	_inner->setIsStackBottom(isStackBottom);
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (const auto mediaMemento = dynamic_cast<Memento*>(memento.get())) {
		if (_inner->showInternal(mediaMemento)) {
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
}

} // namespace Media
} // namespace Info
