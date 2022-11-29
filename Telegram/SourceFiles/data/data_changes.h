/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

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
		Name              = (1ULL << 0),
		Username          = (1ULL << 1),
		Photo             = (1ULL << 2),
		About             = (1ULL << 3),
		Notifications     = (1ULL << 4),
		Migration         = (1ULL << 5),
		UnavailableReason = (1ULL << 6),
		ChatThemeEmoji    = (1ULL << 7),
		IsBlocked         = (1ULL << 8),
		MessagesTTL       = (1ULL << 9),
		FullInfo          = (1ULL << 10),
		Usernames         = (1ULL << 11),

		// For users
		CanShareContact   = (1ULL << 11),
		IsContact         = (1ULL << 12),
		PhoneNumber       = (1ULL << 13),
		OnlineStatus      = (1ULL << 14),
		BotCommands       = (1ULL << 15),
		BotCanBeInvited   = (1ULL << 16),
		BotStartToken     = (1ULL << 17),
		CommonChats       = (1ULL << 18),
		HasCalls          = (1ULL << 19),
		SupportInfo       = (1ULL << 20),
		IsBot             = (1ULL << 21),
		EmojiStatus       = (1ULL << 22),

		// For chats and channels
		InviteLinks       = (1ULL << 23),
		Members           = (1ULL << 24),
		Admins            = (1ULL << 25),
		BannedUsers       = (1ULL << 26),
		Rights            = (1ULL << 27),
		PendingRequests   = (1ULL << 28),
		Reactions         = (1ULL << 29),

		// For channels
		ChannelAmIn       = (1ULL << 30),
		StickersSet       = (1ULL << 31),
		ChannelLinkedChat = (1ULL << 32),
		ChannelLocation   = (1ULL << 33),
		Slowmode          = (1ULL << 34),
		GroupCall         = (1ULL << 35),

		// For iteration
		LastUsedBit       = (1ULL << 35),
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

		LastUsedBit        = (1U << 12),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<History*> history;
	Flags flags = 0;

};

struct TopicUpdate {
	enum class Flag : uint32 {
		None = 0,

		UnreadView = (1U << 1),
		UnreadMentions = (1U << 2),
		UnreadReactions = (1U << 3),
		Notifications = (1U << 4),
		Title = (1U << 5),
		IconId = (1U << 6),
		ColorId = (1U << 7),
		CloudDraft = (1U << 8),
		Closed = (1U << 9),
		Creator = (1U << 10),

		LastUsedBit = (1U << 10),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<ForumTopic*> topic;
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

		Repaint = (1U << 0),
		HasPinnedMessages = (1U << 1),
		ForwardDraft = (1U << 2),
		LocalDraftSet = (1U << 3),
		Height = (1U << 4),

		LastUsedBit = (1U << 4),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<Dialogs::Entry*> entry;
	Flags flags = 0;

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
	Manager<HistoryItem, MessageUpdate> _messageChanges;
	Manager<Dialogs::Entry, EntryUpdate> _entryChanges;

	bool _notify = false;

};

} // namespace Data
