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
#include "info/profile/info_profile_members.h"
#include "info/media/info_media_widget.h"
#include "info/members/info_members_widget.h"
#include "info/common_groups/info_common_groups_widget.h"
#include "info/info_section_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_controller.h"
#include "boxes/peer_list_box.h"

namespace Info {

Memento::Memento(PeerId peerId)
: Memento(peerId, Section::Type::Profile) {
}

Memento::Memento(PeerId peerId, Section section)
: Memento(DefaultStack(peerId, section)) {
}

Memento::Memento(std::vector<std::unique_ptr<ContentMemento>> stack)
: _stack(std::move(stack)) {
}

std::vector<std::unique_ptr<ContentMemento>> Memento::DefaultStack(
		PeerId peerId,
		Section section) {
	auto result = std::vector<std::unique_ptr<ContentMemento>>();
	result.push_back(DefaultContent(peerId, section));
	return result;
}

Section Memento::DefaultSection(not_null<PeerData*> peer) {
	return peer->isSelf()
		? Section(Section::MediaType::Photo)
		: Section(Section::Type::Profile);
}

Memento Memento::Default(not_null<PeerData*> peer) {
	return Memento(peer->id, DefaultSection(peer));
}

std::unique_ptr<ContentMemento> Memento::DefaultContent(
		PeerId peerId,
		Section section) {
	Expects(peerId != 0);

	auto peer = App::peer(peerId);
	if (auto to = peer->migrateTo()) {
		peer = to;
	}
	auto migrated = peer->migrateFrom();
	peerId = peer->id;
	auto migratedPeerId = migrated ? migrated->id : PeerId(0);

	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_unique<Profile::Memento>(
			peerId,
			migratedPeerId);
	case Section::Type::Media:
		return std::make_unique<Media::Memento>(
			peerId,
			migratedPeerId,
			section.mediaType());
	case Section::Type::CommonGroups:
		Assert(peerIsUser(peerId));
		return std::make_unique<CommonGroups::Memento>(
			peerToUser(peerId));
	case Section::Type::Members:
		return std::make_unique<Members::Memento>(
			peerId,
			migratedPeerId);
	}
	Unexpected("Wrong section type in Info::Memento::DefaultContent()");
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

std::vector<std::unique_ptr<ContentMemento>> Memento::takeStack() {
	return std::move(_stack);
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
