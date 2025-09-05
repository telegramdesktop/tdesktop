/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_saved_music.h"
#include "info/media/info_media_common.h"
#include "info/saved/info_saved_music_common.h"
#include "history/history_item.h"

class DocumentData;
class HistoryItem;
class PeerData;
class History;

namespace Info {
class AbstractController;
} // namespace Info

namespace Info::Saved {

class MusicProvider final
	: public Media::ListProvider
	, private Media::ListSectionDelegate
	, public base::has_weak_ptr {
public:
	explicit MusicProvider(not_null<AbstractController*> controller);
	~MusicProvider();

	Media::Type type() override;
	bool hasSelectRestriction() override;
	rpl::producer<bool> hasSelectRestrictionChanges() override;
	bool isPossiblyMyItem(not_null<const HistoryItem*> item) override;

	std::optional<int> fullCount() override;

	void restart() override;
	void checkPreload(
		QSize viewport,
		not_null<Media::BaseLayout*> topLayout,
		not_null<Media::BaseLayout*> bottomLayout,
		bool preloadTop,
		bool preloadBottom) override;
	void refreshViewer() override;
	rpl::producer<> refreshed() override;

	void setSearchQuery(QString query) override;

	std::vector<Media::ListSection> fillSections(
		not_null<Overview::Layout::Delegate*> delegate) override;
	rpl::producer<not_null<Media::BaseLayout*>> layoutRemoved() override;
	Media::BaseLayout *lookupLayout(const HistoryItem *item) override;
	bool isMyItem(not_null<const HistoryItem*> item) override;
	bool isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) override;

	Media::ListItemSelectionData computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) override;
	void applyDragSelection(
		Media::ListSelectedMap &selected,
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
	HistoryItem *scrollTopStateItem(
		Media::ListScrollTopState state) override;
	void saveState(
		not_null<Media::Memento*> memento,
		Media::ListScrollTopState scrollState) override;
	void restoreState(
		not_null<Media::Memento*> memento,
		Fn<void(Media::ListScrollTopState)> restoreScrollState) override;

private:
	static constexpr auto kMinimalIdsLimit = 16;

	bool sectionHasFloatingHeader() override;
	QString sectionTitle(not_null<const Media::BaseLayout*> item) override;
	bool sectionItemBelongsHere(
		not_null<const Media::BaseLayout*> item,
		not_null<const Media::BaseLayout*> previous) override;

	void markLayoutsStale();
	void clearStaleLayouts();
	void clear();

	[[nodiscard]] Media::BaseLayout *getLayout(
		not_null<HistoryItem*> item,
		not_null<Overview::Layout::Delegate*> delegate);
	[[nodiscard]] std::unique_ptr<Media::BaseLayout> createLayout(
		not_null<HistoryItem*> item,
		not_null<Overview::Layout::Delegate*> delegate);

	const not_null<AbstractController*> _controller;
	const not_null<PeerData*> _peer;
	const not_null<History*> _history;

	HistoryItem *_aroundId = nullptr;
	int _idsLimit = kMinimalIdsLimit;
	Data::SavedMusicSlice _slice;

	std::unordered_map<not_null<HistoryItem*>, Media::CachedItem> _layouts;
	rpl::event_stream<not_null<Media::BaseLayout*>> _layoutRemoved;
	rpl::event_stream<> _refreshed;

	bool _started = false;

	rpl::lifetime _lifetime;
	rpl::lifetime _viewerLifetime;

};

} // namespace Info::Saved
