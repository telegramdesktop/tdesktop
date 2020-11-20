/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_group_call.h"

#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRequestPerPage = 30;

} // namespace

GroupCall::GroupCall(
	not_null<ChannelData*> channel,
	uint64 id,
	uint64 accessHash)
: _channel(channel)
, _id(id)
, _accessHash(accessHash) {
}

uint64 GroupCall::id() const {
	return _id;
}

MTPInputGroupCall GroupCall::input() const {
	return MTP_inputGroupCall(MTP_long(_id), MTP_long(_accessHash));
}

auto GroupCall::participants() const
-> const std::vector<Participant> & {
	return _participants;
}

void GroupCall::requestParticipants() {
	if (_participantsRequestId) {
		return;
	} else if (_participants.size() >= _fullCount && _allReceived) {
		return;
	}
	const auto requestFromDate = (_allReceived || _participants.empty())
		? TimeId(0)
		: _participants.back().date;
	auto &api = _channel->session().api();
	_participantsRequestId = api.request(MTPphone_GetGroupParticipants(
		input(),
		MTP_int(requestFromDate),
		MTP_int(kRequestPerPage)
	)).done([=](const MTPphone_GroupParticipants &result) {
		result.match([&](const MTPDphone_groupParticipants &data) {
			_fullCount = data.vcount().v;
			_channel->owner().processUsers(data.vusers());
			for (const auto &p : data.vparticipants().v) {
				p.match([&](const MTPDgroupCallParticipant &data) {
					const auto userId = data.vuser_id().v;
					const auto user = _channel->owner().user(userId);
					const auto value = Participant{
						.user = user,
						.date = data.vdate().v,
						.source = data.vsource().v,
						.muted = data.is_muted(),
						.canSelfUnmute = data.is_can_self_unmute(),
						.left = data.is_left()
					};
					const auto i = ranges::find(
						_participants,
						user,
						&Participant::user);
					if (i == end(_participants)) {
						_participants.push_back(value);
					} else {
						*i = value;
					}
				});
			}
			if (!_allReceived
				&& (data.vparticipants().v.size() < kRequestPerPage)) {
				_allReceived = true;
			}
			if (_allReceived) {
				_fullCount = _participants.size();
			}
		});
		ranges::sort(_participants, std::greater<>(), &Participant::date);
		_channel->session().changes().peerUpdated(
			_channel,
			PeerUpdate::Flag::GroupCall);
	}).fail([=](const RPCError &error) {
		_allReceived = true;
		_fullCount = _participants.size();
		_channel->session().changes().peerUpdated(
			_channel,
			PeerUpdate::Flag::GroupCall);
	}).send();
}

int GroupCall::fullCount() const {
	return _fullCount;
}

bool GroupCall::participantsLoaded() const {
	return _allReceived;
}

} // namespace Data
