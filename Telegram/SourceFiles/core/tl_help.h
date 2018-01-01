/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
