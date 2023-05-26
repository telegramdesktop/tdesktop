/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/expected.h"

class Image;
class PhotoData;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

struct StoryMedia {
	std::variant<not_null<PhotoData*>, not_null<DocumentData*>> data;

	friend inline bool operator==(StoryMedia, StoryMedia) = default;
};

class Story {
public:
	Story(
		StoryId id,
		not_null<PeerData*> peer,
		StoryMedia media,
		TimeId date);

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<PeerData*> peer() const;

	[[nodiscard]] StoryId id() const;
	[[nodiscard]] FullStoryId fullId() const;
	[[nodiscard]] TimeId date() const;
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

	void apply(const MTPDstoryItem &data);

private:
	const StoryId _id = 0;
	const not_null<PeerData*> _peer;
	StoryMedia _media;
	TextWithEntities _caption;
	const TimeId _date = 0;
	bool _pinned = false;

};

struct StoriesList {
	not_null<UserData*> user;
	std::vector<StoryId> ids;
	StoryId readTill = 0;
	int total = 0;

	[[nodiscard]] bool unread() const;

	friend inline bool operator==(StoriesList, StoriesList) = default;
};

enum class NoStory : uchar {
	Unknown,
	Deleted,
};

class Stories final {
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

	void loadMore();
	void apply(const MTPDupdateStories &data);

	[[nodiscard]] const std::vector<StoriesList> &all();
	[[nodiscard]] bool allLoaded() const;
	[[nodiscard]] rpl::producer<> allChanged() const;

	[[nodiscard]] base::expected<not_null<Story*>, NoStory> lookup(
		FullStoryId id) const;
	void resolve(FullStoryId id, Fn<void()> done);

private:
	[[nodiscard]] StoriesList parse(const MTPUserStories &stories);
	[[nodiscard]] Story *parse(
		not_null<PeerData*> peer,
		const MTPDstoryItem &data);
	void processResolvedStories(
		not_null<PeerData*> peer,
		const QVector<MTPStoryItem> &list);
	void sendResolveRequests();
	void finalizeResolve(FullStoryId id);

	void pushToBack(StoriesList &&list);
	void pushToFront(StoriesList &&list);
	void applyDeleted(FullStoryId id);

	const not_null<Session*> _owner;
	base::flat_map<
		PeerId,
		base::flat_map<StoryId, std::unique_ptr<Story>>> _stories;
	base::flat_set<FullStoryId> _deleted;

	base::flat_map<
		PeerId,
		base::flat_map<StoryId, std::vector<Fn<void()>>>> _resolves;
	base::flat_map<mtpRequestId, std::vector<Fn<void()>>> _resolveRequests;

	std::map<
		not_null<Data::Story*>,
		base::flat_set<not_null<HistoryItem*>>> _dependentMessages;

	std::vector<StoriesList> _all;
	rpl::event_stream<> _allChanged;
	QString _state;
	bool _allLoaded = false;

	mtpRequestId _loadMoreRequestId = 0;

};

} // namespace Data
