/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PhotoData;
class DocumentData;

namespace Data {

class Session;

struct StoryPrivacy {
	friend inline bool operator==(StoryPrivacy, StoryPrivacy) = default;
};

struct StoryMedia {
	std::variant<not_null<PhotoData*>, not_null<DocumentData*>> data;

	friend inline bool operator==(StoryMedia, StoryMedia) = default;
};

struct StoryItem {
	StoryId id = 0;
	StoryMedia media;
	TextWithEntities caption;
	TimeId date = 0;
	StoryPrivacy privacy;

	friend inline bool operator==(StoryItem, StoryItem) = default;
};

struct StoriesList {
	not_null<UserData*> user;
	std::vector<StoryItem> items;
	int total = 0;

	friend inline bool operator==(StoriesList, StoriesList) = default;
};

struct FullStoryId {
	UserData *user = nullptr;
	StoryId id = 0;

	explicit operator bool() const {
		return user != nullptr && id != 0;
	}
	friend inline auto operator<=>(FullStoryId, FullStoryId) = default;
	friend inline bool operator==(FullStoryId, FullStoryId) = default;
};

class Stories final {
public:
	explicit Stories(not_null<Session*> owner);
	~Stories();

	void loadMore();
	void apply(const MTPDupdateStories &data);

	[[nodiscard]] const std::vector<StoriesList> &all();
	[[nodiscard]] bool allLoaded() const;

	// #TODO stories testing
	[[nodiscard]] StoryId generate(
		not_null<HistoryItem*> item,
		std::variant<
			v::null_t,
			not_null<PhotoData*>,
			not_null<DocumentData*>> media);

private:
	[[nodiscard]] StoriesList parse(const MTPUserStories &data);
	[[nodiscard]] std::optional<StoryItem> parse(const MTPDstoryItem &data);

	void pushToBack(StoriesList &&list);
	void pushToFront(StoriesList &&list);

	const not_null<Session*> _owner;

	std::vector<StoriesList> _all;
	QString _state;
	bool _allLoaded = false;

	mtpRequestId _loadMoreRequestId = 0;

};

} // namespace Data
