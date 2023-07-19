/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class Image;
class PhotoData;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class Thread;
class PhotoMedia;

enum class StoryPrivacy : uchar {
	Public,
	CloseFriends,
	Contacts,
	SelectedContacts,
	Other,
};

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
	std::variant<
		v::null_t,
		not_null<PhotoData*>,
		not_null<DocumentData*>> data;

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
		const MTPDstoryItem &data,
		TimeId now);

	static constexpr int kRecentViewersMax = 3;

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<PeerData*> peer() const;

	[[nodiscard]] StoryId id() const;
	[[nodiscard]] bool mine() const;
	[[nodiscard]] StoryIdDates idDates() const;
	[[nodiscard]] FullStoryId fullId() const;
	[[nodiscard]] TimeId date() const;
	[[nodiscard]] TimeId expires() const;
	[[nodiscard]] bool unsupported() const;
	[[nodiscard]] bool expired(TimeId now = 0) const;
	[[nodiscard]] const StoryMedia &media() const;
	[[nodiscard]] PhotoData *photo() const;
	[[nodiscard]] DocumentData *document() const;

	[[nodiscard]] bool hasReplyPreview() const;
	[[nodiscard]] Image *replyPreview() const;
	[[nodiscard]] TextWithEntities inReplyText() const;

	void setPinned(bool pinned);
	[[nodiscard]] bool pinned() const;
	[[nodiscard]] StoryPrivacy privacy() const;
	[[nodiscard]] bool forbidsForward() const;
	[[nodiscard]] bool edited() const;

	[[nodiscard]] bool canDownload() const;
	[[nodiscard]] bool canShare() const;
	[[nodiscard]] bool canDelete() const;
	[[nodiscard]] bool canReport() const;

	[[nodiscard]] bool hasDirectLink() const;
	[[nodiscard]] std::optional<QString> errorTextForForward(
		not_null<Thread*> to) const;

	void setCaption(TextWithEntities &&caption);
	[[nodiscard]] const TextWithEntities &caption() const;

	[[nodiscard]] auto recentViewers() const
		-> const std::vector<not_null<PeerData*>> &;
	[[nodiscard]] const std::vector<StoryView> &viewsList() const;
	[[nodiscard]] int views() const;
	void applyViewsSlice(
		const std::optional<StoryView> &offset,
		const std::vector<StoryView> &slice,
		int total);

	void applyChanges(
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now);
	[[nodiscard]] TimeId lastUpdateTime() const;

private:
	void applyFields(
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now,
		bool initial);

	const StoryId _id = 0;
	const not_null<PeerData*> _peer;
	StoryMedia _media;
	TextWithEntities _caption;
	std::vector<not_null<PeerData*>> _recentViewers;
	std::vector<StoryView> _viewsList;
	int _views = 0;
	const TimeId _date = 0;
	const TimeId _expires = 0;
	TimeId _lastUpdateTime = 0;
	bool _pinned : 1 = false;
	bool _privacyPublic : 1 = false;
	bool _privacyCloseFriends : 1 = false;
	bool _privacyContacts : 1 = false;
	bool _privacySelectedContacts : 1 = false;
	bool _noForwards : 1 = false;
	bool _edited : 1 = false;

};

class StoryPreload final : public base::has_weak_ptr {
public:
	StoryPreload(not_null<Story*> story, Fn<void()> done);
	~StoryPreload();

	[[nodiscard]] FullStoryId id() const;
	[[nodiscard]] not_null<Story*> story() const;

private:
	class LoadTask;

	void start();
	void load();
	void callDone();

	const not_null<Story*> _story;
	Fn<void()> _done;

	std::shared_ptr<Data::PhotoMedia> _photo;
	std::unique_ptr<LoadTask> _task;
	rpl::lifetime _lifetime;

};

} // namespace Data
