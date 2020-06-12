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

namespace Data {

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
		PinnedMessage     = (1 << 7),

		// For users
		CanShareContact   = (1 << 8),
		IsContact         = (1 << 9),
		PhoneNumber       = (1 << 10),
		IsBlocked         = (1 << 11),
		OnlineStatus      = (1 << 12),
		BotCommands       = (1 << 13),
		BotCanBeInvited   = (1 << 14),
		CommonChats       = (1 << 15),
		HasCalls          = (1 << 16),
		SupportInfo       = (1 << 17),
		IsBot             = (1 << 18),

		// For chats and channels
		InviteLink        = (1 << 19),
		Members           = (1 << 20),
		Admins            = (1 << 21),
		BannedUsers       = (1 << 22),
		Rights            = (1 << 23),

		// For channels
		ChannelAmIn       = (1 << 24),
		StickersSet       = (1 << 25),
		ChannelLinkedChat = (1 << 26),
		ChannelLocation   = (1 << 27),
		Slowmode          = (1 << 28),

		// For iteration
		LastUsedBit       = (1 << 28),
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

		LastUsedBit    = (1 << 6),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<History*> history;
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

	void sendNotifications();

private:
	template <typename Flag>
	static constexpr int CountBit(Flag Last = Flag::LastUsedBit) {
		auto i = 0;
		while ((1ULL << i) != static_cast<uint64>(Last)) {
			++i;
			Assert(i != 64);
		}
		return (i + 1);
	}

	template <typename DataType, typename UpdateType>
	class Manager final {
	public:
		using Flag = typename UpdateType::Flag;
		using Flags = typename UpdateType::Flags;

		void updated(not_null<DataType*> data, Flags flags);
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
		static constexpr auto kCount = CountBit<Flag>();

		void sendRealtimeNotifications(not_null<DataType*> data, Flags flags);

		std::array<rpl::event_stream<UpdateType>, kCount> _realtimeStreams;
		base::flat_map<not_null<DataType*>, Flags> _updates;
		rpl::event_stream<UpdateType> _stream;

	};

	static constexpr auto kHistoryCount = CountBit<HistoryUpdate::Flag>();

	void scheduleNotifications();

	const not_null<Main::Session*> _session;

	rpl::event_stream<NameUpdate> _nameStream;
	Manager<PeerData, PeerUpdate> _peerChanges;
	Manager<History, HistoryUpdate> _historyChanges;

	bool _notify = false;

};

} // namespace Data
