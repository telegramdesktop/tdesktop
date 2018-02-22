/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_controller.h"

#include <rpl/range.h>
#include <rpl/then.h>
#include "ui/search_field_controller.h"
#include "data/data_shared_media.h"
#include "info/info_content_widget.h"
#include "info/info_memento.h"
#include "info/media/info_media_widget.h"
#include "observer_peer.h"
#include "window/window_controller.h"

namespace Info {
namespace {

not_null<PeerData*> CorrectPeer(PeerId peerId) {
	Expects(peerId != 0);

	auto result = App::peer(peerId);
	if (auto to = result->migrateTo()) {
		return to;
	}
	return result;
}

} // namespace

Key::Key(not_null<PeerData*> peer) : _value(peer) {
}

Key::Key(not_null<Data::Feed*> feed) : _value(feed) {
}

PeerData *Key::peer() const {
	if (const auto peer = base::get_if<not_null<PeerData*>>(&_value)) {
		return *peer;
	}
	return nullptr;
}

Data::Feed *Key::feed() const {
	if (const auto feed = base::get_if<not_null<Data::Feed*>>(&_value)) {
		return *feed;
	}
	return nullptr;
}

rpl::producer<SparseIdsMergedSlice> AbstractController::mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const {
	return SharedMediaMergedViewer(
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				peerId(),
				migratedPeerId(),
				aroundId),
			section().mediaType()),
		limitBefore,
		limitAfter);
}

rpl::producer<QString> AbstractController::mediaSourceQueryValue() const {
	return rpl::single(QString());
}

void AbstractController::showSection(
		Window::SectionMemento &&memento,
		const Window::SectionShow &params) {
	return parentController()->showSection(std::move(memento), params);
}

void AbstractController::showBackFromStack(
		const Window::SectionShow &params) {
	return parentController()->showBackFromStack(params);
}

Controller::Controller(
	not_null<WrapWidget*> widget,
	not_null<Window::Controller*> window,
	not_null<ContentMemento*> memento)
: AbstractController(window)
, _widget(widget)
, _key(memento->peerId()
	? Key(App::peer(memento->peerId()))
	: Key(memento->feed()))
, _migrated(memento->migratedPeerId()
	? App::peer(memento->migratedPeerId())
	: nullptr)
, _section(memento->section()) {
	updateSearchControllers(memento);
	setupMigrationViewer();
}

void Controller::setupMigrationViewer() {
	const auto peer = _key.peer();
	if (!peer || (!peer->isChat() && !peer->isChannel()) || _migrated) {
		return;
	}
	Notify::PeerUpdateValue(
		peer,
		Notify::PeerUpdate::Flag::MigrationChanged
	) | rpl::start_with_next([=] {
		if (peer->migrateTo() || (peer->migrateFrom() != _migrated)) {
			const auto window = parentController();
			const auto section = _section;
			InvokeQueued(_widget, [=] {
				window->showSection(
					Memento(peer->id, section),
					Window::SectionShow(
						Window::SectionShow::Way::Backward,
						anim::type::instant,
						anim::activation::background));
			});
		}
	}, lifetime());
}

Wrap Controller::wrap() const {
	return _widget->wrap();
}

rpl::producer<Wrap> Controller::wrapValue() const {
	return _widget->wrapValue();
}

bool Controller::validateMementoPeer(
		not_null<ContentMemento*> memento) const {
	return memento->peerId() == peerId()
		&& memento->migratedPeerId() == migratedPeerId()
		&& memento->feed() == feed();
}

void Controller::setSection(not_null<ContentMemento*> memento) {
	_section = memento->section();
	updateSearchControllers(memento);
}

void Controller::updateSearchControllers(
		not_null<ContentMemento*> memento) {
	using Type = Section::Type;
	auto type = _section.type();
	auto isMedia = (type == Type::Media);
	auto mediaType = isMedia
		? _section.mediaType()
		: Section::MediaType::kCount;
	auto hasMediaSearch = isMedia
		&& SharedMediaAllowSearch(mediaType);
	auto hasCommonGroupsSearch
		= (type == Type::CommonGroups);
	auto hasMembersSearch
		= (type == Type::Members
			|| type == Type::Profile
			|| type == Type::Channels);
	auto searchQuery = memento->searchFieldQuery();
	if (isMedia) {
		_searchController
			= std::make_unique<Api::DelayedSearchController>();
		auto mediaMemento = dynamic_cast<Media::Memento*>(memento.get());
		Assert(mediaMemento != nullptr);
		_searchController->restoreState(
			mediaMemento->searchState());
	} else {
		_searchController = nullptr;
	}
	if (hasMediaSearch || hasCommonGroupsSearch || hasMembersSearch) {
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
		Window::SectionMemento &&memento,
		const Window::SectionShow &params) {
	if (!_widget->showInternal(&memento, params)) {
		AbstractController::showSection(std::move(memento), params);
	}
}

void Controller::showBackFromStack(const Window::SectionShow &params) {
	if (!_widget->showBackFromStackInternal(params)) {
		AbstractController::showBackFromStack(params);
	}
}

auto Controller::produceSearchQuery(
		const QString &query) const -> SearchQuery {
	Expects(_key.peer() != nullptr);

	auto result = SearchQuery();
	result.type = _section.mediaType();
	result.peerId = _key.peer()->id;
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
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				query.peerId,
				query.migratedPeerId,
				aroundId),
			query.type),
		limitBefore,
		limitAfter);
}

Controller::~Controller() = default;

} // namespace Info
