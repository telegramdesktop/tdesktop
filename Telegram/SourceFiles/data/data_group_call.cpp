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

GroupCall::~GroupCall() {
	_channel->session().api().request(_participantsRequestId).cancel();
	_channel->session().api().request(_reloadRequestId).cancel();
}

uint64 GroupCall::id() const {
	return _id;
}

not_null<ChannelData*> GroupCall::channel() const {
	return _channel;
}

MTPInputGroupCall GroupCall::input() const {
	return MTP_inputGroupCall(MTP_long(_id), MTP_long(_accessHash));
}

auto GroupCall::participants() const
-> const std::vector<Participant> & {
	return _participants;
}

void GroupCall::requestParticipants() {
	if (_participantsRequestId || _reloadRequestId) {
		return;
	} else if (_participants.size() >= _fullCount.current() && _allReceived) {
		return;
	} else if (_allReceived) {
		reload();
		return;
	}
	auto &api = _channel->session().api();
	_participantsRequestId = api.request(MTPphone_GetGroupParticipants(
		input(),
		MTP_string(_nextOffset),
		MTP_int(kRequestPerPage)
	)).done([=](const MTPphone_GroupParticipants &result) {
		result.match([&](const MTPDphone_groupParticipants &data) {
			_nextOffset = qs(data.vnext_offset());
			_channel->owner().processUsers(data.vusers());
			applyParticipantsSlice(data.vparticipants().v);
			_fullCount = data.vcount().v;
			if (!_allReceived
				&& (data.vparticipants().v.size() < kRequestPerPage)) {
				_allReceived = true;
			}
			if (_allReceived) {
				_fullCount = _participants.size();
			}
		});
		_participantsSliceAdded.fire({});
		_participantsRequestId = 0;
	}).fail([=](const RPCError &error) {
		_fullCount = _participants.size();
		_allReceived = true;
		_channel->session().changes().peerUpdated(
			_channel,
			PeerUpdate::Flag::GroupCall);
		_participantsRequestId = 0;
	}).send();
}

int GroupCall::fullCount() const {
	return _fullCount.current();
}

rpl::producer<int> GroupCall::fullCountValue() const {
	return _fullCount.value();
}

bool GroupCall::participantsLoaded() const {
	return _allReceived;
}

rpl::producer<> GroupCall::participantsSliceAdded() {
	return _participantsSliceAdded.events();
}

auto GroupCall::participantUpdated() const
-> rpl::producer<ParticipantUpdate> {
	return _participantUpdates.events();
}

void GroupCall::applyUpdate(const MTPGroupCall &update) {
	applyCall(update, false);
}

void GroupCall::applyCall(const MTPGroupCall &call, bool force) {
	call.match([&](const MTPDgroupCall &data) {
		const auto changed = (_version != data.vversion().v)
			|| (_fullCount.current() != data.vparticipants_count().v);
		if (!force && !changed) {
			return;
		} else if (!force && _version > data.vversion().v) {
			reload();
			return;
		}
		_version = data.vversion().v;
		_fullCount = data.vparticipants_count().v;
	}, [&](const MTPDgroupCallDiscarded &data) {
		const auto changed = (_duration != data.vduration().v)
			|| !_finished;
		if (!force && !changed) {
			return;
		}
		_finished = true;
		_duration = data.vduration().v;
	});
}

void GroupCall::reload() {
	if (_reloadRequestId) {
		return;
	} else if (_participantsRequestId) {
		_channel->session().api().request(_participantsRequestId).cancel();
		_participantsRequestId = 0;
	}
	_reloadRequestId = _channel->session().api().request(
		MTPphone_GetGroupCall(input())
	).done([=](const MTPphone_GroupCall &result) {
		result.match([&](const MTPDphone_groupCall &data) {
			_channel->owner().processUsers(data.vusers());
			_participants.clear();
			applyParticipantsSlice(data.vparticipants().v);
			applyCall(data.vcall(), true);
			_allReceived = (_fullCount.current() == _participants.size());
			_participantsSliceAdded.fire({});
		});
		_reloadRequestId = 0;
	}).fail([=](const RPCError &error) {
		_reloadRequestId = 0;
	}).send();
}

void GroupCall::applyParticipantsSlice(
		const QVector<MTPGroupCallParticipant> &list,
		bool sendIndividualUpdates) {
	auto fullCount = _fullCount.current();
	for (const auto &participant : list) {
		participant.match([&](const MTPDgroupCallParticipant &data) {
			const auto userId = data.vuser_id().v;
			const auto user = _channel->owner().user(userId);
			const auto i = ranges::find(
				_participants,
				user,
				&Participant::user);
			if (data.is_left()) {
				if (i != end(_participants)) {
					auto update = ParticipantUpdate{
						.participant = *i,
						.removed = true,
					};
					_participants.erase(i);
					if (sendIndividualUpdates) {
						_participantUpdates.fire(std::move(update));
					}
				}
				if (fullCount > _participants.size()) {
					--fullCount;
				}
				return;
			}
			const auto value = Participant{
				.user = user,
				.date = data.vdate().v,
				.source = uint32(data.vsource().v),
				.muted = data.is_muted(),
				.canSelfUnmute = data.is_can_self_unmute(),
			};
			if (i == end(_participants)) {
				_participants.push_back(value);
				++fullCount;
			} else {
				*i = value;
			}
			_participantUpdates.fire({
				.participant = value,
			});
		});
	}
	ranges::sort(_participants, std::greater<>(), [](const Participant &p) {
		return p.lastActivePrecise
			? p.lastActivePrecise
			: p.lastActive
			? p.lastActive
			: p.date;
	});
	_fullCount = fullCount;
}

void GroupCall::applyParticipantsMutes(
		const MTPDupdateGroupCallParticipants &update) {
	for (const auto &participant : update.vparticipants().v) {
		participant.match([&](const MTPDgroupCallParticipant &data) {
			if (data.is_left()) {
				return;
			}
			const auto userId = data.vuser_id().v;
			const auto user = _channel->owner().user(userId);
			const auto i = ranges::find(
				_participants,
				user,
				&Participant::user);
			if (i != end(_participants)) {
				i->muted = data.is_muted();
				i->canSelfUnmute = data.is_can_self_unmute();
				_participantUpdates.fire({
					.participant = *i,
				});
			}
		});
	}

}

void GroupCall::applyUpdate(const MTPDupdateGroupCallParticipants &update) {
	const auto version = update.vversion().v;
	if (version < _version) {
		return;
	} else if (version == _version) {
		applyParticipantsMutes(update);
		return;
	} else if (version != _version + 1) {
		applyParticipantsMutes(update);
		reload();
		return;
	}
	_version = update.vversion().v;
	applyUpdateChecked(update);
}

void GroupCall::applyUpdateChecked(
		const MTPDupdateGroupCallParticipants &update) {
	applyParticipantsSlice(update.vparticipants().v, true);
}

} // namespace Data
