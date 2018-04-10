/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_memento.h"

#include "info/profile/info_profile_widget.h"
#include "info/media/info_media_widget.h"
#include "info/members/info_members_widget.h"
#include "info/channels/info_channels_widget.h"
#include "info/common_groups/info_common_groups_widget.h"
#include "info/feed/info_feed_profile_widget.h"
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

Memento::Memento(not_null<Data::Feed*> feed, Section section)
: Memento(DefaultStack(feed, section)) {
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

std::vector<std::unique_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<Data::Feed*> feed,
		Section section) {
	auto result = std::vector<std::unique_ptr<ContentMemento>>();
	result.push_back(DefaultContent(feed, section));
	return result;
}

Section Memento::DefaultSection(Dialogs::Key key) {
	if (const auto peer = key.peer()) {
		if (peer->isSelf()) {
			return Section(Section::MediaType::Photo);
		}
	}
	return Section(Section::Type::Profile);
}

Memento Memento::Default(Dialogs::Key key) {
	if (const auto peer = key.peer()) {
		return Memento(peer->id, DefaultSection(key));
	}
	return Memento(key.feed(), DefaultSection(key));
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

std::unique_ptr<ContentMemento> Memento::DefaultContent(
		not_null<Data::Feed*> feed,
		Section section) {
	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_unique<FeedProfile::Memento>(feed);
	case Section::Type::Channels:
		return std::make_unique<Channels::Memento>(feed);
	}
	Unexpected("Wrong feed section in Info::Memento::DefaultContent()");
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
