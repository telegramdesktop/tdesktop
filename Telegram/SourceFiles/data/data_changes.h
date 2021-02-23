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

namespace Data {

namespace details {

template <typename Flag>
inline constexpr int CountBit(Flag Last = Flag::LastUsedBit) {
	auto i = 0;
	while ((1ULL << i) != static_cast<uint64>(Last)) {
		++i;
		Assert(i != 64);
	}
	return (i + 1);
}

} // namespace details

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
	enum class Flag : uint32 {
		None = 0,

		// Common flags
		Name              = (1U << 0),
		Username          = (1U << 1),
		Photo             = (1U << 2),
		About             = (1U << 3),
		Notifications     = (1U << 4),
		Migration         = (1U << 5),
		UnavailableReason = (1U << 6),
		PinnedMessages    = (1U << 7),
		IsBlocked         = (1U << 8),
		MessagesTTL       = (1U << 9),

		// For users
		CanShareContact   = (1U << 10),
		IsContact         = (1U << 11),
		PhoneNumber       = (1U << 12),
		OnlineStatus      = (1U << 13),
		BotCommands       = (1U << 14),
		BotCanBeInvited   = (1U << 15),
		BotStartToken     = (1U << 16),
		CommonChats       = (1U << 17),
		HasCalls          = (1U << 18),
		SupportInfo       = (1U << 19),
		IsBot             = (1U << 20),

		// For chats and channels
		InviteLinks       = (1U << 21),
		Members           = (1U << 22),
		Admins            = (1U << 23),
		BannedUsers       = (1U << 24),
		Rights            = (1U << 25),

		// For channels
		ChannelAmIn       = (1U << 26),
		StickersSet       = (1U << 27),
		ChannelLinkedChat = (1U << 28),
		ChannelLocation   = (1U << 29),
		Slowmode          = (1U << 30),
		GroupCall         = (1U << 31),

		// For iteration
		LastUsedBit       = (1U << 31),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<PeerData*> peer;
	Flags flags = 0;

};

struct HistoryUpdate {
	enum class Flag : uint32 {
		None = 0,

		IsPinned       = (1U << 0),
		UnreadView     = (1U << 1),
		TopPromoted    = (1U << 2),
		Folder         = (1U << 3),
		UnreadMentions = (1U << 4),
		LocalMessages  = (1U << 5),
		ChatOccupied   = (1U << 6),
		MessageSent    = (1U << 7),
		ScheduledSent  = (1U << 8),
		ForwardDraft   = (1U << 9),
		OutboxRead     = (1U << 10),
		BotKeyboard    = (1U << 11),
		CloudDraft     = (1U << 12),
		LocalDraftSet  = (1U << 13),

		LastUsedBit    = (1U << 13),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<History*> history;
	Flags flags = 0;

};

struct MessageUpdate {
	enum class Flag : uint32 {
		None = 0,

		Edited           = (1U << 0),
		Destroyed        = (1U << 1),
		DialogRowRepaint = (1U << 2),
		DialogRowRefresh = (1U << 3),
		NewAdded         = (1U << 4),
		ReplyMarkup      = (1U << 5),
		BotCallbackSent  = (1U << 6),
		NewMaybeAdded    = (1U << 7),

		LastUsedBit      = (1U << 7),
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

		LastUsedBit = (1U << 0),
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
		static constexpr auto kCount = details::CountBit<Flag>();

		void sendRealtimeNotifications(not_null<DataType*> data, Flags flags);

		std::array<rpl::event_stream<UpdateType>, kCount> _realtimeStreams;
		base::flat_map<not_null<DataType*>, Flags> _updates;
		rpl::event_stream<UpdateType> _stream;

	};

	void scheduleNotifications();

	const not_null<Main::Session*> _session;

	rpl::event_stream<NameUpdate> _nameStream;
	Manager<PeerData, PeerUpdate> _peerChanges;
	Manager<History, HistoryUpdate> _historyChanges;
	Manager<HistoryItem, MessageUpdate> _messageChanges;
	Manager<Dialogs::Entry, EntryUpdate> _entryChanges;

	bool _notify = false;

};

} // namespace Data
