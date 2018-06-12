/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace TLHelp {

template <typename Callback>
inline auto VisitChannelParticipant(
		const MTPChannelParticipant &p,
		Callback &&callback) {
	switch (p.type()) {
	case mtpc_channelParticipant:
		return callback(p.c_channelParticipant());
	case mtpc_channelParticipantSelf:
		return callback(p.c_channelParticipantSelf());
	case mtpc_channelParticipantAdmin:
		return callback(p.c_channelParticipantAdmin());
	case mtpc_channelParticipantCreator:
		return callback(p.c_channelParticipantCreator());
	case mtpc_channelParticipantBanned:
		return callback(p.c_channelParticipantBanned());
	default: Unexpected("Type in VisitChannelParticipant()");
	}
}

inline UserId ReadChannelParticipantUserId(const MTPChannelParticipant &p) {
	return VisitChannelParticipant(p, [](auto &&data) {
		return data.vuser_id.v;
	});
}

template <typename Callback>
inline auto VisitChannelParticipants(
		const MTPchannels_ChannelParticipants &p,
		Callback &&callback) {
	switch (p.type()) {
	case mtpc_channels_channelParticipants:
		return callback(p.c_channels_channelParticipants());
	case mtpc_channels_channelParticipantsNotModified:
		return callback(p.type());
	default: Unexpected("Type in VisitChannelParticipants()");
	}
}

} // namespace TLHelp
