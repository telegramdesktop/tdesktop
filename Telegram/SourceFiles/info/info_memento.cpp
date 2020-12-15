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
//#include "info/channels/info_channels_widget.h" // #feed
#include "info/common_groups/info_common_groups_widget.h"
//#include "info/feed/info_feed_profile_widget.h" // #feed
#include "info/settings/info_settings_widget.h"
#include "info/polls/info_polls_results_widget.h"
#include "info/info_section_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_controller.h"
#include "ui/ui_utility.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Info {

Memento::Memento(not_null<PeerData*> peer)
: Memento(peer, Section::Type::Profile) {
}

Memento::Memento(not_null<PeerData*> peer, Section section)
: Memento(DefaultStack(peer, section)) {
}

//Memento::Memento(not_null<Data::Feed*> feed, Section section) // #feed
//: Memento(DefaultStack(feed, section)) {
//}
//
Memento::Memento(Settings::Tag settings, Section section)
: Memento(DefaultStack(settings, section)) {
}

Memento::Memento(not_null<PollData*> poll, FullMsgId contextId)
: Memento(DefaultStack(poll, contextId)) {
}

Memento::Memento(std::vector<std::shared_ptr<ContentMemento>> stack)
: _stack(std::move(stack)) {
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<PeerData*> peer,
		Section section) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(DefaultContent(peer, section));
	return result;
}

//std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack( // #feed
//		not_null<Data::Feed*> feed,
//		Section section) {
//	auto result = std::vector<std::shared_ptr<ContentMemento>>();
//	result.push_back(DefaultContent(feed, section));
//	return result;
//}
//
std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		Settings::Tag settings,
		Section section) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(std::make_shared<Settings::Memento>(
		settings.self,
		section.settingsType()));
	return result;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::DefaultStack(
		not_null<PollData*> poll,
		FullMsgId contextId) {
	auto result = std::vector<std::shared_ptr<ContentMemento>>();
	result.push_back(std::make_shared<Polls::Memento>(poll, contextId));
	return result;
}

Section Memento::DefaultSection(not_null<PeerData*> peer) {
	if (peer->sharedMediaInfo()) {
		return Section(Section::MediaType::Photo);
	}
	return Section(Section::Type::Profile);
}
// // #feed
//Section Memento::DefaultSection(Dialogs::Key key) {
//	if (const auto peer = key.peer()) {
//		if (peer->sharedMediaInfo()) {
//			return Section(Section::MediaType::Photo);
//		}
//	}
//	return Section(Section::Type::Profile);
//}

std::shared_ptr<Memento> Memento::Default(not_null<PeerData*> peer) {
	return std::make_shared<Memento>(peer, DefaultSection(peer));
}
// // #feed
//Memento Memento::Default(Dialogs::Key key) {
//	if (const auto peer = key.peer()) {
//		return Memento(peer->id, DefaultSection(key));
//	}
//	return Memento(key.feed(), DefaultSection(key));
//}

std::shared_ptr<ContentMemento> Memento::DefaultContent(
		not_null<PeerData*> peer,
		Section section) {
	if (auto to = peer->migrateTo()) {
		peer = to;
	}
	auto migrated = peer->migrateFrom();
	auto migratedPeerId = migrated ? migrated->id : PeerId(0);

	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_shared<Profile::Memento>(
			peer,
			migratedPeerId);
	case Section::Type::Media:
		return std::make_shared<Media::Memento>(
			peer,
			migratedPeerId,
			section.mediaType());
	case Section::Type::CommonGroups:
		return std::make_shared<CommonGroups::Memento>(peer->asUser());
	case Section::Type::Members:
		return std::make_shared<Members::Memento>(
			peer,
			migratedPeerId);
	}
	Unexpected("Wrong section type in Info::Memento::DefaultContent()");
}
//
//std::shared_ptr<ContentMemento> Memento::DefaultContent( // #feed
//		not_null<Data::Feed*> feed,
//		Section section) {
//	switch (section.type()) {
//	case Section::Type::Profile:
//		return std::make_shared<FeedProfile::Memento>(feed);
//	case Section::Type::Channels:
//		return std::make_shared<Channels::Memento>(feed);
//	}
//	Unexpected("Wrong feed section in Info::Memento::DefaultContent()");
//}

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
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
	return result;
}

object_ptr<Ui::LayerWidget> Memento::createLayer(
		not_null<Window::SessionController*> controller,
		const QRect &geometry) {
	if (geometry.width() >= LayerWidget::MinimalSupportedWidth()) {
		return object_ptr<LayerWidget>(controller, this);
	}
	return nullptr;
}

std::vector<std::shared_ptr<ContentMemento>> Memento::takeStack() {
	return std::move(_stack);
}

Memento::~Memento() = default;

MoveMemento::MoveMemento(object_ptr<WrapWidget> content)
: _content(std::move(content)) {
	_content->hide();
	_content->setParent(nullptr);
}

object_ptr<Window::SectionWidget> MoveMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
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
	return result;
}

object_ptr<Ui::LayerWidget> MoveMemento::createLayer(
		not_null<Window::SessionController*> controller,
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
