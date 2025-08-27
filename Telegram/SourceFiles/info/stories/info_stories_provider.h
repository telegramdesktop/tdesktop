/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_stories_ids.h"
#include "info/media/info_media_common.h"
#include "info/stories/info_stories_common.h"

class DocumentData;
class HistoryItem;
class PeerData;
class History;

namespace Data {
class Story;
} // namespace Data

namespace Info {
class AbstractController;
} // namespace Info

namespace Info::Stories {

class Provider final
	: public Media::ListProvider
	, private Media::ListSectionDelegate
	, public base::has_weak_ptr {
public:
	explicit Provider(not_null<AbstractController*> controller);
	~Provider();

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
	static constexpr auto kDefaultAroundId = ServerMaxStoryId - 1;

	bool sectionHasFloatingHeader() override;
	QString sectionTitle(not_null<const Media::BaseLayout*> item) override;
	bool sectionItemBelongsHere(
		not_null<const Media::BaseLayout*> item,
		not_null<const Media::BaseLayout*> previous) override;

	void storyRemoved(not_null<Data::Story*> story);
	void markLayoutsStale();
	void clearStaleLayouts();
	void clear();

	[[nodiscard]] HistoryItem *ensureItem(StoryId id);
	[[nodiscard]] Media::BaseLayout *getLayout(
		StoryId id,
		not_null<Overview::Layout::Delegate*> delegate);
	[[nodiscard]] std::unique_ptr<Media::BaseLayout> createLayout(
		StoryId id,
		not_null<Overview::Layout::Delegate*> delegate);

	const not_null<AbstractController*> _controller;
	const not_null<PeerData*> _peer;
	const not_null<History*> _history;
	const int _albumId = 0;
	const int _addingToAlbumId = 0;

	StoryId _aroundId = kDefaultAroundId;
	int _idsLimit = kMinimalIdsLimit;
	Data::StoriesIdsSlice _slice;

	base::flat_map<StoryId, std::shared_ptr<HistoryItem>> _items;
	std::unordered_map<StoryId, Media::CachedItem> _layouts;
	rpl::event_stream<not_null<Media::BaseLayout*>> _layoutRemoved;
	rpl::event_stream<> _refreshed;

	bool _started = false;

	rpl::lifetime _lifetime;
	rpl::lifetime _viewerLifetime;

};

} // namespace Info::Stories
