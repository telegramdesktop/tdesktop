/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_controller.h"

#include "ui/search_field_controller.h"
#include "history/history.h"
#include "info/info_content_widget.h"
#include "info/info_memento.h"
#include "info/global_media/info_global_media_widget.h"
#include "info/media/info_media_widget.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "data/data_forum.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_shared_media.h"
#include "data/data_media_types.h"
#include "data/data_download_manager.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Info {

Key::Key(not_null<PeerData*> peer) : _value(peer) {
}

Key::Key(not_null<Data::ForumTopic*> topic) : _value(topic) {
}

Key::Key(not_null<Data::SavedSublist*> sublist) : _value(sublist) {
}

Key::Key(Settings::Tag settings) : _value(settings) {
}

Key::Key(Downloads::Tag downloads) : _value(downloads) {
}

Key::Key(Stories::Tag stories) : _value(stories) {
}

Key::Key(Statistics::Tag statistics) : _value(statistics) {
}

Key::Key(PeerGifts::Tag gifts) : _value(gifts) {
}

Key::Key(BotStarRef::Tag starref) : _value(starref) {
}

Key::Key(GlobalMedia::Tag global) : _value(global) {
}

Key::Key(not_null<PollData*> poll, FullMsgId contextId)
: _value(PollKey{ poll, contextId }) {
}

Key::Key(
	std::shared_ptr<Api::WhoReadList> whoReadIds,
	Data::ReactionId selected,
	FullMsgId contextId)
: _value(ReactionsKey{ whoReadIds, selected, contextId }) {
}

PeerData *Key::peer() const {
	if (const auto peer = std::get_if<not_null<PeerData*>>(&_value)) {
		return *peer;
	} else if (const auto topic = this->topic()) {
		return topic->channel();
	} else if (const auto sublist = this->sublist()) {
		return sublist->owningHistory()->peer;
	}
	return nullptr;
}

Data::ForumTopic *Key::topic() const {
	if (const auto topic = std::get_if<not_null<Data::ForumTopic*>>(
			&_value)) {
		return *topic;
	}
	return nullptr;
}

Data::SavedSublist *Key::sublist() const {
	if (const auto sublist = std::get_if<not_null<Data::SavedSublist*>>(
			&_value)) {
		return *sublist;
	}
	return nullptr;
}

UserData *Key::settingsSelf() const {
	if (const auto tag = std::get_if<Settings::Tag>(&_value)) {
		return tag->self;
	}
	return nullptr;
}

bool Key::isDownloads() const {
	return v::is<Downloads::Tag>(_value);
}

bool Key::isGlobalMedia() const {
	return v::is<GlobalMedia::Tag>(_value);
}

PeerData *Key::storiesPeer() const {
	if (const auto tag = std::get_if<Stories::Tag>(&_value)) {
		return tag->peer;
	}
	return nullptr;
}

int Key::storiesAlbumId() const {
	if (const auto tag = std::get_if<Stories::Tag>(&_value)) {
		return tag->albumId;
	}
	return 0;
}

int Key::storiesAddToAlbumId() const {
	if (const auto tag = std::get_if<Stories::Tag>(&_value)) {
		return tag->addingToAlbumId;
	}
	return 0;
}

PeerData *Key::giftsPeer() const {
	if (const auto tag = std::get_if<PeerGifts::Tag>(&_value)) {
		return tag->peer;
	}
	return nullptr;
}

int Key::giftsCollectionId() const {
	if (const auto tag = std::get_if<PeerGifts::Tag>(&_value)) {
		return tag->collectionId;
	}
	return 0;
}

Statistics::Tag Key::statisticsTag() const {
	if (const auto tag = std::get_if<Statistics::Tag>(&_value)) {
		return *tag;
	}
	return Statistics::Tag();
}

PeerData *Key::starrefPeer() const {
	if (const auto tag = std::get_if<BotStarRef::Tag>(&_value)) {
		return tag->peer;
	}
	return nullptr;
}

BotStarRef::Type Key::starrefType() const {
	if (const auto tag = std::get_if<BotStarRef::Tag>(&_value)) {
		return tag->type;
	}
	return BotStarRef::Type();
}

PollData *Key::poll() const {
	if (const auto data = std::get_if<PollKey>(&_value)) {
		return data->poll;
	}
	return nullptr;
}

FullMsgId Key::pollContextId() const {
	if (const auto data = std::get_if<PollKey>(&_value)) {
		return data->contextId;
	}
	return FullMsgId();
}

std::shared_ptr<Api::WhoReadList> Key::reactionsWhoReadIds() const {
	if (const auto data = std::get_if<ReactionsKey>(&_value)) {
		return data->whoReadIds;
	}
	return nullptr;
}

Data::ReactionId Key::reactionsSelected() const {
	if (const auto data = std::get_if<ReactionsKey>(&_value)) {
		return data->selected;
	}
	return Data::ReactionId();
}

FullMsgId Key::reactionsContextId() const {
	if (const auto data = std::get_if<ReactionsKey>(&_value)) {
		return data->contextId;
	}
	return FullMsgId();
}

rpl::producer<SparseIdsMergedSlice> AbstractController::mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const {
	Expects(peer() != nullptr);

	const auto isScheduled = [&] {
		const auto peerId = peer()->id;
		if (const auto item = session().data().message(peerId, aroundId)) {
			return item->isScheduled();
		}
		return false;
	}();

	const auto mediaViewer = isScheduled
		? SharedScheduledMediaViewer
		: SharedMediaMergedViewer;
	const auto topicId = isScheduled
		? SparseIdsMergedSlice::kScheduledTopicId
		: topic()
		? topic()->rootId()
		: MsgId(0);

	return mediaViewer(
		&session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				peer()->id,
				topicId,
				sublist() ? sublist()->sublistPeer()->id : PeerId(),
				migratedPeerId(),
				aroundId),
			section().mediaType()),
		limitBefore,
		limitAfter);
}

rpl::producer<QString> AbstractController::mediaSourceQueryValue() const {
	return rpl::single(QString());
}

rpl::producer<QString> AbstractController::searchQueryValue() const {
	return rpl::single(QString());
}

AbstractController::AbstractController(
	not_null<Window::SessionController*> parent)
: SessionNavigation(&parent->session())
, _parent(parent) {
}

PeerData *AbstractController::peer() const {
	return key().peer();
}

PeerId AbstractController::migratedPeerId() const {
	if (const auto peer = migrated()) {
		return peer->id;
	}
	return PeerId(0);
}

PollData *AbstractController::poll() const {
	if (const auto item = session().data().message(pollContextId())) {
		if (const auto media = item->media()) {
			return media->poll();
		}
	}
	return nullptr;
}

auto AbstractController::reactionsWhoReadIds() const
-> std::shared_ptr<Api::WhoReadList> {
	return key().reactionsWhoReadIds();
}

Data::ReactionId AbstractController::reactionsSelected() const {
	return key().reactionsSelected();
}

FullMsgId AbstractController::reactionsContextId() const {
	return key().reactionsContextId();
}

void AbstractController::showSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const Window::SectionShow &params) {
	return parentController()->showSection(std::move(memento), params);
}

void AbstractController::showBackFromStack(
		const Window::SectionShow &params) {
	return parentController()->showBackFromStack(params);
}

void AbstractController::showPeerHistory(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId msgId) {
	return parentController()->showPeerHistory(peerId, params, msgId);
}

Controller::Controller(
	not_null<WrapWidget*> widget,
	not_null<Window::SessionController*> window,
	not_null<ContentMemento*> memento)
: AbstractController(window)
, _widget(widget)
, _key(memento->key())
, _migrated(memento->migratedPeerId()
	? window->session().data().peer(memento->migratedPeerId()).get()
	: nullptr)
, _section(memento->section()) {
	updateSearchControllers(memento);
	setupMigrationViewer();
	setupTopicViewer();
}

void Controller::replaceKey(Key key) {
	_key = key;
}

void Controller::setupMigrationViewer() {
	const auto peer = _key.peer();
	if (_key.topic()
		|| !peer
		|| (!peer->isChat() && !peer->isChannel())
		|| _migrated) {
		return;
	}
	peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::Migration
	) | rpl::filter([=] {
		return peer->migrateTo() || (peer->migrateFrom() != _migrated);
	}) | rpl::start_with_next([=] {
		replaceWith(std::make_shared<Memento>(peer, _section));
	}, lifetime());
}

void Controller::replaceWith(std::shared_ptr<Memento> memento) {
	const auto window = parentController();
	auto params = Window::SectionShow(
		Window::SectionShow::Way::Backward,
		anim::type::instant,
		anim::activation::background);
	if (wrap() == Wrap::Side) {
		params.thirdColumn = true;
	}
	InvokeQueued(_widget, [=, memento = std::move(memento)]() mutable {
		window->showSection(std::move(memento), params);
	});
}

void Controller::setupTopicViewer() {
	session().data().itemIdChanged(
	) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
		if (const auto topic = _key.topic()) {
			if (topic->rootId() == change.oldId
				|| (topic->peer()->id == change.newId.peer
					&& topic->rootId() == change.newId.msg)) {
				const auto now = topic->forum()->topicFor(change.newId.msg);
				_key = Key(now);
				replaceWith(std::make_shared<Memento>(now, _section));
			}
		}
	}, _lifetime);
}

Wrap Controller::wrap() const {
	return _widget->wrap();
}

rpl::producer<Wrap> Controller::wrapValue() const {
	return _widget->wrapValue();
}

not_null<Ui::RpWidget*> Controller::wrapWidget() const {
	return _widget;
}

bool Controller::validateMementoPeer(
		not_null<ContentMemento*> memento) const {
	return memento->peer() == peer()
		&& memento->migratedPeerId() == migratedPeerId()
		&& memento->settingsSelf() == settingsSelf()
		&& memento->storiesPeer() == storiesPeer()
		&& memento->statisticsTag().peer == statisticsTag().peer
		&& memento->starrefPeer() == starrefPeer()
		&& memento->starrefType() == starrefType();
}

void Controller::setSection(not_null<ContentMemento*> memento) {
	_section = memento->section();
	updateSearchControllers(memento);
}

bool Controller::hasBackButton() const {
	return _widget->hasBackButton();
}

void Controller::updateSearchControllers(
		not_null<ContentMemento*> memento) {
	using Type = Section::Type;
	const auto type = _section.type();
	const auto isMedia = (type == Type::Media)
		|| (type == Type::GlobalMedia);
	const auto mediaType = isMedia
		? _section.mediaType()
		: Section::MediaType::kCount;
	const auto hasMediaSearch = isMedia
		&& SharedMediaAllowSearch(mediaType);
	const auto hasRequestsListSearch = (type == Type::RequestsList);
	const auto hasCommonGroupsSearch = (type == Type::CommonGroups);
	const auto hasDownloadsSearch = (type == Type::Downloads);
	const auto hasMembersSearch = (type == Type::Members)
		|| (type == Type::Profile);
	const auto searchQuery = memento->searchFieldQuery();
	if (type == Type::Media) {
		_searchController
			= std::make_unique<Api::DelayedSearchController>(&session());
		auto mediaMemento = dynamic_cast<Media::Memento*>(memento.get());
		Assert(mediaMemento != nullptr);
		_searchController->restoreState(mediaMemento->searchState());
	} else {
		_searchController = nullptr;
	}
	if (hasMediaSearch
		|| hasRequestsListSearch
		|| hasCommonGroupsSearch
		|| hasDownloadsSearch
		|| hasMembersSearch) {
		_searchFieldController
			= std::make_unique<Ui::SearchFieldController>(
				searchQuery);
		if (_searchController) {
			_searchFieldController->queryValue(
			) | rpl::start_with_next([=](QString &&query) {
				_searchController->setQuery(
					produceSearchQuery(std::move(query)));
			}, _searchFieldController->lifetime());
		}
		_seachEnabledByContent = memento->searchEnabledByContent();
		_searchStartsFocused = memento->searchStartsFocused();
	} else {
		_searchFieldController = nullptr;
	}
}

void Controller::saveSearchState(not_null<ContentMemento*> memento) {
	if (_searchFieldController) {
		memento->setSearchFieldQuery(
			_searchFieldController->query());
		memento->setSearchEnabledByContent(
			_seachEnabledByContent.current());
	}
	if (_searchController) {
		auto mediaMemento = dynamic_cast<Media::Memento*>(
			memento.get());
		Assert(mediaMemento != nullptr);
		mediaMemento->setSearchState(_searchController->saveState());
	}
}

void Controller::showSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const Window::SectionShow &params) {
	if (!_widget->showInternal(memento.get(), params)) {
		AbstractController::showSection(std::move(memento), params);
	}
}

void Controller::showBackFromStack(const Window::SectionShow &params) {
	if (!_widget->showBackFromStackInternal(params)) {
		AbstractController::showBackFromStack(params);
	}
}

void Controller::removeFromStack(const std::vector<Section> &sections) const {
	_widget->removeFromStack(sections);
}

auto Controller::produceSearchQuery(
		const QString &query) const -> SearchQuery {
	Expects(_key.peer() != nullptr);

	auto result = SearchQuery();
	result.type = _section.mediaType();
	result.peerId = _key.peer()->id;
	result.topicRootId = _key.topic() ? _key.topic()->rootId() : 0;
	result.query = query;
	result.migratedPeerId = _migrated ? _migrated->id : PeerId(0);
	return result;
}

rpl::producer<bool> Controller::searchEnabledByContent() const {
	return _seachEnabledByContent.value();
}

rpl::producer<QString> Controller::mediaSourceQueryValue() const {
	return _searchController->currentQueryValue();
}

rpl::producer<QString> Controller::searchQueryValue() const {
	const auto controller = searchFieldController();
	return controller ? controller->queryValue() : rpl::single(QString());
}

rpl::producer<SparseIdsMergedSlice> Controller::mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const {
	auto query = _searchController->currentQuery();
	if (!query.query.isEmpty()) {
		return _searchController->idsSlice(
			aroundId,
			limitBefore,
			limitAfter);
	}

	return SharedMediaMergedViewer(
		&session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				query.peerId,
				query.topicRootId,
				query.monoforumPeerId,
				query.migratedPeerId,
				aroundId),
			query.type),
		limitBefore,
		limitAfter);
}

std::any &Controller::stepDataReference() {
	return _stepData;
}

void Controller::takeStepData(not_null<Controller*> another) {
	_stepData = base::take(another->_stepData);
}

Controller::~Controller() = default;

} // namespace Info
