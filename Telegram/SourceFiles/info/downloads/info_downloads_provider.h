/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/media/info_media_common.h"

namespace Info {
class AbstractController;
} // namespace Info

namespace Info::Downloads {

class Provider final : public Media::ListProvider {
public:
	explicit Provider(not_null<AbstractController*> controller);

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

	std::vector<Media::ListSection> fillSections(
		not_null<Overview::Layout::Delegate*> delegate) override;
	rpl::producer<not_null<Media::BaseLayout*>> layoutRemoved() override;
	Media::BaseLayout *lookupLayout(const HistoryItem *item) override;
	bool isMyItem(not_null<const HistoryItem*> item) override;
	bool isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) override;

	void applyDragSelection(
		Media::ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) override;

	void saveState(
		not_null<Media::Memento*> memento,
		Media::ListScrollTopState scrollState) override;
	void restoreState(
		not_null<Media::Memento*> memento,
		Fn<void(Media::ListScrollTopState)> restoreScrollState) override;

private:
	struct Element {
		not_null<HistoryItem*> item;
		int64 started = 0; // unixtime * 1000
	};

	void itemRemoved(not_null<const HistoryItem*> item);
	void markLayoutsStale();
	void clearStaleLayouts();

	[[nodiscard]] Media::BaseLayout *getLayout(
		Element element,
		not_null<Overview::Layout::Delegate*> delegate);
	[[nodiscard]] std::unique_ptr<Media::BaseLayout> createLayout(
		Element element,
		not_null<Overview::Layout::Delegate*> delegate);

	const not_null<AbstractController*> _controller;

	std::vector<Element> _elements;
	std::optional<int> _fullCount;
	base::flat_set<not_null<const HistoryItem*>> _downloading;
	base::flat_set<not_null<const HistoryItem*>> _downloaded;

	std::unordered_map<not_null<HistoryItem*>, Media::CachedItem> _layouts;
	rpl::event_stream<not_null<Media::BaseLayout*>> _layoutRemoved;
	rpl::event_stream<> _refreshed;

	rpl::lifetime _lifetime;

};

} // namespace Info::Downloads
