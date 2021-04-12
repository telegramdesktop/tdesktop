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

constexpr auto kRequestPerPage = 50;
constexpr auto kSpeakingAfterActive = crl::time(6000);
constexpr auto kActiveAfterJoined = crl::time(1000);
constexpr auto kWaitForUpdatesTimeout = 3 * crl::time(1000);

[[nodiscard]] QString ExtractNextOffset(const MTPphone_GroupCall &call) {
	return call.match([&](const MTPDphone_groupCall &data) {
		return qs(data.vparticipants_next_offset());
	});
}

} // namespace

GroupCall::GroupCall(
	not_null<PeerData*> peer,
	uint64 id,
	uint64 accessHash,
	TimeId scheduleDate)
: _id(id)
, _accessHash(accessHash)
, _peer(peer)
, _reloadByQueuedUpdatesTimer([=] { reload(); })
, _speakingByActiveFinishTimer([=] { checkFinishSpeakingByActive(); })
, _scheduleDate(scheduleDate) {
}

GroupCall::~GroupCall() {
	api().request(_unknownParticipantPeersRequestId).cancel();
	api().request(_participantsRequestId).cancel();
	api().request(_reloadRequestId).cancel();
}

uint64 GroupCall::id() const {
	return _id;
}

bool GroupCall::loaded() const {
	return _version > 0;
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
	if (!_savedFull) {
		if (_participantsRequestId || _reloadRequestId) {
			return;
		} else if (_allParticipantsLoaded) {
			return;
		}
	}
	_participantsRequestId = api().request(MTPphone_GetGroupParticipants(
		input(),
		MTP_vector<MTPInputPeer>(), // ids
		MTP_vector<MTPint>(), // ssrcs
		MTP_string(_savedFull
			? ExtractNextOffset(*_savedFull)
			: _nextOffset),
		MTP_int(kRequestPerPage)
	)).done([=](const MTPphone_GroupParticipants &result) {
		_participantsRequestId = 0;
		processSavedFullCall();
		result.match([&](const MTPDphone_groupParticipants &data) {
			_nextOffset = qs(data.vnext_offset());
			_peer->owner().processUsers(data.vusers());
			_peer->owner().processChats(data.vchats());
			applyParticipantsSlice(
				data.vparticipants().v,
				ApplySliceSource::SliceLoaded);
			setServerParticipantsCount(data.vcount().v);
			if (data.vparticipants().v.isEmpty()) {
				_allParticipantsLoaded = true;
			}
			finishParticipantsSliceRequest();
		});
	}).fail([=](const MTP::Error &error) {
		_participantsRequestId = 0;
		processSavedFullCall();
		setServerParticipantsCount(_participants.size());
		_allParticipantsLoaded = true;
		finishParticipantsSliceRequest();
	}).send();
}

void GroupCall::processSavedFullCall() {
	if (!_savedFull) {
		return;
	}
	_reloadRequestId = 0;
	processFullCallFields(*base::take(_savedFull));
}

void GroupCall::finishParticipantsSliceRequest() {
	computeParticipantsCount();
	processQueuedUpdates();
	_participantsSliceAdded.fire({});
}

void GroupCall::setServerParticipantsCount(int count) {
	_serverParticipantsCount = count;
	changePeerEmptyCallFlag();
}

void GroupCall::changePeerEmptyCallFlag() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	constexpr auto chatFlag = MTPDchat::Flag::f_call_not_empty;
	constexpr auto channelFlag = MTPDchannel::Flag::f_call_not_empty;
	if (_peer->groupCall() != this) {
		return;
	} else if (_serverParticipantsCount > 0) {
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
	return _allParticipantsLoaded;
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

void GroupCall::enqueueUpdate(const MTPUpdate &update) {
	update.match([&](const MTPDupdateGroupCall &updateData) {
		updateData.vcall().match([&](const MTPDgroupCall &data) {
			const auto version = data.vversion().v;
			if (!_applyingQueuedUpdates
				&& (!_version || _version == version)) {
				DEBUG_LOG(("Group Call Participants: "
					"Apply updateGroupCall %1 -> %2"
					).arg(_version
					).arg(version));
				applyEnqueuedUpdate(update);
			} else if (!_version || _version <= version) {
				DEBUG_LOG(("Group Call Participants: "
					"Queue updateGroupCall %1 -> %2"
					).arg(_version
					).arg(version));
				const auto type = QueuedType::Call;
				_queuedUpdates.emplace(std::pair{ version, type }, update);
			}
		}, [&](const MTPDgroupCallDiscarded &data) {
			discard(data);
		});
	}, [&](const MTPDupdateGroupCallParticipants &updateData) {
		const auto version = updateData.vversion().v;
		const auto proj = [](const MTPGroupCallParticipant &data) {
			return data.match([&](const MTPDgroupCallParticipant &data) {
				return data.is_versioned();
			});
		};
		const auto increment = ranges::contains(
			updateData.vparticipants().v,
			true,
			proj);
		const auto required = increment ? (version - 1) : version;
		if (!_applyingQueuedUpdates && (_version == required)) {
			DEBUG_LOG(("Group Call Participants: "
				"Apply updateGroupCallParticipant %1 (%2)"
				).arg(_version
				).arg(Logs::b(increment)));
			applyEnqueuedUpdate(update);
		} else if (_version <= required) {
			DEBUG_LOG(("Group Call Participants: "
				"Queue updateGroupCallParticipant %1 -> %2 (%3)"
				).arg(_version
				).arg(version
				).arg(Logs::b(increment)));
			const auto type = increment
				? QueuedType::VersionedParticipant
				: QueuedType::Participant;
			_queuedUpdates.emplace(std::pair{ version, type }, update);
		}
	}, [](const auto &) {
		Unexpected("Type in GroupCall::enqueueUpdate.");
	});
	processQueuedUpdates();
}

void GroupCall::discard(const MTPDgroupCallDiscarded &data) {
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
	Core::App().calls().applyGroupCallUpdateChecked(
		&peer->session(),
		MTP_updateGroupCall(
			MTP_int(peer->isChat()
				? peerToChat(peer->id).bare
				: peerToChannel(peer->id).bare),
			MTP_groupCallDiscarded(
				data.vid(),
				data.vaccess_hash(),
				data.vduration())));
}

void GroupCall::processFullCallUsersChats(const MTPphone_GroupCall &call) {
	call.match([&](const MTPDphone_groupCall &data) {
		_peer->owner().processUsers(data.vusers());
		_peer->owner().processChats(data.vchats());
	});
}

void GroupCall::processFullCallFields(const MTPphone_GroupCall &call) {
	call.match([&](const MTPDphone_groupCall &data) {
		const auto &participants = data.vparticipants().v;
		const auto nextOffset = qs(data.vparticipants_next_offset());
		data.vcall().match([&](const MTPDgroupCall &data) {
			_participants.clear();
			_speakingByActiveFinishes.clear();
			_participantPeerBySsrc.clear();
			_allParticipantsLoaded = false;

			applyParticipantsSlice(
				participants,
				ApplySliceSource::SliceLoaded);
			_nextOffset = nextOffset;

			applyCallFields(data);
		}, [&](const MTPDgroupCallDiscarded &data) {
			discard(data);
		});
	});
}

void GroupCall::processFullCall(const MTPphone_GroupCall &call) {
	processFullCallUsersChats(call);
	processFullCallFields(call);
	finishParticipantsSliceRequest();
}

void GroupCall::applyCallFields(const MTPDgroupCall &data) {
	DEBUG_LOG(("Group Call Participants: "
		"Set from groupCall %1 -> %2"
		).arg(_version
		).arg(data.vversion().v));
	_version = data.vversion().v;
	if (!_version) {
		LOG(("API Error: Got zero version in groupCall."));
		_version = 1;
	}
	_joinMuted = data.is_join_muted();
	_canChangeJoinMuted = data.is_can_change_join_muted();
	_joinedToTop = !data.is_join_date_asc();
	setServerParticipantsCount(data.vparticipants_count().v);
	changePeerEmptyCallFlag();
	_title = qs(data.vtitle().value_or_empty());
	_recordStartDate = data.vrecord_start_date().value_or_empty();
	_scheduleDate = data.vschedule_date().value_or_empty();
	_scheduleStartSubscribed = data.is_schedule_start_subscribed();
	_allParticipantsLoaded
		= (_serverParticipantsCount == _participants.size());
}

void GroupCall::applyLocalUpdate(
		const MTPDupdateGroupCallParticipants &update) {
	applyParticipantsSlice(
		update.vparticipants().v,
		ApplySliceSource::UpdateReceived);
}

void GroupCall::applyEnqueuedUpdate(const MTPUpdate &update) {
	Expects(!_applyingQueuedUpdates);

	_applyingQueuedUpdates = true;
	const auto guard = gsl::finally([&] { _applyingQueuedUpdates = false; });

	update.match([&](const MTPDupdateGroupCall &data) {
		data.vcall().match([&](const MTPDgroupCall &data) {
			applyCallFields(data);
			computeParticipantsCount();
		}, [&](const MTPDgroupCallDiscarded &data) {
			discard(data);
		});
	}, [&](const MTPDupdateGroupCallParticipants &data) {
		DEBUG_LOG(("Group Call Participants: "
			"Set from updateGroupCallParticipants %1 -> %2"
			).arg(_version
			).arg(data.vversion().v));
		_version = data.vversion().v;
		if (!_version) {
			LOG(("API Error: "
				"Got zero version in updateGroupCallParticipants."));
			_version = 1;
		}
		applyParticipantsSlice(
			data.vparticipants().v,
			ApplySliceSource::UpdateReceived);
	}, [](const auto &) {
		Unexpected("Type in GroupCall::applyEnqueuedUpdate.");
	});
	Core::App().calls().applyGroupCallUpdateChecked(
		&_peer->session(),
		update);
}

void GroupCall::processQueuedUpdates() {
	if (!_version || _applyingQueuedUpdates) {
		return;
	}

	const auto size = _queuedUpdates.size();
	while (!_queuedUpdates.empty()) {
		const auto &entry = _queuedUpdates.front();
		const auto version = entry.first.first;
		const auto type = entry.first.second;
		const auto incremented = (type == QueuedType::VersionedParticipant);
		if ((version < _version)
			|| (version == _version && incremented)) {
			_queuedUpdates.erase(_queuedUpdates.begin());
		} else if (version == _version
			|| (version == _version + 1 && incremented)) {
			const auto update = entry.second;
			_queuedUpdates.erase(_queuedUpdates.begin());
			applyEnqueuedUpdate(update);
		} else {
			break;
		}
	}
	if (_queuedUpdates.empty()) {
		_reloadByQueuedUpdatesTimer.cancel();
	} else if (_queuedUpdates.size() != size
		|| !_reloadByQueuedUpdatesTimer.isActive()) {
		_reloadByQueuedUpdatesTimer.callOnce(kWaitForUpdatesTimeout);
	}
}

void GroupCall::computeParticipantsCount() {
	_fullCount = _allParticipantsLoaded
		? int(_participants.size())
		: std::max(int(_participants.size()), _serverParticipantsCount);
}

void GroupCall::reload() {
	if (_reloadRequestId || _applyingQueuedUpdates) {
		return;
	} else if (_participantsRequestId) {
		api().request(_participantsRequestId).cancel();
		_participantsRequestId = 0;
	}

	DEBUG_LOG(("Group Call Participants: "
		"Reloading with queued: %1"
		).arg(_queuedUpdates.size()));

	while (!_queuedUpdates.empty()) {
		const auto &entry = _queuedUpdates.front();
		const auto update = entry.second;
		_queuedUpdates.erase(_queuedUpdates.begin());
		applyEnqueuedUpdate(update);
	}
	_reloadByQueuedUpdatesTimer.cancel();

	_reloadRequestId = api().request(
		MTPphone_GetGroupCall(input())
	).done([=](const MTPphone_GroupCall &result) {
		if (requestParticipantsAfterReload(result)) {
			_savedFull = result;
			processFullCallUsersChats(result);
			requestParticipants();
			return;
		}
		_reloadRequestId = 0;
		processFullCall(result);
	}).fail([=](const MTP::Error &error) {
		_reloadRequestId = 0;
	}).send();
}

bool GroupCall::requestParticipantsAfterReload(
		const MTPphone_GroupCall &call) const {
	return call.match([&](const MTPDphone_groupCall &data) {
		const auto received = data.vparticipants().v.size();
		const auto size = data.vcall().match([&](const MTPDgroupCall &data) {
			return data.vparticipants_count().v;
		}, [](const auto &) {
			return 0;
		});
		return (received < size) && (received < _participants.size());
	});
}

void GroupCall::applyParticipantsSlice(
		const QVector<MTPGroupCallParticipant> &list,
		ApplySliceSource sliceSource) {
	const auto amInCall = inCall();
	const auto now = base::unixtime::now();
	const auto speakingAfterActive = TimeId(kSpeakingAfterActive / 1000);

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
				if (_serverParticipantsCount > 0) {
					--_serverParticipantsCount;
				}
				return;
			}
			if (const auto about = data.vabout()) {
				participantPeer->setAbout(qs(*about));
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
			const auto raisedHandRating
				= data.vraise_hand_rating().value_or_empty();
			const auto value = Participant{
				.peer = participantPeer,
				.date = data.vdate().v,
				.lastActive = lastActive,
				.raisedHandRating = raisedHandRating,
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
				++_serverParticipantsCount;
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
		changePeerEmptyCallFlag();
		computeParticipantsCount();
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

void GroupCall::resolveParticipants(const base::flat_set<uint32> &ssrcs) {
	if (ssrcs.empty()) {
		return;
	}
	for (const auto ssrc : ssrcs) {
		_unknownSpokenSsrcs.emplace(ssrc, LastSpokeTimes());
	}
	requestUnknownParticipants();
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
	auto nearest = crl::time(0);
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
	for (const auto &[ssrc, when] : ssrcs) {
		ssrcInputs.push_back(MTP_int(ssrc));
	}
	auto peerInputs = QVector<MTPInputPeer>();
	peerInputs.reserve(participantPeerIds.size());
	for (const auto &[participantPeerId, when] : participantPeerIds) {
		if (const auto userId = peerToUser(participantPeerId)) {
			peerInputs.push_back(
				MTP_inputPeerUser(MTP_int(userId.bare), MTP_long(0))); // #TODO ids
		} else if (const auto chatId = peerToChat(participantPeerId)) {
			peerInputs.push_back(MTP_inputPeerChat(MTP_int(chatId.bare))); // #TODO ids
		} else if (const auto channelId = peerToChannel(participantPeerId)) {
			peerInputs.push_back(
				MTP_inputPeerChannel(MTP_int(channelId.bare), MTP_long(0))); // #TODO ids
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
			_peer->owner().processChats(data.vchats());
			applyParticipantsSlice(
				data.vparticipants().v,
				ApplySliceSource::UnknownLoaded);
		});
		_unknownParticipantPeersRequestId = 0;
		const auto now = crl::now();
		for (const auto &[ssrc, when] : ssrcs) {
			if (when.voice || when.anything) {
				applyLastSpoke(ssrc, when, now);
			}
			_unknownSpokenSsrcs.remove(ssrc);
		}
		for (const auto &[id, when] : participantPeerIds) {
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
	}).fail([=](const MTP::Error &error) {
		_unknownParticipantPeersRequestId = 0;
		for (const auto &[ssrc, when] : ssrcs) {
			_unknownSpokenSsrcs.remove(ssrc);
		}
		for (const auto &[participantPeerId, when] : participantPeerIds) {
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

void GroupCall::setJoinMutedLocally(bool muted) {
	_joinMuted = muted;
}

bool GroupCall::joinMuted() const {
	return _joinMuted;
}

bool GroupCall::canChangeJoinMuted() const {
	return _canChangeJoinMuted;
}

bool GroupCall::joinedToTop() const {
	return _joinedToTop;
}

ApiWrap &GroupCall::api() const {
	return _peer->session().api();
}

} // namespace Data
