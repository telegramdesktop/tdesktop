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
constexpr auto kSpeakStatusKeptFor = crl::time(1000);

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
	api().request(_unknownSourcesRequestId).cancel();
	api().request(_participantsRequestId).cancel();
	api().request(_reloadRequestId).cancel();
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
	_participantsRequestId = api().request(MTPphone_GetGroupParticipants(
		input(),
		MTP_vector<MTPint>(), // ids
		MTP_vector<MTPint>(), // sources
		MTP_string(_nextOffset),
		MTP_int(kRequestPerPage)
	)).done([=](const MTPphone_GroupParticipants &result) {
		result.match([&](const MTPDphone_groupParticipants &data) {
			_nextOffset = qs(data.vnext_offset());
			_channel->owner().processUsers(data.vusers());
			applyParticipantsSlice(
				data.vparticipants().v,
				ApplySliceSource::SliceLoaded);
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

UserData *GroupCall::userBySource(uint32 source) const {
	const auto i = _userBySource.find(source);
	return (i != end(_userBySource)) ? i->second.get() : nullptr;
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
			|| (_fullCount.current() != data.vparticipants_count().v)
			|| (_joinMuted != data.is_join_muted())
			|| (_canChangeJoinMuted != data.is_can_change_join_muted());
		if (!force && !changed) {
			return;
		} else if (!force && _version > data.vversion().v) {
			reload();
			return;
		}
		_joinMuted = data.is_join_muted();
		_canChangeJoinMuted = data.is_can_change_join_muted();
		_version = data.vversion().v;
		_fullCount = data.vparticipants_count().v;
	}, [&](const MTPDgroupCallDiscarded &data) {
		const auto id = _id;
		const auto channel = _channel;
		crl::on_main(&channel->session(), [=] {
			if (channel->call() && channel->call()->id() == id) {
				channel->clearCall();
			}
		});
	});
}

void GroupCall::reload() {
	if (_reloadRequestId) {
		return;
	} else if (_participantsRequestId) {
		api().request(_participantsRequestId).cancel();
		_participantsRequestId = 0;
	}
	_reloadRequestId = api().request(
		MTPphone_GetGroupCall(input())
	).done([=](const MTPphone_GroupCall &result) {
		result.match([&](const MTPDphone_groupCall &data) {
			_channel->owner().processUsers(data.vusers());
			_participants.clear();
			_userBySource.clear();
			applyParticipantsSlice(
				data.vparticipants().v,
				ApplySliceSource::SliceLoaded);
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
		ApplySliceSource sliceSource) {
	if (sliceSource != ApplySliceSource::UnknownLoaded) {
		return;
	}
	auto changedCount = _fullCount.current();
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
						.was = *i,
					};
					_userBySource.erase(i->source);
					_participants.erase(i);
					if (sliceSource != ApplySliceSource::SliceLoaded) {
						_participantUpdates.fire(std::move(update));
					}
				}
				if (changedCount > _participants.size()) {
					--changedCount;
				}
				return;
			}
			const auto was = (i != end(_participants))
				? std::make_optional(*i)
				: std::nullopt;
			const auto value = Participant{
				.user = user,
				.date = data.vdate().v,
				.lastActive = was ? was->lastActive : 0,
				.source = uint32(data.vsource().v),
				.speaking = !data.is_muted() && (was ? was->speaking : false),
				.muted = data.is_muted(),
				.canSelfUnmute = !data.is_muted() || data.is_can_self_unmute(),
			};
			if (i == end(_participants)) {
				_userBySource.emplace(value.source, user);
				_participants.push_back(value);
				_channel->owner().unregisterInvitedToCallUser(_id, user);
				++changedCount;
			} else {
				if (i->source != value.source) {
					_userBySource.erase(i->source);
					_userBySource.emplace(value.source, user);
				}
				*i = value;
			}
			if (sliceSource != ApplySliceSource::SliceLoaded) {
				_participantUpdates.fire({
					.was = was,
					.now = value,
				});
			}
		});
	}
	if (sliceSource == ApplySliceSource::UpdateReceived) {
		_fullCount = changedCount;
	}
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
				const auto was = *i;
				i->muted = data.is_muted();
				i->canSelfUnmute = !i->muted || data.is_can_self_unmute();
				if (i->muted) {
					i->speaking = false;
				}
				_participantUpdates.fire({
					.was = was,
					.now = *i,
				});
			}
		});
	}
}

void GroupCall::applyLastSpoke(uint32 source, crl::time when, crl::time now) {
	const auto i = _userBySource.find(source);
	if (i == end(_userBySource)) {
		_unknownSpokenSources.emplace(source, when);
		requestUnknownSources();
		return;
	}
	const auto j = ranges::find(_participants, i->second, &Participant::user);
	Assert(j != end(_participants));

	const auto speaking = (when + kSpeakStatusKeptFor >= now) && !j->muted;
	if (j->speaking != speaking) {
		const auto was = *j;
		j->speaking = speaking;
		_participantUpdates.fire({
			.was = was,
			.now = *j,
		});
	}
}

void GroupCall::requestUnknownSources() {
	if (_unknownSourcesRequestId || _unknownSpokenSources.empty()) {
		return;
	}
	const auto sources = [&] {
		if (_unknownSpokenSources.size() < kRequestPerPage) {
			return base::take(_unknownSpokenSources);
		}
		auto result = base::flat_map<uint32, crl::time>();
		result.reserve(kRequestPerPage);
		while (result.size() < kRequestPerPage) {
			const auto [source, when] = _unknownSpokenSources.back();
			result.emplace(source, when);
			_unknownSpokenSources.erase(_unknownSpokenSources.end() - 1);
		}
		return result;
	}();
	auto sourceInputs = QVector<MTPint>();
	sourceInputs.reserve(sources.size());
	for (const auto [source, when] : sources) {
		sourceInputs.push_back(MTP_int(source));
	}
	_unknownSourcesRequestId = api().request(MTPphone_GetGroupParticipants(
		input(),
		MTP_vector<MTPint>(), // ids
		MTP_vector<MTPint>(sourceInputs),
		MTP_string(QString()),
		MTP_int(kRequestPerPage)
	)).done([=](const MTPphone_GroupParticipants &result) {
		result.match([&](const MTPDphone_groupParticipants &data) {
			_channel->owner().processUsers(data.vusers());
			applyParticipantsSlice(
				data.vparticipants().v,
				ApplySliceSource::UnknownLoaded);
		});
		_unknownSourcesRequestId = 0;
		const auto now = crl::now();
		for (const auto [source, when] : sources) {
			applyLastSpoke(source, when, now);
			_unknownSpokenSources.remove(source);
		}
		requestUnknownSources();
	}).fail([=](const RPCError &error) {
		_unknownSourcesRequestId = 0;
		for (const auto [source, when] : sources) {
			_unknownSpokenSources.remove(source);
		}
		requestUnknownSources();
	}).send();
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
	applyParticipantsSlice(
		update.vparticipants().v,
		ApplySliceSource::UpdateReceived);
}

void GroupCall::setJoinMutedLocally(bool muted) {
	_joinMuted = muted;
}

bool GroupCall::joinMuted() const {
	return _joinMuted;
}

bool GroupCall::canChangeJoinMuted() const {
	return _canChangeJoinMuted;
}

ApiWrap &GroupCall::api() const {
	return _channel->session().api();
}

} // namespace Data
