/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "base/expected.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_story.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
enum class ReportReason;
} // namespace Ui

namespace Data {

class Folder;
class Session;
struct StoryView;
struct StoryIdDates;
class Story;
class StoryPreload;

struct StoriesIds {
	base::flat_set<StoryId, std::greater<>> list;

	friend inline bool operator==(
		const StoriesIds&,
		const StoriesIds&) = default;
};

struct StoriesSourceInfo {
	PeerId id = 0;
	TimeId last = 0;
	uint32 count : 15 = 0;
	uint32 unreadCount : 15 = 0;
	uint32 premium : 1 = 0;

	friend inline bool operator==(
		StoriesSourceInfo,
		StoriesSourceInfo) = default;
};

struct StoriesSource {
	not_null<PeerData*> peer;
	base::flat_set<StoryIdDates> ids;
	StoryId readTill = 0;
	bool hidden = false;

	[[nodiscard]] StoriesSourceInfo info() const;
	[[nodiscard]] int unreadCount() const;
	[[nodiscard]] StoryIdDates toOpen() const;

	friend inline bool operator==(StoriesSource, StoriesSource) = default;
};

enum class NoStory : uchar {
	Unknown,
	Deleted,
};

enum class StorySourcesList : uchar {
	NotHidden,
	Hidden,
};

struct StoriesContextSingle {
	friend inline auto operator<=>(
		StoriesContextSingle,
		StoriesContextSingle) = default;
	friend inline bool operator==(StoriesContextSingle, StoriesContextSingle) = default;
};

struct StoriesContextPeer {
	friend inline auto operator<=>(
		StoriesContextPeer,
		StoriesContextPeer) = default;
	friend inline bool operator==(StoriesContextPeer, StoriesContextPeer) = default;
};

struct StoriesContextSaved {
	friend inline auto operator<=>(
		StoriesContextSaved,
		StoriesContextSaved) = default;
	friend inline bool operator==(StoriesContextSaved, StoriesContextSaved) = default;
};

struct StoriesContextArchive {
	friend inline auto operator<=>(
		StoriesContextArchive,
		StoriesContextArchive) = default;
	friend inline bool operator==(StoriesContextArchive, StoriesContextArchive) = default;
};

struct StoriesContext {
	std::variant<
		StoriesContextSingle,
		StoriesContextPeer,
		StoriesContextSaved,
		StoriesContextArchive,
		StorySourcesList> data;

	friend inline auto operator<=>(
		StoriesContext,
		StoriesContext) = default;
	friend inline bool operator==(StoriesContext, StoriesContext) = default;
};

struct StealthMode {
	TimeId enabledTill = 0;
	TimeId cooldownTill = 0;

	friend inline auto operator<=>(StealthMode, StealthMode) = default;
	friend inline bool operator==(StealthMode, StealthMode) = default;
};

inline constexpr auto kStorySourcesListCount = 2;

class Stories final : public base::has_weak_ptr {
public:
	explicit Stories(not_null<Session*> owner);
	~Stories();

	static constexpr auto kPinnedToastDuration = 4 * crl::time(1000);

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	void updateDependentMessages(not_null<Data::Story*> story);
	void registerDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<Data::Story*> dependency);
	void unregisterDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<Data::Story*> dependency);

	void loadMore(StorySourcesList list);
	void apply(const MTPDupdateStory &data);
	void apply(const MTPDupdateReadStories &data);
	void apply(const MTPStoriesStealthMode &stealthMode);
	void apply(not_null<PeerData*> peer, const MTPPeerStories *data);
	Story *applyFromWebpage(PeerId peerId, const MTPstoryItem &story);
	void loadAround(FullStoryId id, StoriesContext context);

	const StoriesSource *source(PeerId id) const;
	[[nodiscard]] const std::vector<StoriesSourceInfo> &sources(
		StorySourcesList list) const;
	[[nodiscard]] bool sourcesLoaded(StorySourcesList list) const;
	[[nodiscard]] rpl::producer<> sourcesChanged(
		StorySourcesList list) const;
	[[nodiscard]] rpl::producer<PeerId> sourceChanged() const;
	[[nodiscard]] rpl::producer<PeerId> itemsChanged() const;

	[[nodiscard]] base::expected<not_null<Story*>, NoStory> lookup(
		FullStoryId id) const;
	void resolve(FullStoryId id, Fn<void()> done, bool force = false);
	[[nodiscard]] std::shared_ptr<HistoryItem> resolveItem(FullStoryId id);
	[[nodiscard]] std::shared_ptr<HistoryItem> resolveItem(
		not_null<Story*> story);

	[[nodiscard]] bool isQuitPrevent();
	void markAsRead(FullStoryId id, bool viewed);

	void toggleHidden(
		PeerId peerId,
		bool hidden,
		std::shared_ptr<Ui::Show> show);

	static constexpr auto kViewsPerPage = 50;
	void loadViewsSlice(
		not_null<PeerData*> peer,
		StoryId id,
		QString offset,
		Fn<void(StoryViews)> done);

	[[nodiscard]] bool hasArchive(not_null<PeerData*> peer) const;

	[[nodiscard]] const StoriesIds &archive(PeerId peerId) const;
	[[nodiscard]] rpl::producer<PeerId> archiveChanged() const;
	[[nodiscard]] int archiveCount(PeerId peerId) const;
	[[nodiscard]] bool archiveCountKnown(PeerId peerId) const;
	[[nodiscard]] bool archiveLoaded(PeerId peerId) const;
	void archiveLoadMore(PeerId peerId);

	[[nodiscard]] const StoriesIds &saved(PeerId peerId) const;
	[[nodiscard]] rpl::producer<PeerId> savedChanged() const;
	[[nodiscard]] int savedCount(PeerId peerId) const;
	[[nodiscard]] bool savedCountKnown(PeerId peerId) const;
	[[nodiscard]] bool savedLoaded(PeerId peerId) const;
	void savedLoadMore(PeerId peerId);

	void deleteList(const std::vector<FullStoryId> &ids);
	void togglePinnedList(const std::vector<FullStoryId> &ids, bool pinned);
	void report(
		std::shared_ptr<Ui::Show> show,
		FullStoryId id,
		Ui::ReportReason reason,
		QString text);

	void incrementPreloadingMainSources();
	void decrementPreloadingMainSources();
	void incrementPreloadingHiddenSources();
	void decrementPreloadingHiddenSources();
	void setPreloadingInViewer(std::vector<FullStoryId> ids);

	struct PeerSourceState {
		StoryId maxId = 0;
		StoryId readTill = 0;
	};
	[[nodiscard]] std::optional<PeerSourceState> peerSourceState(
		not_null<PeerData*> peer,
		StoryId storyMaxId);
	[[nodiscard]] bool isUnread(not_null<Story*> story);

	enum class Polling {
		Chat,
		Viewer,
	};
	void registerPolling(not_null<Story*> story, Polling polling);
	void unregisterPolling(not_null<Story*> story, Polling polling);

	[[nodiscard]] bool registerPolling(FullStoryId id, Polling polling);
	void unregisterPolling(FullStoryId id, Polling polling);
	void requestPeerStories(
		not_null<PeerData*> peer,
		Fn<void()> done = nullptr);

	void savedStateChanged(not_null<Story*> story);
	[[nodiscard]] std::shared_ptr<HistoryItem> lookupItem(
		not_null<Story*> story);

	[[nodiscard]] StealthMode stealthMode() const;
	[[nodiscard]] rpl::producer<StealthMode> stealthModeValue() const;
	void activateStealthMode(Fn<void()> done = nullptr);

	void sendReaction(FullStoryId id, Data::ReactionId reaction);

private:
	struct Set {
		StoriesIds ids;
		int total = -1;
		StoryId lastId = 0;
		bool loaded = false;
		mtpRequestId requestId = 0;
	};

	struct PollingSettings {
		int chat = 0;
		int viewer = 0;
	};

	void parseAndApply(const MTPPeerStories &stories);
	[[nodiscard]] Story *parseAndApply(
		not_null<PeerData*> peer,
		const MTPDstoryItem &data,
		TimeId now);
	StoryIdDates parseAndApply(
		not_null<PeerData*> peer,
		const MTPstoryItem &story,
		TimeId now);
	void processResolvedStories(
		not_null<PeerData*> peer,
		const QVector<MTPStoryItem> &list);
	void sendResolveRequests();
	void finalizeResolve(FullStoryId id);
	void updatePeerStoriesState(not_null<PeerData*> peer);

	[[nodiscard]] Set *lookupArchive(not_null<PeerData*> peer);
	void clearArchive(not_null<PeerData*> peer);

	void applyDeleted(not_null<PeerData*> peer, StoryId id);
	void applyExpired(FullStoryId id);
	void applyRemovedFromActive(FullStoryId id);
	void applyDeletedFromSources(PeerId id, StorySourcesList list);
	void removeDependencyStory(not_null<Story*> story);
	void sort(StorySourcesList list);
	bool bumpReadTill(PeerId peerId, StoryId maxReadTill);
	void requestReadTills();

	void sendMarkAsReadRequests();
	void sendMarkAsReadRequest(not_null<PeerData*> peer, StoryId tillId);
	void sendIncrementViewsRequests();
	void checkQuitPreventFinished();

	void registerExpiring(TimeId expires, FullStoryId id);
	void scheduleExpireTimer();
	void processExpired();

	void preloadSourcesChanged(StorySourcesList list);
	bool rebuildPreloadSources(StorySourcesList list);
	void continuePreloading();
	[[nodiscard]] bool shouldContinuePreload(FullStoryId id) const;
	[[nodiscard]] FullStoryId nextPreloadId() const;
	void startPreloading(not_null<Story*> story);
	void preloadFinished(FullStoryId id, bool markAsPreloaded = false);
	void preloadListsMore();

	void notifySourcesChanged(StorySourcesList list);
	void pushHiddenCountsToFolder();

	[[nodiscard]] int pollingInterval(
		const PollingSettings &settings) const;
	void maybeSchedulePolling(
		not_null<Story*> story,
		const PollingSettings &settings,
		TimeId now);
	void sendPollingRequests();
	void sendPollingViewsRequests();
	void sendViewsSliceRequest();
	void sendViewsCountsRequest();

	const not_null<Session*> _owner;
	std::unordered_map<
		PeerId,
		base::flat_map<StoryId, std::unique_ptr<Story>>> _stories;
	base::flat_map<FullStoryId, std::unique_ptr<Story>> _deletingStories;
	std::unordered_map<
		PeerId,
		base::flat_map<StoryId, std::weak_ptr<HistoryItem>>> _items;
	base::flat_multi_map<TimeId, FullStoryId> _expiring;
	base::flat_set<PeerId> _peersWithDeletedStories;
	base::flat_set<FullStoryId> _deleted;
	base::Timer _expireTimer;
	bool _expireSchedulePosted = false;

	base::flat_map<
		PeerId,
		base::flat_map<StoryId, std::vector<Fn<void()>>>> _resolvePending;
	base::flat_map<
		PeerId,
		base::flat_map<StoryId, std::vector<Fn<void()>>>> _resolveSent;

	std::unordered_map<
		not_null<Data::Story*>,
		base::flat_set<not_null<HistoryItem*>>> _dependentMessages;

	std::unordered_map<PeerId, StoriesSource> _all;
	std::vector<StoriesSourceInfo> _sources[kStorySourcesListCount];
	rpl::event_stream<> _sourcesChanged[kStorySourcesListCount];
	bool _sourcesLoaded[kStorySourcesListCount] = { false };
	QString _sourcesStates[kStorySourcesListCount];
	Folder *_folderForHidden = nullptr;

	mtpRequestId _loadMoreRequestId[kStorySourcesListCount] = { 0 };

	rpl::event_stream<PeerId> _sourceChanged;
	rpl::event_stream<PeerId> _itemsChanged;

	std::unordered_map<PeerId, Set> _archive;
	rpl::event_stream<PeerId> _archiveChanged;

	std::unordered_map<PeerId, Set> _saved;
	rpl::event_stream<PeerId> _savedChanged;

	base::flat_set<PeerId> _markReadPending;
	base::Timer _markReadTimer;
	base::flat_set<PeerId> _markReadRequests;
	base::flat_map<
		not_null<PeerData*>,
		std::vector<Fn<void()>>> _requestingPeerStories;

	base::flat_map<PeerId, base::flat_set<StoryId>> _incrementViewsPending;
	base::Timer _incrementViewsTimer;
	base::flat_set<PeerId> _incrementViewsRequests;

	PeerData *_viewsStoryPeer = nullptr;
	StoryId _viewsStoryId = 0;
	QString _viewsOffset;
	Fn<void(StoryViews)> _viewsDone;
	mtpRequestId _viewsRequestId = 0;

	base::flat_set<FullStoryId> _preloaded;
	std::vector<FullStoryId> _toPreloadSources[kStorySourcesListCount];
	std::vector<FullStoryId> _toPreloadViewer;
	std::unique_ptr<StoryPreload> _preloading;
	int _preloadingHiddenSourcesCounter = 0;
	int _preloadingMainSourcesCounter = 0;

	base::flat_map<PeerId, StoryId> _readTill;
	base::flat_set<FullStoryId> _pendingReadTillItems;
	base::flat_map<not_null<PeerData*>, StoryId> _pendingPeerStateMaxId;
	mtpRequestId _readTillsRequestId = 0;
	bool _readTillReceived = false;

	base::flat_map<not_null<Story*>, PollingSettings> _pollingSettings;
	base::flat_set<not_null<Story*>> _pollingViews;
	base::Timer _pollingTimer;
	base::Timer _pollingViewsTimer;

	rpl::variable<StealthMode> _stealthMode;

	rpl::lifetime _lifetime;

};

} // namespace Data
