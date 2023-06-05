/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/expected.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

class Image;
class PhotoData;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

struct StoryIdDates {
	StoryId id = 0;
	TimeId date = 0;
	TimeId expires = 0;

	[[nodiscard]] bool valid() const {
		return id != 0;
	}
	explicit operator bool() const {
		return valid();
	}

	friend inline auto operator<=>(StoryIdDates, StoryIdDates) = default;
	friend inline bool operator==(StoryIdDates, StoryIdDates) = default;
};

struct StoryMedia {
	std::variant<not_null<PhotoData*>, not_null<DocumentData*>> data;

	friend inline bool operator==(StoryMedia, StoryMedia) = default;
};

struct StoryView {
	not_null<PeerData*> peer;
	TimeId date = 0;

	friend inline bool operator==(StoryView, StoryView) = default;
};

class Story final {
public:
	Story(
		StoryId id,
		not_null<PeerData*> peer,
		StoryMedia media,
		TimeId date,
		TimeId expires);

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<PeerData*> peer() const;

	[[nodiscard]] StoryId id() const;
	[[nodiscard]] bool mine() const;
	[[nodiscard]] StoryIdDates idDates() const;
	[[nodiscard]] FullStoryId fullId() const;
	[[nodiscard]] TimeId date() const;
	[[nodiscard]] TimeId expires() const;
	[[nodiscard]] bool expired(TimeId now = 0) const;
	[[nodiscard]] const StoryMedia &media() const;
	[[nodiscard]] PhotoData *photo() const;
	[[nodiscard]] DocumentData *document() const;

	[[nodiscard]] bool hasReplyPreview() const;
	[[nodiscard]] Image *replyPreview() const;
	[[nodiscard]] TextWithEntities inReplyText() const;

	void setPinned(bool pinned);
	[[nodiscard]] bool pinned() const;

	void setCaption(TextWithEntities &&caption);
	[[nodiscard]] const TextWithEntities &caption() const;

	void setViewsData(std::vector<not_null<PeerData*>> recent, int total);
	[[nodiscard]] auto recentViewers() const
		-> const std::vector<not_null<PeerData*>> &;
	[[nodiscard]] const std::vector<StoryView> &viewsList() const;
	[[nodiscard]] int views() const;
	void applyViewsSlice(
		const std::optional<StoryView> &offset,
		const std::vector<StoryView> &slice,
		int total);

	bool applyChanges(StoryMedia media, const MTPDstoryItem &data);

private:
	const StoryId _id = 0;
	const not_null<PeerData*> _peer;
	StoryMedia _media;
	TextWithEntities _caption;
	std::vector<not_null<PeerData*>> _recentViewers;
	std::vector<StoryView> _viewsList;
	int _views = 0;
	const TimeId _date = 0;
	const TimeId _expires = 0;
	bool _pinned = false;

};

struct StoriesSourceInfo {
	PeerId id = 0;
	TimeId last = 0;
	bool unread = false;
	bool premium = false;
	bool hidden = false;

	friend inline bool operator==(
		StoriesSourceInfo,
		StoriesSourceInfo) = default;
};

struct StoriesSource {
	not_null<UserData*> user;
	base::flat_set<StoryIdDates> ids;
	StoryId readTill = 0;
	bool hidden = false;

	[[nodiscard]] StoriesSourceInfo info() const;
	[[nodiscard]] bool unread() const;

	friend inline bool operator==(StoriesSource, StoriesSource) = default;
};

enum class NoStory : uchar {
	Unknown,
	Deleted,
};

enum class StorySourcesList : uchar {
	NotHidden,
	All,
};

struct StoriesContextSingle {
};

struct StoriesContextPeer {
};

struct StoriesContextSaved {
};

struct StoriesContextArchive {
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

inline constexpr auto kStorySourcesListCount = 2;

class Stories final : public base::has_weak_ptr {
public:
	explicit Stories(not_null<Session*> owner);
	~Stories();

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
	void loadAround(FullStoryId id, StoriesContext context);

	[[nodiscard]] const base::flat_map<PeerId, StoriesSource> &all() const;
	[[nodiscard]] const std::vector<StoriesSourceInfo> &sources(
		StorySourcesList list) const;
	[[nodiscard]] bool sourcesLoaded(StorySourcesList list) const;
	[[nodiscard]] rpl::producer<> sourcesChanged(
		StorySourcesList list) const;
	[[nodiscard]] rpl::producer<PeerId> itemsChanged() const;

	[[nodiscard]] base::expected<not_null<Story*>, NoStory> lookup(
		FullStoryId id) const;
	void resolve(FullStoryId id, Fn<void()> done);

	[[nodiscard]] bool isQuitPrevent();
	void markAsRead(FullStoryId id, bool viewed);

	void toggleHidden(PeerId peerId, bool hidden);

	static constexpr auto kViewsPerPage = 50;
	void loadViewsSlice(
		StoryId id,
		std::optional<StoryView> offset,
		Fn<void(std::vector<StoryView>)> done);

	[[nodiscard]] const base::flat_set<StoryId> &expiredMine() const;
	[[nodiscard]] rpl::producer<> expiredMineChanged() const;
	[[nodiscard]] int expiredMineCount() const;
	[[nodiscard]] bool expiredMineCountKnown() const;
	[[nodiscard]] bool expiredMineLoaded() const;
	[[nodiscard]] void expiredMineLoadMore();

private:
	void parseAndApply(const MTPUserStories &stories);
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

	void applyDeleted(FullStoryId id);
	void applyExpired(FullStoryId id);
	void applyRemovedFromActive(FullStoryId id);
	void applyDeletedFromSources(PeerId id, StorySourcesList list);
	void removeDependencyStory(not_null<Story*> story);
	void sort(StorySourcesList list);

	void addToExpiredMine(not_null<Story*> story);

	void sendMarkAsReadRequests();
	void sendMarkAsReadRequest(not_null<PeerData*> peer, StoryId tillId);

	void requestUserStories(not_null<UserData*> user);
	void registerExpiring(TimeId expires, FullStoryId id);
	void scheduleExpireTimer();
	void processExpired();

	const not_null<Session*> _owner;
	base::flat_map<
		PeerId,
		base::flat_map<StoryId, std::unique_ptr<Story>>> _stories;
	base::flat_multi_map<TimeId, FullStoryId> _expiring;
	base::flat_set<FullStoryId> _deleted;
	base::Timer _expireTimer;
	bool _expireSchedulePosted = false;

	base::flat_map<
		PeerId,
		base::flat_map<StoryId, std::vector<Fn<void()>>>> _resolvePending;
	base::flat_map<
		PeerId,
		base::flat_map<StoryId, std::vector<Fn<void()>>>> _resolveSent;

	std::map<
		not_null<Data::Story*>,
		base::flat_set<not_null<HistoryItem*>>> _dependentMessages;

	base::flat_map<PeerId, StoriesSource> _all;
	std::vector<StoriesSourceInfo> _sources[kStorySourcesListCount];
	rpl::event_stream<> _sourcesChanged[kStorySourcesListCount];
	bool _sourcesLoaded[kStorySourcesListCount] = { false };
	QString _sourcesStates[kStorySourcesListCount];

	mtpRequestId _loadMoreRequestId[kStorySourcesListCount] = { 0 };

	rpl::event_stream<PeerId> _itemsChanged;

	base::flat_set<StoryId> _expiredMine;
	int _expiredMineTotal = -1;
	StoryId _expiredMineLastId = 0;
	bool _expiredMineLoaded = false;
	rpl::event_stream<> _expiredMineChanged;
	mtpRequestId _expiredMineRequestId = 0;

	base::flat_set<PeerId> _markReadPending;
	base::Timer _markReadTimer;
	base::flat_set<PeerId> _markReadRequests;
	base::flat_set<not_null<UserData*>> _requestingUserStories;

	StoryId _viewsStoryId = 0;
	std::optional<StoryView> _viewsOffset;
	Fn<void(std::vector<StoryView>)> _viewsDone;
	mtpRequestId _viewsRequestId = 0;

};

} // namespace Data
