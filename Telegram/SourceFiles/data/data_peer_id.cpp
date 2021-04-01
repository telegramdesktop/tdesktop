/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer_id.h"

PeerId peerFromMTP(const MTPPeer &peer) {
	return peer.match([](const MTPDpeerUser &data) {
		return peerFromUser(data.vuser_id());
	}, [](const MTPDpeerChat &data) {
		return peerFromChat(data.vchat_id());
	}, [](const MTPDpeerChannel &data) {
		return peerFromChannel(data.vchannel_id());
	});
}

MTPpeer peerToMTP(PeerId id) {
	if (peerIsUser(id)) {
		return MTP_peerUser(peerToBareMTPInt(id));
	} else if (peerIsChat(id)) {
		return MTP_peerChat(peerToBareMTPInt(id));
	} else if (peerIsChannel(id)) {
		return MTP_peerChannel(peerToBareMTPInt(id));
	}
	return MTP_peerUser(MTP_int(0));
}

PeerId DeserializePeerId(quint64 serialized) {
	const auto flag = (UserId::kReservedBit << 48);
	const auto legacy = !(serialized & (UserId::kReservedBit << 48));
	if (!legacy) {
		return PeerId(serialized & (~flag));
	}
	constexpr auto PeerIdMask = uint64(0xFFFFFFFFULL);
	constexpr auto PeerIdTypeMask = uint64(0xF00000000ULL);
	constexpr auto PeerIdUserShift = uint64(0x000000000ULL);
	constexpr auto PeerIdChatShift = uint64(0x100000000ULL);
	constexpr auto PeerIdChannelShift = uint64(0x200000000ULL);
	constexpr auto PeerIdFakeShift = uint64(0xF00000000ULL);
	return ((serialized & PeerIdTypeMask) == PeerIdUserShift)
		? peerFromUser(UserId(serialized & PeerIdMask))
		: ((serialized & PeerIdTypeMask) == PeerIdChatShift)
		? peerFromChat(ChatId(serialized & PeerIdMask))
		: ((serialized & PeerIdTypeMask) == PeerIdChannelShift)
		? peerFromChannel(ChannelId(serialized & PeerIdMask))
		: ((serialized & PeerIdTypeMask) == PeerIdFakeShift)
		? PeerId(FakeChatId(serialized & PeerIdMask))
		: PeerId(0);
}

quint64 SerializePeerId(PeerId id) {
	Expects(!(id.value & (UserId::kReservedBit << 48)));

	return id.value | (UserId::kReservedBit << 48);
}
