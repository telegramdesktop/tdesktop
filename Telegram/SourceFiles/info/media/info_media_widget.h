/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"
#include "storage/storage_shared_media.h"
#include "data/data_search_controller.h"

namespace tr {
template <typename ...Tags>
struct phrase;
} // namespace tr

namespace Data {
class ForumTopic;
} // namespace Data

namespace Info::Media {

using Type = Storage::SharedMediaType;

[[nodiscard]] std::optional<int> TypeToTabIndex(Type type);
[[nodiscard]] Type TabIndexToType(int index);
[[nodiscard]] tr::phrase<> SharedMediaTitle(Type type);

class InnerWidget;

class Memento final : public ContentMemento {
public:
	explicit Memento(not_null<Controller*> controller);
	Memento(not_null<PeerData*> peer, PeerId migratedPeerId, Type type);
	Memento(not_null<Data::ForumTopic*> topic, Type type);
	Memento(not_null<Data::SavedSublist*> sublist, Type type);

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
	Memento(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Data::SavedSublist *sublist,
		PeerId migratedPeerId,
		Type type);

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

	rpl::producer<QString> title() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

} // namespace Info::Media
