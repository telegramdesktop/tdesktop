/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
struct EmojiGroup;
} // namespace Ui

namespace Data {

class DocumentMedia;
class Session;
struct UniqueGift;

struct EmojiStatusCollectible {
	CollectibleId id = 0;
	DocumentId documentId = 0;
	QString title;
	QString slug;
	DocumentId patternDocumentId = 0;
	QColor centerColor;
	QColor edgeColor;
	QColor patternColor;
	QColor textColor;

	explicit operator bool() const {
		return id != 0;
	}
};
struct EmojiStatusData {
	EmojiStatusId id;
	TimeId until = 0;
};

class EmojiStatuses final {
public:
	explicit EmojiStatuses(not_null<Session*> owner);
	~EmojiStatuses();

	[[nodiscard]] Session &owner() const {
		return *_owner;
	}
	[[nodiscard]] Main::Session &session() const;

	void refreshRecent();
	void refreshRecentDelayed();
	void refreshDefault();
	void refreshColored();
	void refreshChannelDefault();
	void refreshChannelColored();
	void refreshCollectibles();

	enum class Type {
		Recent,
		Default,
		Colored,
		ChannelDefault,
		ChannelColored,
		Collectibles,
	};
	[[nodiscard]] const std::vector<EmojiStatusId> &list(Type type) const;

	[[nodiscard]] EmojiStatusData parse(const MTPEmojiStatus &status);

	[[nodiscard]] rpl::producer<> recentUpdates() const;
	[[nodiscard]] rpl::producer<> defaultUpdates() const;
	[[nodiscard]] rpl::producer<> channelDefaultUpdates() const;
	[[nodiscard]] rpl::producer<> collectiblesUpdates() const;

	void set(EmojiStatusId id, TimeId until = 0);
	void set(not_null<PeerData*> peer, EmojiStatusId id, TimeId until = 0);
	[[nodiscard]] EmojiStatusId fromUniqueGift(const Data::UniqueGift &gift);
	[[nodiscard]] EmojiStatusCollectible *collectibleInfo(CollectibleId id);

	void registerAutomaticClear(not_null<PeerData*> peer, TimeId until);

	using Groups = std::vector<Ui::EmojiGroup>;
	[[nodiscard]] rpl::producer<Groups> emojiGroupsValue() const;
	[[nodiscard]] rpl::producer<Groups> statusGroupsValue() const;
	[[nodiscard]] rpl::producer<Groups> stickerGroupsValue() const;
	[[nodiscard]] rpl::producer<Groups> profilePhotoGroupsValue() const;
	void requestEmojiGroups();
	void requestStatusGroups();
	void requestStickerGroups();
	void requestProfilePhotoGroups();

private:
	struct GroupsType {
		rpl::variable<Groups> data;
		mtpRequestId requestId = 0;
		int32 hash = 0;
	};

	void requestRecent();
	void requestDefault();
	void requestColored();
	void requestChannelDefault();
	void requestChannelColored();
	void requestCollectibles();

	void updateRecent(const MTPDaccount_emojiStatuses &data);
	void updateDefault(const MTPDaccount_emojiStatuses &data);
	void updateColored(const MTPDmessages_stickerSet &data);
	void updateChannelDefault(const MTPDaccount_emojiStatuses &data);
	void updateChannelColored(const MTPDmessages_stickerSet &data);
	void updateCollectibles(const MTPDaccount_emojiStatuses &data);

	void processClearingIn(TimeId wait);
	void processClearing();

	[[nodiscard]] std::vector<EmojiStatusId> parse(
		const MTPDaccount_emojiStatuses &data);

	template <typename Request>
	void requestGroups(not_null<GroupsType*> type, Request &&request);

	const not_null<Session*> _owner;

	std::vector<EmojiStatusId> _recent;
	std::vector<EmojiStatusId> _default;
	std::vector<EmojiStatusId> _colored;
	std::vector<EmojiStatusId> _channelDefault;
	std::vector<EmojiStatusId> _channelColored;
	std::vector<EmojiStatusId> _collectibles;
	rpl::event_stream<> _recentUpdated;
	rpl::event_stream<> _defaultUpdated;
	rpl::event_stream<> _coloredUpdated;
	rpl::event_stream<> _channelDefaultUpdated;
	rpl::event_stream<> _channelColoredUpdated;
	rpl::event_stream<> _collectiblesUpdated;

	base::flat_map<
		CollectibleId,
		std::shared_ptr<EmojiStatusCollectible>> _collectibleData;

	mtpRequestId _recentRequestId = 0;
	bool _recentRequestScheduled = false;
	uint64 _recentHash = 0;

	mtpRequestId _defaultRequestId = 0;
	uint64 _defaultHash = 0;

	mtpRequestId _coloredRequestId = 0;

	mtpRequestId _channelDefaultRequestId = 0;
	uint64 _channelDefaultHash = 0;

	mtpRequestId _channelColoredRequestId = 0;

	mtpRequestId _collectiblesRequestId = 0;
	uint64 _collectiblesHash = 0;

	base::flat_map<not_null<PeerData*>, mtpRequestId> _sentRequests;

	base::flat_map<not_null<PeerData*>, TimeId> _clearing;
	base::Timer _clearingTimer;

	GroupsType _emojiGroups;
	GroupsType _statusGroups;
	GroupsType _stickerGroups;
	GroupsType _profilePhotoGroups;

	rpl::lifetime _lifetime;

};

} // namespace Data
