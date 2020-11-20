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
		Name              = (1 << 0),
		Username          = (1 << 1),
		Photo             = (1 << 2),
		About             = (1 << 3),
		Notifications     = (1 << 4),
		Migration         = (1 << 5),
		UnavailableReason = (1 << 6),
		PinnedMessages    = (1 << 7),
		IsBlocked         = (1 << 8),

		// For users
		CanShareContact   = (1 << 9),
		IsContact         = (1 << 10),
		PhoneNumber       = (1 << 11),
		OnlineStatus      = (1 << 12),
		BotCommands       = (1 << 13),
		BotCanBeInvited   = (1 << 14),
		BotStartToken     = (1 << 15),
		CommonChats       = (1 << 16),
		HasCalls          = (1 << 17),
		SupportInfo       = (1 << 18),
		IsBot             = (1 << 19),

		// For chats and channels
		InviteLink        = (1 << 20),
		Members           = (1 << 21),
		Admins            = (1 << 22),
		BannedUsers       = (1 << 23),
		Rights            = (1 << 24),

		// For channels
		ChannelAmIn       = (1 << 25),
		StickersSet       = (1 << 26),
		ChannelLinkedChat = (1 << 27),
		ChannelLocation   = (1 << 28),
		Slowmode          = (1 << 29),
		GroupCall         = (1 << 30),

		// For iteration
		LastUsedBit       = (1 << 30),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<PeerData*> peer;
	Flags flags = 0;

};

struct HistoryUpdate {
	enum class Flag : uint32 {
		None = 0,

		IsPinned       = (1 << 0),
		UnreadView     = (1 << 1),
		TopPromoted    = (1 << 2),
		Folder         = (1 << 3),
		UnreadMentions = (1 << 4),
		LocalMessages  = (1 << 5),
		ChatOccupied   = (1 << 6),
		MessageSent    = (1 << 7),
		ScheduledSent  = (1 << 8),
		ForwardDraft   = (1 << 9),
		OutboxRead     = (1 << 10),
		BotKeyboard    = (1 << 11),
		CloudDraft     = (1 << 12),

		LastUsedBit    = (1 << 12),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<History*> history;
	Flags flags = 0;

};

struct MessageUpdate {
	enum class Flag : uint32 {
		None = 0,

		Edited           = (1 << 0),
		Destroyed        = (1 << 1),
		DialogRowRepaint = (1 << 2),
		DialogRowRefresh = (1 << 3),
		NewAdded         = (1 << 4),
		ReplyMarkup      = (1 << 5),
		BotCallbackSent  = (1 << 6),
		NewMaybeAdded    = (1 << 7),

		LastUsedBit      = (1 << 7),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<HistoryItem*> item;
	Flags flags = 0;

};

struct EntryUpdate {
	enum class Flag : uint32 {
		None = 0,

		Repaint = (1 << 0),

		LastUsedBit = (1 << 0),
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
