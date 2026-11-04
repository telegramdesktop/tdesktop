/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_story_drafts.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct StorySourceSend {
	int option = 0;
	std::vector<PeerId> excludedEveryone;
	std::vector<PeerId> excludedContacts;
	std::vector<PeerId> selected;
	bool pinned = true;
	bool allowForwards = true;
	std::vector<int> albums;
	PeerId postTo = 0;

	friend inline bool operator==(
		const StorySourceSend &,
		const StorySourceSend &) = default;
};

struct StorySource {
	uint64 pendingId = 0;
	PeerId peer = 0;
	StoryId storyId = 0;
	TimeId expiresAt = 0;
	PhotoId photoId = 0;
	DocumentId documentId = 0;
	uint64 fingerprint = 0;
	StoryDraft draft;
	StorySourceSend send;
};

struct StorySourceSave {
	StoryDraft draft;
	StoryDraftMedia media;
	uint64 fingerprint = 0;
};

class StoryEditSources final {
public:
	explicit StoryEditSources(not_null<Main::Session*> session);

	void savePending(
		uint64 pendingId,
		const StorySourceSave &save,
		const StorySourceSend &send);
	void finalize(uint64 pendingId, StoryId storyId, TimeId expiresAt);
	void removePending(uint64 pendingId);
	void updateSendState(
		PeerId peer,
		StoryId storyId,
		const StorySourceSend &send);
	[[nodiscard]] StorySource find(
		PeerId peer,
		StoryId storyId,
		PhotoId photoId,
		DocumentId documentId);
	[[nodiscard]] uint64 fingerprint(PeerId peer, StoryId storyId);
	void removeExpired();
	void clear();

private:
	void load();
	void write();
	void removeFiles(const StorySource &source);
	[[nodiscard]] QString folder() const;
	[[nodiscard]] QString storeFile(
		uint64 pendingId,
		int index,
		const StoryDraftSceneFile &file,
		const QString &mediaPath) const;

	const not_null<Main::Session*> _session;
	std::vector<StorySource> _list;
	bool _loaded = false;

};

} // namespace Data
