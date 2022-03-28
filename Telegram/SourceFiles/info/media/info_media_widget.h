/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include "info/info_content_widget.h"
#include "storage/storage_shared_media.h"
#include "data/data_search_controller.h"

namespace Info {
namespace Media {

using Type = Storage::SharedMediaType;

std::optional<int> TypeToTabIndex(Type type);
Type TabIndexToType(int index);

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<PeerData*> peer, PeerId migratedPeerId, Type type);

	using SearchState = Api::DelayedSearchController::SavedState;

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	[[nodiscard]] Section section() const override;

	[[nodiscard]] Type type() const {
		return _type;
	}

	// Only for media, not for downloads.
	void setAroundId(FullMsgId aroundId) {
		_aroundId = aroundId;
	}
	[[nodiscard]] FullMsgId aroundId() const {
		return _aroundId;
	}
	void setIdsLimit(int limit) {
		_idsLimit = limit;
	}
	[[nodiscard]] int idsLimit() const {
		return _idsLimit;
	}

	void setScrollTopItem(GlobalMsgId item) {
		_scrollTopItem = item;
	}
	[[nodiscard]] GlobalMsgId scrollTopItem() const {
		return _scrollTopItem;
	}
	void setScrollTopItemPosition(int64 position) {
		_scrollTopItemPosition = position;
	}
	[[nodiscard]] int64 scrollTopItemPosition() const {
		return _scrollTopItemPosition;
	}
	void setScrollTopShift(int shift) {
		_scrollTopShift = shift;
	}
	[[nodiscard]] int scrollTopShift() const {
		return _scrollTopShift;
	}
	void setSearchState(SearchState &&state) {
		_searchState = std::move(state);
	}
	[[nodiscard]] SearchState searchState() {
		return std::move(_searchState);
	}

private:
	Type _type = Type::Photo;
	FullMsgId _aroundId;
	int _idsLimit = 0;
	int64 _scrollTopItemPosition = 0;
	GlobalMsgId _scrollTopItem;
	int _scrollTopShift = 0;
	SearchState _searchState;

};

class Widget final : public ContentWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Controller*> controller);

	void setIsStackBottom(bool isStackBottom) override;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<SelectedItems> selectedListValue() const override;
	void selectionAction(SelectionAction action) override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

} // namespace Media
} // namespace Info
