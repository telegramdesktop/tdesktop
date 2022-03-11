/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/media/info_media_common.h"
#include "data/data_shared_media.h"

namespace Info {
class AbstractController;
} // namespace Info

namespace Info::Media {

class Provider final : public ListProvider, private ListSectionDelegate {
public:
	explicit Provider(not_null<AbstractController*> controller);

	Type type() override;
	bool hasSelectRestriction() override;
	rpl::producer<bool> hasSelectRestrictionChanges() override;
	bool isPossiblyMyItem(not_null<const HistoryItem*> item) override;

	std::optional<int> fullCount() override;

	void restart() override;
	void checkPreload(
		QSize viewport,
		not_null<BaseLayout*> topLayout,
		not_null<BaseLayout*> bottomLayout,
		bool preloadTop,
		bool preloadBottom) override;
	void refreshViewer() override;
	rpl::producer<> refreshed() override;

	std::vector<ListSection> fillSections(
		not_null<Overview::Layout::Delegate*> delegate) override;
	rpl::producer<not_null<BaseLayout*>> layoutRemoved() override;
	BaseLayout *lookupLayout(const HistoryItem *item) override;
	bool isMyItem(not_null<const HistoryItem*> item) override;
	bool isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) override;

	void setSearchQuery(QString query) override;

	ListItemSelectionData computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) override;
	void applyDragSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) override;

	bool allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) override;
	QString showInFolderPath(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) override;

	int64 scrollTopStatePosition(not_null<HistoryItem*> item) override;
	HistoryItem *scrollTopStateItem(ListScrollTopState state) override;
	void saveState(
		not_null<Memento*> memento,
		ListScrollTopState scrollState) override;
	void restoreState(
		not_null<Memento*> memento,
		Fn<void(ListScrollTopState)> restoreScrollState) override;

private:
	static constexpr auto kMinimalIdsLimit = 16;
	static constexpr auto kDefaultAroundId = (ServerMaxMsgId - 1);

	bool sectionHasFloatingHeader() override;
	QString sectionTitle(not_null<const BaseLayout*> item) override;
	bool sectionItemBelongsHere(
		not_null<const BaseLayout*> item,
		not_null<const BaseLayout*> previous) override;

	[[nodiscard]] bool isPossiblyMyPeerId(PeerId peerId) const;
	[[nodiscard]] FullMsgId computeFullId(UniversalMsgId universalId) const;
	[[nodiscard]] BaseLayout *getLayout(
		UniversalMsgId universalId,
		not_null<Overview::Layout::Delegate*> delegate);
	[[nodiscard]] std::unique_ptr<BaseLayout> createLayout(
		UniversalMsgId universalId,
		not_null<Overview::Layout::Delegate*> delegate,
		Type type);

	[[nodiscard]] SparseIdsMergedSlice::Key sliceKey(
		UniversalMsgId universalId) const;

	void itemRemoved(not_null<const HistoryItem*> item);
	void markLayoutsStale();
	void clearStaleLayouts();

	const not_null<AbstractController*> _controller;

	const not_null<PeerData*> _peer;
	PeerData * const _migrated = nullptr;
	const Type _type = Type::Photo;

	UniversalMsgId _universalAroundId = kDefaultAroundId;
	int _idsLimit = kMinimalIdsLimit;
	SparseIdsMergedSlice _slice;

	std::unordered_map<UniversalMsgId, CachedItem> _layouts;
	rpl::event_stream<not_null<BaseLayout*>> _layoutRemoved;
	rpl::event_stream<> _refreshed;

	rpl::lifetime _lifetime;
	rpl::lifetime _viewerLifetime;

};

} // namespace Info::Media
