/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "overview/overview_layout.h"

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Info::Media {

using Type = Storage::SharedMediaType;
using BaseLayout = Overview::Layout::ItemBase;

class Memento;
class ListSection;

inline constexpr auto kPreloadIfLessThanScreens = 2;

struct ListItemSelectionData {
	explicit ListItemSelectionData(TextSelection text) : text(text) {
	}

	TextSelection text;
	bool canDelete = false;
	bool canForward = false;
	bool canToggleStoryPin = false;
	bool canUnpinStory = false;

	friend inline bool operator==(
		ListItemSelectionData,
		ListItemSelectionData) = default;
};

using ListSelectedMap = base::flat_map<
	not_null<const HistoryItem*>,
	ListItemSelectionData,
	std::less<>>;

enum class ListDragSelectAction {
	None,
	Selecting,
	Deselecting,
};

struct ListContext {
	Overview::Layout::PaintContext layoutContext;
	not_null<ListSelectedMap*> selected;
	not_null<ListSelectedMap*> dragSelected;
	ListDragSelectAction dragSelectAction = ListDragSelectAction::None;
};

struct ListScrollTopState {
	int64 position = 0; // ListProvider-specific.
	HistoryItem *item = nullptr;
	int shift = 0;
};

struct ListFoundItem {
	not_null<BaseLayout*> layout;
	QRect geometry;
	bool exact = false;
};

struct CachedItem {
	CachedItem(std::unique_ptr<BaseLayout> item) : item(std::move(item)) {
	};
	CachedItem(CachedItem &&other) = default;
	CachedItem &operator=(CachedItem &&other) = default;
	~CachedItem() = default;

	std::unique_ptr<BaseLayout> item;
	bool stale = false;
};

using UniversalMsgId = MsgId;

[[nodiscard]] UniversalMsgId GetUniversalId(FullMsgId itemId);
[[nodiscard]] UniversalMsgId GetUniversalId(
	not_null<const HistoryItem*> item);
[[nodiscard]] UniversalMsgId GetUniversalId(
	not_null<const BaseLayout*> layout);

bool ChangeItemSelection(
	ListSelectedMap &selected,
	not_null<const HistoryItem*> item,
	ListItemSelectionData selectionData);

class ListSectionDelegate {
public:
	[[nodiscard]] virtual bool sectionHasFloatingHeader() = 0;
	[[nodiscard]] virtual QString sectionTitle(
		not_null<const BaseLayout*> item) = 0;
	[[nodiscard]] virtual bool sectionItemBelongsHere(
		not_null<const BaseLayout*> item,
		not_null<const BaseLayout*> previous) = 0;

	[[nodiscard]] not_null<ListSectionDelegate*> sectionDelegate() {
		return this;
	}
};

class ListProvider {
public:
	[[nodiscard]] virtual Type type() = 0;
	[[nodiscard]] virtual bool hasSelectRestriction() = 0;
	[[nodiscard]] virtual auto hasSelectRestrictionChanges()
		->rpl::producer<bool> = 0;
	[[nodiscard]] virtual bool isPossiblyMyItem(
		not_null<const HistoryItem*> item) = 0;

	[[nodiscard]] virtual std::optional<int> fullCount() = 0;

	virtual void restart() = 0;
	virtual void checkPreload(
		QSize viewport,
		not_null<BaseLayout*> topLayout,
		not_null<BaseLayout*> bottomLayout,
		bool preloadTop,
		bool preloadBottom) = 0;
	virtual void refreshViewer() = 0;
	[[nodiscard]] virtual rpl::producer<> refreshed() = 0;

	[[nodiscard]] virtual std::vector<ListSection> fillSections(
		not_null<Overview::Layout::Delegate*> delegate) = 0;
	[[nodiscard]] virtual auto layoutRemoved()
		-> rpl::producer<not_null<BaseLayout*>> = 0;
	[[nodiscard]] virtual BaseLayout *lookupLayout(
		const HistoryItem *item) = 0;
	[[nodiscard]] virtual bool isMyItem(
		not_null<const HistoryItem*> item) = 0;
	[[nodiscard]] virtual bool isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) = 0;

	[[nodiscard]] virtual ListItemSelectionData computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) = 0;
	virtual void applyDragSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) = 0;

	[[nodiscard]] virtual bool allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) = 0;
	[[nodiscard]] virtual QString showInFolderPath(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) = 0;

	virtual void setSearchQuery(QString query) = 0;

	[[nodiscard]] virtual int64 scrollTopStatePosition(
		not_null<HistoryItem*> item) = 0;
	[[nodiscard]] virtual HistoryItem *scrollTopStateItem(
		ListScrollTopState state) = 0;
	virtual void saveState(
		not_null<Memento*> memento,
		ListScrollTopState scrollState) = 0;
	virtual void restoreState(
		not_null<Memento*> memento,
		Fn<void(ListScrollTopState)> restoreScrollState) = 0;

	virtual ~ListProvider() = default;
};

} // namespace Info::Media
