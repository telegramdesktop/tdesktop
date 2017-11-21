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
#pragma once

#include <rpl/variable.h>
#include "data/data_search_controller.h"

namespace Ui {
class SearchFieldController;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace Info {

enum class Wrap;
class WrapWidget;
class Memento;
class ContentMemento;

class Section final {
public:
	enum class Type {
		Profile,
		Media,
		CommonGroups,
		Members,
	};
	using MediaType = Storage::SharedMediaType;

	Section(Type type) : _type(type) {
		Expects(type != Type::Media);
	}
	Section(MediaType mediaType)
	: _type(Type::Media)
	, _mediaType(mediaType) {
	}

	Type type() const {
		return _type;
	}
	MediaType mediaType() const {
		Expects(_type == Type::Media);
		return _mediaType;
	}

private:
	Type _type;
	Storage::SharedMediaType _mediaType;

};

class Controller {
public:
	Controller(
		not_null<WrapWidget*> widget,
		not_null<Window::Controller*> window,
		not_null<ContentMemento*> memento);

	not_null<PeerData*> peer() const {
		return _peer;
	}
	PeerData *migrated() const {
		return _migrated;
	}
	PeerId peerId() const {
		return _peer->id;
	}
	PeerId migratedPeerId() const {
		return _migrated ? _migrated->id : PeerId(0);
	}
	const Section &section() const {
		return _section;
	}

	bool validateMementoPeer(
		not_null<ContentMemento*> memento) const;

	Wrap wrap() const;
	rpl::producer<Wrap> wrapValue() const;
	void setSection(not_null<ContentMemento*> memento);

	not_null<Window::Controller*> window() const {
		return _window;
	}
	Ui::SearchFieldController *searchFieldController() const {
		return _searchFieldController.get();
	}
	void setSearchEnabledByContent(bool enabled) {
		_seachEnabledByContent = enabled;
	}
	rpl::producer<bool> searchEnabledByContent() const;
	rpl::producer<SparseIdsMergedSlice> mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const;
	rpl::producer<QString> mediaSourceQueryValue() const;

	void saveSearchState(not_null<ContentMemento*> memento);

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	~Controller();

private:
	using SearchQuery = Api::DelayedSearchController::Query;

	void updateSearchControllers(not_null<ContentMemento*> memento);
	SearchQuery produceSearchQuery(const QString &query) const;
	void setupMigrationViewer();

	not_null<WrapWidget*> _widget;
	not_null<PeerData*> _peer;
	PeerData *_migrated = nullptr;
	not_null<Window::Controller*> _window;
	rpl::variable<Wrap> _wrap;
	Section _section;

	std::unique_ptr<Ui::SearchFieldController> _searchFieldController;
	std::unique_ptr<Api::DelayedSearchController> _searchController;
	rpl::variable<bool> _seachEnabledByContent = false;

	rpl::lifetime _lifetime;

};

} // namespace Info