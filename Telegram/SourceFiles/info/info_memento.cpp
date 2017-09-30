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
#include "info/info_memento.h"

#include "info/profile/info_profile_widget.h"
#include "info/media/info_media_widget.h"
#include "info/info_common_groups_widget.h"
#include "info/info_section_widget.h"
#include "info/info_layer_widget.h"

namespace Info {

Memento::Memento(PeerId peerId)
: Memento(peerId, Section::Type::Profile) {
}

Memento::Memento(PeerId peerId, Section section)
: Memento(peerId, section, Default(peerId, section)) {
}

Memento::Memento(
	PeerId peerId,
	Section section,
	std::unique_ptr<ContentMemento> content)
: _peerId(peerId)
, _section(section)
, _content(std::move(content)) {
}

std::unique_ptr<ContentMemento> Memento::Default(
		PeerId peerId,
		Section section) {
	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_unique<Profile::Memento>(peerId);
	case Section::Type::Media:
		return std::make_unique<Media::Memento>(
			peerId,
			section.mediaType());
	case Section::Type::CommonGroups:
		Assert(peerIsUser(peerId));
		return std::make_unique<CommonGroups::Memento>(
			peerToUser(peerId));
	}
	Unexpected("Wrong section type in Info::Memento::Default()");
}

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto wrap = (column == Window::Column::Third)
		? Wrap::Side
		: Wrap::Narrow;
	auto result = object_ptr<SectionWidget>(
		parent,
		controller,
		wrap,
		this);
	result->setGeometry(geometry);
	return std::move(result);
}

object_ptr<Window::LayerWidget> Memento::createLayer(
		not_null<Window::Controller*> controller,
		const QRect &geometry) {
	if (geometry.width() >= LayerWidget::MinimalSupportedWidth()) {
		return object_ptr<LayerWidget>(controller, this);
	}
	return nullptr;
}

void Memento::setInner(std::unique_ptr<ContentMemento> content) {
	_content = std::move(content);
}

Memento::~Memento() = default;

MoveMemento::MoveMemento(object_ptr<WrapWidget> content)
: _content(std::move(content)) {
}

object_ptr<Window::SectionWidget> MoveMemento::createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto wrap = (column == Window::Column::Third)
		? Wrap::Side
		: Wrap::Narrow;
	auto result = object_ptr<SectionWidget>(
		parent,
		controller,
		wrap,
		this);
	result->setGeometry(geometry);
	return std::move(result);
}

object_ptr<Window::LayerWidget> MoveMemento::createLayer(
		not_null<Window::Controller*> controller,
		const QRect &geometry) {
	if (geometry.width() < LayerWidget::MinimalSupportedWidth()) {
		return nullptr;
	}
	return object_ptr<LayerWidget>(controller, this);
}

object_ptr<WrapWidget> MoveMemento::takeContent(
		QWidget *parent,
		Wrap wrap) {
	Ui::AttachParentChild(parent, _content);
	_content->setWrap(wrap);
	return std::move(_content);
}

} // namespace Info
