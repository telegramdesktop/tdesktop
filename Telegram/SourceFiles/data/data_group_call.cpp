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

const base::flat_set<uint32> &GroupCall::sources() const {
	return _sources;
}

void GroupCall::requestParticipants() {
	if (_participantsRequestId || _reloadRequestId) {
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
		_channel->session().changes().peerUpdated(
			_channel,
			PeerUpdate::Flag::GroupCall);
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
	return _fullCount;
}

bool GroupCall::participantsLoaded() const {
	return _allReceived;
}

void GroupCall::applyUpdate(const MTPGroupCall &update) {
	applyCall(update, false);
}

void GroupCall::applyCall(const MTPGroupCall &call, bool force) {
	call.match([&](const MTPDgroupCall &data) {
		const auto changed = (_version != data.vversion().v)
			|| (_fullCount != data.vparticipants_count().v);
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
	_channel->session().changes().peerUpdated(
		_channel,
		PeerUpdate::Flag::GroupCall);
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
			_sources.clear();
			applyParticipantsSlice(data.vparticipants().v);
			for (const auto &source : data.vsources().v) {
				_sources.emplace(source.v);
			}
			_fullCount = _sources.size();
			if (_participants.size() > _fullCount) {
				_fullCount = _participants.size();
			}
			_allReceived = (_fullCount == _participants.size());
			applyCall(data.vcall(), true);
		});
		_reloadRequestId = 0;
	}).fail([=](const RPCError &error) {
		_reloadRequestId = 0;
	}).send();
}

void GroupCall::applyParticipantsSlice(
		const QVector<MTPGroupCallParticipant> &list) {
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
					_sources.remove(i->source);
					_participants.erase(i);
				}
				if (_fullCount > _participants.size()) {
					--_fullCount;
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
				++_fullCount;
			} else {
				*i = value;
			}
			_sources.emplace(uint32(data.vsource().v));
		});
	}
	ranges::sort(_participants, std::greater<>(), &Participant::date);
}

void GroupCall::applyUpdate(const MTPDupdateGroupCallParticipants &update) {
	if (update.vversion().v <= _version) {
		return;
	} else if (update.vversion().v != _version + 1) {
		reload();
		return;
	}
	_version = update.vversion().v;
	applyParticipantsSlice(update.vparticipants().v);
	_channel->session().changes().peerUpdated(
		_channel,
		PeerUpdate::Flag::GroupCall);
}

} // namespace Data
