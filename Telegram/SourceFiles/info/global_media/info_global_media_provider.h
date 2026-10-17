/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_messages.h"
#include "info/media/info_media_common.h"
#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Info {
class AbstractController;
} // namespace Info

namespace Info::GlobalMedia {

struct GlobalMediaKey {
	Data::MessagePosition aroundId;

	friend inline constexpr bool operator==(
		const GlobalMediaKey &,
		const GlobalMediaKey &) = default;
};

struct GlobalMediaSliceSnapshot {
	QString query;
	uint64 generation = 0;
	int fullCount = 0;
	int skippedAfter = 0;
	int skippedBefore = 0;
	bool fullyLoaded = false;
	std::vector<Data::MessagePosition> positions;
};

class GlobalMediaSlice final {
public:
	using Key = GlobalMediaKey;
	using Value = Data::MessagePosition;

	explicit GlobalMediaSlice(
		Key key,
		std::vector<Data::MessagePosition> items = {},
		std::optional<int> fullCount = std::nullopt,
		int skippedAfter = 0);

	[[nodiscard]] std::optional<int> fullCount() const;
	[[nodiscard]] std::optional<int> skippedBefore() const;
	[[nodiscard]] std::optional<int> skippedAfter() const;
	[[nodiscard]] std::optional<int> indexOf(Value fullId) const;
	[[nodiscard]] int size() const;
	[[nodiscard]] Value operator[](int index) const;
	[[nodiscard]] std::optional<int> distance(
		const Key &a,
		const Key &b) const;
	[[nodiscard]] std::optional<Value> nearest(Value id) const;
	[[nodiscard]] const std::vector<Value> &items() const;

private:
	GlobalMediaKey _key;
	std::vector<Data::MessagePosition> _items;
	std::optional<int> _fullCount;
	int _skippedAfter = 0;

};

class Provider final
	: public Media::ListProvider
	, private Media::ListSectionDelegate {
public:
	using Type = Media::Type;
	using BaseLayout = Media::BaseLayout;

	explicit Provider(not_null<AbstractController*> controller);
	~Provider() override;

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
	[[nodiscard]] bool anchorWhileAtTop() override;

	[[nodiscard]] auto sliceSnapshot() const
		-> const std::optional<GlobalMediaSliceSnapshot> &;

	std::vector<Media::ListSection> fillSections(
		not_null<Overview::Layout::Delegate*> delegate) override;
	rpl::producer<not_null<BaseLayout*>> layoutRemoved() override;
	BaseLayout *lookupLayout(const HistoryItem *item) override;
	bool isMyItem(not_null<const HistoryItem*> item) override;
	bool isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) override;

	void setSearchQuery(QString query) override;
	void jumpToMessage(MsgId messageId, Fn<void(FullMsgId)> callback) override;

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

	struct FillResult {
		GlobalMediaSlice slice;
		bool notEnough = false;
	};
	struct SliceUpdate {
		QString query;
		uint64 generation = 0;
		GlobalMediaSlice slice;
	};
	struct RequestCursor {
		Data::MessagePosition position;
		int32 rate = 0;

		friend inline constexpr bool operator==(
			const RequestCursor &,
			const RequestCursor &) = default;
	};
	struct List {
		std::vector<Data::MessagePosition> list;
		base::flat_set<FullMsgId> ids;
		Data::MessagePosition offsetPosition;
		int32 offsetRate = 0;
		int fullCount = 0;
		int filteredCount = 0;
		mtpRequestId requestId = 0;
		uint64 requestToken = 0;
		std::vector<Fn<void()>> requestWaiters;
		std::vector<RequestCursor> requestCursors;
		bool loaded = false;
	};
	struct EdgeRequestKey {
		enum class Direction : uchar {
			Top,
			Bottom,
		};

		uint64 generation = 0;
		Direction direction = Direction::Top;
		Data::MessagePosition aroundId;

		friend inline constexpr bool operator==(
			const EdgeRequestKey &,
			const EdgeRequestKey &) = default;
	};

	bool sectionHasFloatingHeader() override;
	QString sectionTitle(not_null<const BaseLayout*> item) override;
	bool sectionItemBelongsHere(
		not_null<const BaseLayout*> item,
		not_null<const BaseLayout*> previous) override;

	[[nodiscard]] rpl::producer<SliceUpdate> source(
		Type type,
		Data::MessagePosition aroundId,
		QString query,
		uint64 generation,
		int limitBefore,
		int limitAfter);

	[[nodiscard]] BaseLayout *getLayout(
		FullMsgId itemId,
		not_null<Overview::Layout::Delegate*> delegate);
	[[nodiscard]] std::unique_ptr<BaseLayout> createLayout(
		FullMsgId itemId,
		not_null<Overview::Layout::Delegate*> delegate,
		Type type);

	[[nodiscard]] GlobalMediaKey sliceKey(
		Data::MessagePosition aroundId) const;

	void itemRemoved(not_null<const HistoryItem*> item);
	void markLayoutsStale();
	void clearStaleLayouts();
	[[nodiscard]] List *listForQuery(const QString &query);
	[[nodiscard]] FillResult fillRequest(
		const QString &query,
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter);
	void requestMore(
		const QString &query,
		uint64 generation,
		Fn<void()> loaded);
	[[nodiscard]] std::optional<GlobalMediaSliceSnapshot> makeSnapshot(
		const SliceUpdate &update) const;

	const not_null<AbstractController*> _controller;
	const not_null<Main::Session*> _session;
	const Type _type = {};
	const bool _onlyForwardable = false;

	Data::MessagePosition _aroundId = Data::MaxMessagePosition;
	int _idsLimit = kMinimalIdsLimit;
	GlobalMediaSlice _slice;
	uint64 _generation = 0;
	std::optional<EdgeRequestKey> _edgeRequest;
	std::optional<GlobalMediaSliceSnapshot> _sliceSnapshot;

	base::flat_set<FullMsgId> _seenIds;
	std::unordered_map<FullMsgId, Media::CachedItem> _layouts;
	rpl::event_stream<not_null<BaseLayout*>> _layoutRemoved;
	rpl::event_stream<> _refreshed;

	QString _totalListQuery;
	base::flat_map<QString, List> _totalLists;

	rpl::lifetime _lifetime;
	rpl::lifetime _viewerLifetime;

};

} // namespace Info::GlobalMedia
