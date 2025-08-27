/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_widget.h"

#include "history/history.h"
#include "info/media/info_media_inner_widget.h"
#include "info/info_controller.h"
#include "main/main_session.h"
#include "ui/widgets/scroll_area.h"
#include "ui/search_field_controller.h"
#include "ui/ui_utility.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_sublist.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"

namespace Info::Media {

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

tr::phrase<> SharedMediaTitle(Type type) {
	switch (type) {
	case Type::Photo:
		return tr::lng_media_type_photos;
	case Type::GIF:
		return tr::lng_media_type_gifs;
	case Type::Video:
		return tr::lng_media_type_videos;
	case Type::MusicFile:
		return tr::lng_media_type_songs;
	case Type::File:
		return tr::lng_media_type_files;
	case Type::RoundVoiceFile:
		return tr::lng_media_type_audios;
	case Type::Link:
		return tr::lng_media_type_links;
	case Type::RoundFile:
		return tr::lng_media_type_rounds;
	}
	Unexpected("Bad media type in Info::TitleValue()");
}

Memento::Memento(not_null<Controller*> controller)
: Memento(
	(controller->peer()
		? controller->peer()
		: controller->storiesPeer()
		? controller->storiesPeer()
		: controller->parentController()->session().user()),
	controller->topic(),
	controller->sublist(),
	controller->migratedPeerId(),
	(controller->section().type() == Section::Type::Downloads
		? Type::File
		: controller->section().type() == Section::Type::Stories
		? Type::PhotoVideo
		: controller->section().mediaType())) {
}

Memento::Memento(not_null<PeerData*> peer, PeerId migratedPeerId, Type type)
: Memento(peer, nullptr, nullptr, migratedPeerId, type) {
}

Memento::Memento(not_null<Data::ForumTopic*> topic, Type type)
: Memento(topic->channel(), topic, nullptr, PeerId(), type) {
}

Memento::Memento(not_null<Data::SavedSublist*> sublist, Type type)
: Memento(sublist->owningHistory()->peer, nullptr, sublist, PeerId(), type) {
}

Memento::Memento(
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	PeerId migratedPeerId,
	Type type)
: ContentMemento(peer, topic, sublist, migratedPeerId)
, _type(type) {
	_searchState.query.type = type;
	_searchState.query.peerId = peer->id;
	_searchState.query.topicRootId = topic ? topic->rootId() : MsgId();
	_searchState.query.monoforumPeerId = sublist
		? sublist->sublistPeer()->id
		: PeerId();
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

Widget::Widget(QWidget *parent, not_null<Controller*> controller)
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

void Widget::selectionAction(SelectionAction action) {
	_inner->selectionAction(action);
}

rpl::producer<QString> Widget::title() {
	if (controller()->key().peer()->sharedMediaInfo() && isStackBottom()) {
		return tr::lng_profile_shared_media();
	}
	return SharedMediaTitle(controller()->section().mediaType())();
}

void Widget::setIsStackBottom(bool isStackBottom) {
	ContentWidget::setIsStackBottom(isStackBottom);
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

} // namespace Info::Media
