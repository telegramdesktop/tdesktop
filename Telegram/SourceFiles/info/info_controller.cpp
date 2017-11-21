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

Controller::Controller(
	not_null<WrapWidget*> widget,
	not_null<Window::Controller*> window,
	not_null<ContentMemento*> memento)
: _widget(widget)
, _peer(App::peer(memento->peerId()))
, _migrated(memento->migratedPeerId()
	? App::peer(memento->migratedPeerId())
	: nullptr)
, _window(window)
, _section(memento->section()) {
	updateSearchControllers(memento);
	setupMigrationViewer();
}

void Controller::setupMigrationViewer() {
	if (!_peer->isChat() && (!_peer->isChannel() || _migrated != nullptr)) {
		return;
	}
	Notify::PeerUpdateValue(_peer, Notify::PeerUpdate::Flag::MigrationChanged)
		| rpl::start_with_next([this] {
			if (_peer->migrateTo() || (_peer->migrateFrom() != _migrated)) {
				auto windowController = window();
				auto peerId = _peer->id;
				auto section = _section;
				InvokeQueued(_widget, [=] {
					windowController->showSection(
						Memento(peerId, section),
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
		&& memento->migratedPeerId() == migratedPeerId();
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
		= (type == Type::Members || type == Type::Profile);
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
			_searchFieldController->queryValue()
				| rpl::start_with_next([=](QString &&query) {
					_searchController->setQuery(
						produceSearchQuery(std::move(query)));
				}, _searchFieldController->lifetime());
		}
		_seachEnabledByContent = memento->searchEnabledByContent();
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

auto Controller::produceSearchQuery(
		const QString &query) const -> SearchQuery {
	auto result = SearchQuery();
	result.type = _section.mediaType();
	result.peerId = _peer->id;
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
