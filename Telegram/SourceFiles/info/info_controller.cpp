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

#include "ui/search_field_controller.h"
#include "history/history_shared_media.h"
#include "info/info_content_widget.h"
#include "info/info_memento.h"

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
	updateSearchControllers();
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

void Controller::setSection(const Section &section) {
	_section = section;
	updateSearchControllers();
}

void Controller::updateSearchControllers() {
	auto isMedia = (_section.type() == Section::Type::Media);
	auto mediaType = isMedia
		? _section.mediaType()
		: Section::MediaType::kCount;
	auto hasMediaSearch = isMedia
		&& SharedMediaAllowSearch(mediaType);
//	auto hasCommonGroupsSearch
//		= (_section.type() == Section::Type::CommonGroups);
	if (isMedia) {
		_searchController
			= std::make_unique<Api::DelayedSearchController>();
		_searchController->setQueryFast(produceSearchQuery());
	} else {
		_searchController = nullptr;
	}
	if (hasMediaSearch) {
		_searchFieldController
			= std::make_unique<Ui::SearchFieldController>();
		_searchFieldController->queryValue()
			| rpl::start_with_next([=](QString &&query) {
				_searchController->setQuery(
					produceSearchQuery(std::move(query)));
			}, _searchFieldController->lifetime());
	} else {
		_searchFieldController = nullptr;
	}
}

auto Controller::produceSearchQuery(
		QString &&query) const -> SearchQuery {
	auto result = SearchQuery();
	result.type = _section.mediaType();
	result.peerId = _peer->id;
	result.query = std::move(query);
	result.migratedPeerId = _migrated ? _migrated->id : PeerId(0);
	return result;
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
