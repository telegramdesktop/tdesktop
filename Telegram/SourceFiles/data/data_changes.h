/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "data/data_chat_participant_status.h"

class History;
class PeerData;
class HistoryItem;

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Data::details {

template <typename Flag>
inline constexpr int CountBit(Flag Last = Flag::LastUsedBit) {
	auto i = 0;
	while ((1ULL << i) != static_cast<uint64>(Last)) {
		++i;
		Assert(i != 64);
	}
	return i;
}

} // namespace Data::details

namespace Data {

class ForumTopic;
class SavedSublist;
class Story;

struct NameUpdate {
	NameUpdate(
		not_null<PeerData*> peer,
		base::flat_set<QChar> oldFirstLetters)
	: peer(peer)
	, oldFirstLetters(std::move(oldFirstLetters)) {
	}

	not_null<PeerData*> peer;
	base::flat_set<QChar> oldFirstLetters;
};

struct PeerUpdate {
	enum class Flag : uint64 {
		None = 0,

		// Common flags
		Name                = (1ULL << 0),
		Username            = (1ULL << 1),
		Photo               = (1ULL << 2),
		About               = (1ULL << 3),
		Notifications       = (1ULL << 4),
		Migration           = (1ULL << 5),
		UnavailableReason   = (1ULL << 6),
		ChatThemeToken      = (1ULL << 7),
		ChatWallPaper       = (1ULL << 8),
		IsBlocked           = (1ULL << 9),
		MessagesTTL         = (1ULL << 10),
		FullInfo            = (1ULL << 11),
		Usernames           = (1ULL << 12),
		TranslationDisabled = (1ULL << 13),
		Color               = (1ULL << 14),
		BackgroundEmoji     = (1ULL << 15),
		StoriesState        = (1ULL << 16),
		VerifyInfo          = (1ULL << 17),
		StarsPerMessage     = (1ULL << 18),

		// For users
		CanShareContact     = (1ULL << 19),
		IsContact           = (1ULL << 20),
		PhoneNumber         = (1ULL << 21),
		OnlineStatus        = (1ULL << 22),
		BotCommands         = (1ULL << 23),
		BotCanBeInvited     = (1ULL << 24),
		BotStartToken       = (1ULL << 25),
		CommonChats         = (1ULL << 26),
		PeerGifts           = (1ULL << 27),
		HasCalls            = (1ULL << 28),
		SupportInfo         = (1ULL << 29),
		IsBot               = (1ULL << 30),
		EmojiStatus         = (1ULL << 31),
		BusinessDetails     = (1ULL << 32),
		Birthday            = (1ULL << 33),
		PersonalChannel     = (1ULL << 34),
		StarRefProgram      = (1ULL << 35),
		PaysPerMessage      = (1ULL << 36),
		GiftSettings        = (1ULL << 37),
		StarsRating         = (1ULL << 38),
		ContactNote         = (1ULL << 39),

		// For chats and channels
		InviteLinks         = (1ULL << 40),
		Members             = (1ULL << 41),
		Admins              = (1ULL << 42),
		BannedUsers         = (1ULL << 43),
		Rights              = (1ULL << 44),
		PendingRequests     = (1ULL << 45),
		Reactions           = (1ULL << 46),

		// For channels
		ChannelAmIn         = (1ULL << 47),
		StickersSet         = (1ULL << 48),
		EmojiSet            = (1ULL << 49),
		DiscussionLink      = (1ULL << 50),
		MonoforumLink       = (1ULL << 51),
		ChannelLocation     = (1ULL << 52),
		Slowmode            = (1ULL << 53),
		GroupCall           = (1ULL << 54),

		// For iteration
		LastUsedBit         = (1ULL << 54),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<PeerData*> peer;
	Flags flags = 0;

};

struct HistoryUpdate {
	enum class Flag : uint32 {
		None = 0,

		IsPinned           = (1U << 0),
		UnreadView         = (1U << 1),
		TopPromoted        = (1U << 2),
		Folder             = (1U << 3),
		UnreadMentions     = (1U << 4),
		UnreadReactions    = (1U << 5),
		ClientSideMessages = (1U << 6),
		ChatOccupied       = (1U << 7),
		MessageSent        = (1U << 8),
		ScheduledSent      = (1U << 9),
		OutboxRead         = (1U << 10),
		BotKeyboard        = (1U << 11),
		CloudDraft         = (1U << 12),
		TranslateFrom      = (1U << 13),
		TranslatedTo       = (1U << 14),

		LastUsedBit        = (1U << 14),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<History*> history;
	Flags flags = 0;

};

struct TopicUpdate {
	enum class Flag : uint32 {
		None = 0,

		UnreadView      = (1U << 1),
		UnreadMentions  = (1U << 2),
		UnreadReactions = (1U << 3),
		Notifications   = (1U << 4),
		Title           = (1U << 5),
		IconId          = (1U << 6),
		ColorId         = (1U << 7),
		CloudDraft      = (1U << 8),
		Closed          = (1U << 9),
		Creator         = (1U << 10),
		Destroyed       = (1U << 11),

		LastUsedBit     = (1U << 11),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<ForumTopic*> topic;
	Flags flags = 0;

};

struct SublistUpdate {
	enum class Flag : uint32 {
		None = 0,

		UnreadView = (1U << 1),
		UnreadReactions = (1U << 2),
		CloudDraft = (1U << 3),
		Destroyed = (1U << 4),

		LastUsedBit = (1U << 4),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<SavedSublist*> sublist;
	Flags flags = 0;

};

struct MessageUpdate {
	enum class Flag : uint32 {
		None = 0,

		Edited             = (1U << 0),
		Destroyed          = (1U << 1),
		DialogRowRepaint   = (1U << 2),
		DialogRowRefresh   = (1U << 3),
		NewAdded           = (1U << 4),
		ReplyMarkup        = (1U << 5),
		BotCallbackSent    = (1U << 6),
		NewMaybeAdded      = (1U << 7),
		ReplyToTopAdded    = (1U << 8),
		NewUnreadReaction  = (1U << 9),

		LastUsedBit        = (1U << 9),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<HistoryItem*> item;
	Flags flags = 0;

};

struct EntryUpdate {
	enum class Flag : uint32 {
		None = 0,

		Repaint           = (1U << 0),
		HasPinnedMessages = (1U << 1),
		ForwardDraft      = (1U << 2),
		LocalDraftSet     = (1U << 3),
		Height            = (1U << 4),
		Destroyed         = (1U << 5),

		LastUsedBit       = (1U << 5),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<Dialogs::Entry*> entry;
	Flags flags = 0;

};

struct StoryUpdate {
	enum class Flag : uint32 {
		None = 0,

		Edited       = (1U << 0),
		Destroyed    = (1U << 1),
		NewAdded     = (1U << 2),
		ViewsChanged = (1U << 3),
		MarkRead     = (1U << 4),
		Reaction     = (1U << 5),

		LastUsedBit  = (1U << 5),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<Story*> story;
	Flags flags = 0;

};

struct ChatAdminChange {
	not_null<PeerData*> peer;
	not_null<UserData*> user;
	ChatAdminRights rights;
	QString rank;
};

class Changes final {
public:
	explicit Changes(not_null<Main::Session*> session);

	[[nodiscard]] Main::Session &session() const;

	void nameUpdated(
		not_null<PeerData*> peer,
		base::flat_set<QChar> oldFirstLetters);
	[[nodiscard]] rpl::producer<NameUpdate> realtimeNameUpdates() const;
	[[nodiscard]] rpl::producer<NameUpdate> realtimeNameUpdates(
		not_null<PeerData*> peer) const;

	void peerUpdated(not_null<PeerData*> peer, PeerUpdate::Flags flags);
	[[nodiscard]] rpl::producer<PeerUpdate> peerUpdates(
		PeerUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<PeerUpdate> peerUpdates(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<PeerUpdate> peerFlagsValue(
		not_null<PeerData*> peer,
		PeerUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<PeerUpdate> realtimePeerUpdates(
		PeerUpdate::Flag flag) const;

	void historyUpdated(
		not_null<History*> history,
		HistoryUpdate::Flags flags);
	[[nodiscard]] rpl::producer<HistoryUpdate> historyUpdates(
		HistoryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<HistoryUpdate> historyUpdates(
		not_null<History*> history,
		HistoryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<HistoryUpdate> historyFlagsValue(
		not_null<History*> history,
		HistoryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<HistoryUpdate> realtimeHistoryUpdates(
		HistoryUpdate::Flag flag) const;

	void topicUpdated(
		not_null<ForumTopic*> topic,
		TopicUpdate::Flags flags);
	[[nodiscard]] rpl::producer<TopicUpdate> topicUpdates(
		TopicUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<TopicUpdate> topicUpdates(
		not_null<ForumTopic*> topic,
		TopicUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<TopicUpdate> topicFlagsValue(
		not_null<ForumTopic*> topic,
		TopicUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<TopicUpdate> realtimeTopicUpdates(
		TopicUpdate::Flag flag) const;
	void topicRemoved(not_null<ForumTopic*> topic);

	void sublistUpdated(
		not_null<SavedSublist*> sublist,
		SublistUpdate::Flags flags);
	[[nodiscard]] rpl::producer<SublistUpdate> sublistUpdates(
		SublistUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<SublistUpdate> sublistUpdates(
		not_null<SavedSublist*> sublist,
		SublistUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<SublistUpdate> sublistFlagsValue(
		not_null<SavedSublist*> sublist,
		SublistUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<SublistUpdate> realtimeSublistUpdates(
		SublistUpdate::Flag flag) const;
	void sublistRemoved(not_null<SavedSublist*> sublist);

	void messageUpdated(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags);
	[[nodiscard]] rpl::producer<MessageUpdate> messageUpdates(
		MessageUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<MessageUpdate> messageUpdates(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<MessageUpdate> messageFlagsValue(
		not_null<HistoryItem*> item,
		MessageUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<MessageUpdate> realtimeMessageUpdates(
		MessageUpdate::Flag flag) const;

	void entryUpdated(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags);
	[[nodiscard]] rpl::producer<EntryUpdate> entryUpdates(
		EntryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<EntryUpdate> entryUpdates(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<EntryUpdate> entryFlagsValue(
		not_null<Dialogs::Entry*> entry,
		EntryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<EntryUpdate> realtimeEntryUpdates(
		EntryUpdate::Flag flag) const;
	void entryRemoved(not_null<Dialogs::Entry*> entry);

	void storyUpdated(
		not_null<Story*> story,
		StoryUpdate::Flags flags);
	[[nodiscard]] rpl::producer<StoryUpdate> storyUpdates(
		StoryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<StoryUpdate> storyUpdates(
		not_null<Story*> story,
		StoryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<StoryUpdate> storyFlagsValue(
		not_null<Story*> story,
		StoryUpdate::Flags flags) const;
	[[nodiscard]] rpl::producer<StoryUpdate> realtimeStoryUpdates(
		StoryUpdate::Flag flag) const;

	void chatAdminChanged(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		ChatAdminRights rights,
		QString rank);
	[[nodiscard]] rpl::producer<ChatAdminChange> chatAdminChanges() const;

	void sendNotifications();

private:
	template <typename DataType, typename UpdateType>
	class Manager final {
	public:
		using Flag = typename UpdateType::Flag;
		using Flags = typename UpdateType::Flags;

		void updated(
			not_null<DataType*> data,
			Flags flags,
			bool dropScheduled = false);
		[[nodiscard]] rpl::producer<UpdateType> updates(Flags flags) const;
		[[nodiscard]] rpl::producer<UpdateType> updates(
			not_null<DataType*> data,
			Flags flags) const;
		[[nodiscard]] rpl::producer<UpdateType> flagsValue(
			not_null<DataType*> data,
			Flags flags) const;
		[[nodiscard]] rpl::producer<UpdateType> realtimeUpdates(
			Flag flag) const;

		void drop(not_null<DataType*> data);

		void sendNotifications();

	private:
		static constexpr auto kCount = details::CountBit<Flag>() + 1;

		void sendRealtimeNotifications(
			not_null<DataType*> data,
			Flags flags);

		std::array<rpl::event_stream<UpdateType>, kCount> _realtimeStreams;
		base::flat_map<not_null<DataType*>, Flags> _updates;
		rpl::event_stream<UpdateType> _stream;

	};

	void scheduleNotifications();

	const not_null<Main::Session*> _session;

	rpl::event_stream<NameUpdate> _nameStream;
	Manager<PeerData, PeerUpdate> _peerChanges;
	Manager<History, HistoryUpdate> _historyChanges;
	Manager<ForumTopic, TopicUpdate> _topicChanges;
	Manager<SavedSublist, SublistUpdate> _sublistChanges;
	Manager<HistoryItem, MessageUpdate> _messageChanges;
	Manager<Dialogs::Entry, EntryUpdate> _entryChanges;
	Manager<Story, StoryUpdate> _storyChanges;
	rpl::event_stream<ChatAdminChange> _chatAdminChanges;

	bool _notify = false;

};

} // namespace Data
