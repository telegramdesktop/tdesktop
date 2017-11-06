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

#include <rpl/producer.h>
#include "info/info_content_widget.h"
#include "storage/storage_shared_media.h"
#include "data/data_search_controller.h"

namespace Info {
namespace Media {

using Type = Storage::SharedMediaType;

base::optional<int> TypeToTabIndex(Type type);
Type TabIndexToType(int index);

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(PeerId peerId, PeerId migratedPeerId, Type type);

	using SearchState = Api::DelayedSearchController::SavedState;

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	Type type() const {
		return _type;
	}

	void setAroundId(FullMsgId aroundId) {
		_aroundId = aroundId;
	}
	FullMsgId aroundId() const {
		return _aroundId;
	}
	void setIdsLimit(int limit) {
		_idsLimit = limit;
	}
	int idsLimit() const {
		return _idsLimit;
	}
	void setScrollTopItem(FullMsgId item) {
		_scrollTopItem = item;
	}
	FullMsgId scrollTopItem() const {
		return _scrollTopItem;
	}
	void setScrollTopShift(int shift) {
		_scrollTopShift = shift;
	}
	int scrollTopShift() const {
		return _scrollTopShift;
	}
	void setSearchState(SearchState &&state) {
		_searchState = std::move(state);
	}
	SearchState searchState() {
		return std::move(_searchState);
	}

private:
	Type _type = Type::Photo;
	FullMsgId _aroundId;
	int _idsLimit = 0;
	FullMsgId _scrollTopItem;
	int _scrollTopShift = 0;
	SearchState _searchState;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller);

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<SelectedItems> selectedListValue() const override;
	void cancelSelection() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::unique_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

} // namespace Media
} // namespace Info
