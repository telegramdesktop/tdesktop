/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/media/info_media_widget.h"

#include "info/media/info_media_inner_widget.h"
#include "info/info_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/search_field_controller.h"
#include "styles/style_info.h"

namespace Info {
namespace Media {

base::optional<int> TypeToTabIndex(Type type) {
	switch (type) {
	case Type::Photo: return 0;
	case Type::Video: return 1;
	case Type::File: return 2;
	}
	return base::none;
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
	controller->peerId(),
	controller->migratedPeerId(),
	controller->section().mediaType()) {
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
	return std::move(result);
}

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller));
	_inner->scrollToRequests()
		| rpl::start_with_next([this](int skip) {
			scrollTo({ skip, -1 });
		}, _inner->lifetime());
	controller->wrapValue()
		| rpl::start_with_next([this] {
			refreshSearchField();
		}, lifetime());
}

rpl::producer<SelectedItems> Widget::selectedListValue() const {
	return _inner->selectedListValue();
}

void Widget::cancelSelection() {
	_inner->cancelSelection();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto mediaMemento = dynamic_cast<Memento*>(memento.get())) {
		if (_inner->showInternal(mediaMemento)) {
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, not_null<Memento*> memento) {
	setGeometry(geometry);
	myEnsureResized(this);
	restoreState(memento);
}

std::unique_ptr<ContentMemento> Widget::createMemento() {
	auto result = std::make_unique<Memento>(controller());
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
}

void Widget::refreshSearchField() {
	auto search = controller()->searchFieldController();
	if (search && controller()->wrap() == Wrap::Layer) {
		_searchField = search->createRowView(
			this,
			st::infoLayerMediaSearch);
		auto field = _searchField.get();
		widthValue()
			| rpl::start_with_next([field](int newWidth) {
				field->resizeToWidth(newWidth);
				field->moveToLeft(0, 0);
			}, field->lifetime());
		field->show();
		setScrollTopSkip(field->heightNoMargins() - st::lineWidth);
	} else {
		_searchField = nullptr;
		setScrollTopSkip(0);
	}
}

} // namespace Media
} // namespace Info
