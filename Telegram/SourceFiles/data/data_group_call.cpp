/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_group_call.h"

#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "calls/calls_instance.h"
#include "calls/calls_group_call.h"
#include "calls/calls_group_common.h"
#include "core/application.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRequestPerPage = 30;
constexpr auto kSpeakingAfterActive = crl::time(6000);
constexpr auto kActiveAfterJoined = crl::time(1000);

} // namespace

GroupCall::GroupCall(
	not_null<PeerData*> peer,
	uint64 id,
	uint64 accessHash)
: _id(id)
, _accessHash(accessHash)
, _peer(peer)
, _speakingByActiveFinishTimer([=] { checkFinishSpeakingByActive(); }) {
}

GroupCall::~GroupCall() {
	api().request(_unknownParticipantPeersRequestId).cancel();
	api().request(_participantsRequestId).cancel();
	api().request(_reloadRequestId).cancel();
}

uint64 GroupCall::id() const {
	return _id;
}

not_null<PeerData*> GroupCall::peer() const {
	return _peer;
}

MTPInputGroupCall GroupCall::input() const {
	return MTP_inputGroupCall(MTP_long(_id), MTP_long(_accessHash));
}

void GroupCall::setPeer(not_null<PeerData*> peer) {
	Expects(peer->migrateFrom() == _peer);
	Expects(_peer->migrateTo() == peer);

	_peer = peer;
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
		MTP_vector<MTPInputPeer>(), // ids
		MTP_vector<MTPint>(), // ssrcs
		MTP_string(_nextOffset),
		MTP_int(kRequestPerPage)
	)).done([=](const MTPphone_GroupParticipants &result) {
		result.match([&](const MTPDphone_groupParticipants &data) {
			_nextOffset = qs(data.vnext_offset());
			_peer->owner().processUsers(data.vusers());
			_peer->owner().processChats(data.vchats());
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
		changePeerEmptyCallFlag();
	}).fail([=](const RPCError &error) {
		_fullCount = _participants.size();
		_allReceived = true;
		_participantsRequestId = 0;
		changePeerEmptyCallFlag();
	}).send();
}

void GroupCall::changePeerEmptyCallFlag() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	constexpr auto chatFlag = MTPDchat::Flag::f_call_not_empty;
	constexpr auto channelFlag = MTPDchannel::Flag::f_call_not_empty;
	if (_peer->groupCall() != this) {
		return;
	} else if (fullCount() > 0) {
		if (chat && !(chat->flags() & chatFlag)) {
			chat->addFlags(chatFlag);
			chat->session().changes().peerUpdated(
				chat,
				Data::PeerUpdate::Flag::GroupCall);
		} else if (channel && !(channel->flags() & channelFlag)) {
			channel->addFlags(channelFlag);
			channel->session().changes().peerUpdated(
				channel,
				Data::PeerUpdate::Flag::GroupCall);
		}
	} else if (chat && (chat->flags() & chatFlag)) {
		chat->removeFlags(chatFlag);
		chat->session().changes().peerUpdated(
			chat,
			Data::PeerUpdate::Flag::GroupCall);
	} else if (channel && (channel->flags() & channelFlag)) {
		channel->removeFlags(channelFlag);
		channel->session().changes().peerUpdated(
			channel,
			Data::PeerUpdate::Flag::GroupCall);
	}
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

PeerData *GroupCall::participantPeerBySsrc(uint32 ssrc) const {
	const auto i = _participantPeerBySsrc.find(ssrc);
	return (i != end(_participantPeerBySsrc)) ? i->second.get() : nullptr;
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
		changePeerEmptyCallFlag();
	}, [&](const MTPDgroupCallDiscarded &data) {
		const auto id = _id;
		const auto peer = _peer;
		crl::on_main(&peer->session(), [=] {
			if (peer->groupCall() && peer->groupCall()->id() == id) {
				if (const auto chat = peer->asChat()) {
					chat->clearGroupCall();
				} else if (const auto channel = peer->asChannel()) {
					channel->clearGroupCall();
				}
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
			_peer->owner().processUsers(data.vusers());
			_peer->owner().processChats(data.vchats());
			_participants.clear();
			_speakingByActiveFinishes.clear();
			_participantPeerBySsrc.clear();
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
	const auto amInCall = inCall();
	const auto now = base::unixtime::now();
	const auto speakingAfterActive = TimeId(kSpeakingAfterActive / 1000);

	auto changedCount = _fullCount.current();
	for (const auto &participant : list) {
		participant.match([&](const MTPDgroupCallParticipant &data) {
			const auto participantPeerId = peerFromMTP(data.vpeer());
			const auto participantPeer = _peer->owner().peer(
				participantPeerId);
			const auto i = ranges::find(
				_participants,
				participantPeer,
				&Participant::peer);
			if (data.is_left()) {
				if (i != end(_participants)) {
					auto update = ParticipantUpdate{
						.was = *i,
					};
					_participantPeerBySsrc.erase(i->ssrc);
					_speakingByActiveFinishes.remove(participantPeer);
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
			const auto canSelfUnmute = !data.is_muted()
				|| data.is_can_self_unmute();
			const auto lastActive = data.vactive_date().value_or(
				was ? was->lastActive : 0);
			const auto speaking = canSelfUnmute
				&& ((was ? was->speaking : false)
					|| (!amInCall
						&& (lastActive + speakingAfterActive > now)));
			const auto volume = (was
				&& !was->applyVolumeFromMin
				&& data.is_min())
				? was->volume
				: data.vvolume().value_or(Calls::Group::kDefaultVolume);
			const auto applyVolumeFromMin = (was && data.is_min())
				? was->applyVolumeFromMin
				: (data.is_min() || data.is_volume_by_admin());
			const auto mutedByMe = (was && data.is_min())
				? was->mutedByMe
				: data.is_muted_by_you();
			const auto onlyMinLoaded = data.is_min()
				&& (!was || was->onlyMinLoaded);
			const auto value = Participant{
				.peer = participantPeer,
				.date = data.vdate().v,
				.lastActive = lastActive,
				.ssrc = uint32(data.vsource().v),
				.volume = volume,
				.applyVolumeFromMin = applyVolumeFromMin,
				.speaking = canSelfUnmute && (was ? was->speaking : false),
				.muted = data.is_muted(),
				.mutedByMe = mutedByMe,
				.canSelfUnmute = canSelfUnmute,
				.onlyMinLoaded = onlyMinLoaded,
			};
			if (i == end(_participants)) {
				_participantPeerBySsrc.emplace(value.ssrc, participantPeer);
				_participants.push_back(value);
				if (const auto user = participantPeer->asUser()) {
					_peer->owner().unregisterInvitedToCallUser(_id, user);
				}
			} else {
				if (i->ssrc != value.ssrc) {
					_participantPeerBySsrc.erase(i->ssrc);
					_participantPeerBySsrc.emplace(
						value.ssrc,
						participantPeer);
				}
				*i = value;
			}
			if (data.is_just_joined()) {
				++changedCount;
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
		changePeerEmptyCallFlag();
	}
}

void GroupCall::applyLastSpoke(
		uint32 ssrc,
		LastSpokeTimes when,
		crl::time now) {
	const auto i = _participantPeerBySsrc.find(ssrc);
	if (i == end(_participantPeerBySsrc)) {
		_unknownSpokenSsrcs[ssrc] = when;
		requestUnknownParticipants();
		return;
	}
	const auto j = ranges::find(
		_participants,
		i->second,
		&Participant::peer);
	Assert(j != end(_participants));

	_speakingByActiveFinishes.remove(j->peer);
	const auto sounding = (when.anything + kSoundStatusKeptFor >= now)
		&& j->canSelfUnmute;
	const auto speaking = sounding
		&& (when.voice + kSoundStatusKeptFor >= now);
	if (j->sounding != sounding || j->speaking != speaking) {
		const auto was = *j;
		j->sounding = sounding;
		j->speaking = speaking;
		_participantUpdates.fire({
			.was = was,
			.now = *j,
		});
	}
}

void GroupCall::applyActiveUpdate(
		PeerId participantPeerId,
		LastSpokeTimes when,
		PeerData *participantPeerLoaded) {
	if (inCall()) {
		return;
	}
	const auto i = participantPeerLoaded
		? ranges::find(
			_participants,
			not_null{ participantPeerLoaded },
			&Participant::peer)
		: _participants.end();
	const auto notFound = (i == end(_participants));
	const auto loadByUserId = notFound || i->onlyMinLoaded;
	if (loadByUserId) {
		_unknownSpokenPeerIds[participantPeerId] = when;
		requestUnknownParticipants();
	}
	if (notFound || !i->canSelfUnmute) {
		return;
	}
	const auto was = std::make_optional(*i);
	const auto now = crl::now();
	const auto elapsed = TimeId((now - when.anything) / crl::time(1000));
	const auto lastActive = base::unixtime::now() - elapsed;
	const auto finishes = when.anything + kSpeakingAfterActive;
	if (lastActive <= i->lastActive || finishes <= now) {
		return;
	}
	_speakingByActiveFinishes[i->peer] = finishes;
	if (!_speakingByActiveFinishTimer.isActive()) {
		_speakingByActiveFinishTimer.callOnce(finishes - now);
	}

	i->lastActive = lastActive;
	i->speaking = true;
	i->canSelfUnmute = true;
	if (!was->speaking || !was->canSelfUnmute) {
		_participantUpdates.fire({
			.was = was,
			.now = *i,
		});
	}
}

void GroupCall::checkFinishSpeakingByActive() {
	const auto now = crl::now();
	auto nearest = 0;
	auto stop = std::vector<not_null<PeerData*>>();
	for (auto i = begin(_speakingByActiveFinishes)
		; i != end(_speakingByActiveFinishes);) {
		const auto when = i->second;
		if (now >= when) {
			stop.push_back(i->first);
			i = _speakingByActiveFinishes.erase(i);
		} else {
			if (!nearest || nearest > when) {
				nearest = when;
			}
			++i;
		}
	}
	for (const auto participantPeer : stop) {
		const auto i = ranges::find(
			_participants,
			participantPeer,
			&Participant::peer);
		if (i->speaking) {
			const auto was = *i;
			i->speaking = false;
			_participantUpdates.fire({
				.was = was,
				.now = *i,
			});
		}
	}
	if (nearest) {
		_speakingByActiveFinishTimer.callOnce(nearest - now);
	}
}

void GroupCall::requestUnknownParticipants() {
	if (_unknownParticipantPeersRequestId
		|| (_unknownSpokenSsrcs.empty() && _unknownSpokenPeerIds.empty())) {
		return;
	}
	const auto ssrcs = [&] {
		if (_unknownSpokenSsrcs.size() < kRequestPerPage) {
			return base::take(_unknownSpokenSsrcs);
		}
		auto result = base::flat_map<uint32, LastSpokeTimes>();
		result.reserve(kRequestPerPage);
		while (result.size() < kRequestPerPage) {
			const auto [ssrc, when] = _unknownSpokenSsrcs.back();
			result.emplace(ssrc, when);
			_unknownSpokenSsrcs.erase(_unknownSpokenSsrcs.end() - 1);
		}
		return result;
	}();
	const auto participantPeerIds = [&] {
		if (_unknownSpokenPeerIds.size() + ssrcs.size() < kRequestPerPage) {
			return base::take(_unknownSpokenPeerIds);
		}
		auto result = base::flat_map<PeerId, LastSpokeTimes>();
		const auto available = (kRequestPerPage - int(ssrcs.size()));
		if (available > 0) {
			result.reserve(available);
			while (result.size() < available) {
				const auto &back = _unknownSpokenPeerIds.back();
				const auto [participantPeerId, when] = back;
				result.emplace(participantPeerId, when);
				_unknownSpokenPeerIds.erase(_unknownSpokenPeerIds.end() - 1);
			}
		}
		return result;
	}();
	auto ssrcInputs = QVector<MTPint>();
	ssrcInputs.reserve(ssrcs.size());
	for (const auto [ssrc, when] : ssrcs) {
		ssrcInputs.push_back(MTP_int(ssrc));
	}
	auto peerInputs = QVector<MTPInputPeer>();
	peerInputs.reserve(participantPeerIds.size());
	for (const auto [participantPeerId, when] : participantPeerIds) {
		if (const auto userId = peerToUser(participantPeerId)) {
			peerInputs.push_back(
				MTP_inputPeerUser(MTP_int(userId), MTP_long(0)));
		} else if (const auto chatId = peerToChat(participantPeerId)) {
			peerInputs.push_back(MTP_inputPeerChat(MTP_int(chatId)));
		} else if (const auto channelId = peerToChannel(participantPeerId)) {
			peerInputs.push_back(
				MTP_inputPeerChannel(MTP_int(channelId), MTP_long(0)));
		}
	}
	_unknownParticipantPeersRequestId = api().request(
		MTPphone_GetGroupParticipants(
			input(),
			MTP_vector<MTPInputPeer>(peerInputs),
			MTP_vector<MTPint>(ssrcInputs),
			MTP_string(QString()),
			MTP_int(kRequestPerPage)
		)
	).done([=](const MTPphone_GroupParticipants &result) {
		result.match([&](const MTPDphone_groupParticipants &data) {
			_peer->owner().processUsers(data.vusers());
			applyParticipantsSlice(
				data.vparticipants().v,
				ApplySliceSource::UnknownLoaded);
		});
		_unknownParticipantPeersRequestId = 0;
		const auto now = crl::now();
		for (const auto [ssrc, when] : ssrcs) {
			applyLastSpoke(ssrc, when, now);
			_unknownSpokenSsrcs.remove(ssrc);
		}
		for (const auto [id, when] : participantPeerIds) {
			if (const auto participantPeer = _peer->owner().peerLoaded(id)) {
				const auto isParticipant = ranges::contains(
					_participants,
					not_null{ participantPeer },
					&Participant::peer);
				if (isParticipant) {
					applyActiveUpdate(id, when, participantPeer);
				}
			}
			_unknownSpokenPeerIds.remove(id);
		}
		requestUnknownParticipants();
	}).fail([=](const RPCError &error) {
		_unknownParticipantPeersRequestId = 0;
		for (const auto [ssrc, when] : ssrcs) {
			_unknownSpokenSsrcs.remove(ssrc);
		}
		for (const auto [participantPeerId, when] : participantPeerIds) {
			_unknownSpokenPeerIds.remove(participantPeerId);
		}
		requestUnknownParticipants();
	}).send();
}

void GroupCall::setInCall() {
	_unknownSpokenPeerIds.clear();
	if (_speakingByActiveFinishes.empty()) {
		return;
	}
	auto restartTimer = true;
	const auto latest = crl::now() + kActiveAfterJoined;
	for (auto &[peer, when] : _speakingByActiveFinishes) {
		if (when > latest) {
			when = latest;
		} else {
			restartTimer = false;
		}
	}
	if (restartTimer) {
		_speakingByActiveFinishTimer.callOnce(kActiveAfterJoined);
	}
}

bool GroupCall::inCall() const {
	const auto current = Core::App().calls().currentGroupCall();
	return (current != nullptr)
		&& (current->id() == _id)
		&& (current->state() == Calls::GroupCall::State::Joined);
}

void GroupCall::applyUpdate(const MTPDupdateGroupCallParticipants &update) {
	const auto version = update.vversion().v;
	const auto applyUpdate = [&] {
		if (version < _version) {
			return false;
		}
		auto versionShouldIncrement = false;
		for (const auto &participant : update.vparticipants().v) {
			const auto versioned = participant.match([&](
					const MTPDgroupCallParticipant &data) {
				return data.is_versioned();
			});
			if (versioned) {
				versionShouldIncrement = true;
				break;
			}
		}
		return versionShouldIncrement
			? (version == _version + 1)
			: (version == _version);
	}();
	if (!applyUpdate) {
		return;
	}
	_version = version;
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
	return _peer->session().api();
}

} // namespace Data
