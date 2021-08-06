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
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
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

const std::string &GroupCallParticipant::cameraEndpoint() const {
	return GetCameraEndpoint(videoParams);
}

const std::string &GroupCallParticipant::screenEndpoint() const {
	return GetScreenEndpoint(videoParams);
}

bool GroupCallParticipant::cameraPaused() const {
	return IsCameraPaused(videoParams);
}

bool GroupCallParticipant::screenPaused() const {
	return IsScreenPaused(videoParams);
}

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
		result.match([&](const MTPDphone_groupParticipants &data) {
			_participantsRequestId = 0;
			const auto reloaded = processSavedFullCall();
			_nextOffset = qs(data.vnext_offset());
			_peer->owner().processUsers(data.vusers());
			_peer->owner().processChats(data.vchats());
			applyParticipantsSlice(
				data.vparticipants().v,
				(reloaded
					? ApplySliceSource::FullReloaded
					: ApplySliceSource::SliceLoaded));
			setServerParticipantsCount(data.vcount().v);
			if (data.vparticipants().v.isEmpty()) {
				_allParticipantsLoaded = true;
			}
			finishParticipantsSliceRequest();
			if (reloaded) {
				_participantsReloaded.fire({});
			}
		});
	}).fail([=](const MTP::Error &error) {
		_participantsRequestId = 0;
		const auto reloaded = processSavedFullCall();
		setServerParticipantsCount(_participants.size());
		_allParticipantsLoaded = true;
		finishParticipantsSliceRequest();
		if (reloaded) {
			_participantsReloaded.fire({});
		}
	}).send();
}

bool GroupCall::processSavedFullCall() {
	if (!_savedFull) {
		return false;
	}
	_reloadRequestId = 0;
	processFullCallFields(*base::take(_savedFull));
	return true;
}

void GroupCall::finishParticipantsSliceRequest() {
	computeParticipantsCount();
	processQueuedUpdates();
}

void GroupCall::setServerParticipantsCount(int count) {
	_serverParticipantsCount = count;
	changePeerEmptyCallFlag();
}

void GroupCall::changePeerEmptyCallFlag() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	constexpr auto chatFlag = ChatDataFlag::CallNotEmpty;
	constexpr auto channelFlag = ChannelDataFlag::CallNotEmpty;
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

PeerData *GroupCall::participantPeerByAudioSsrc(uint32 ssrc) const {
	const auto i = _participantPeerByAudioSsrc.find(ssrc);
	return (i != end(_participantPeerByAudioSsrc))
		? i->second.get()
		: nullptr;
}

const GroupCallParticipant *GroupCall::participantByPeer(
		not_null<PeerData*> peer) const {
	return const_cast<GroupCall*>(this)->findParticipant(peer);
}

GroupCallParticipant *GroupCall::findParticipant(
		not_null<PeerData*> peer) {
	const auto i = ranges::find(_participants, peer, &Participant::peer);
	return (i != end(_participants)) ? &*i : nullptr;
}

const GroupCallParticipant *GroupCall::participantByEndpoint(
		const std::string &endpoint) const {
	if (endpoint.empty()) {
		return nullptr;
	}
	for (const auto &participant : _participants) {
		if (GetCameraEndpoint(participant.videoParams) == endpoint
			|| GetScreenEndpoint(participant.videoParams) == endpoint) {
			return &participant;
		}
	}
	return nullptr;
}

rpl::producer<> GroupCall::participantsReloaded() {
	return _participantsReloaded.events();
}

auto GroupCall::participantUpdated() const
-> rpl::producer<ParticipantUpdate> {
	return _participantUpdates.events();
}

auto GroupCall::participantSpeaking() const
-> rpl::producer<not_null<Participant*>> {
	return _participantSpeaking.events();
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
			_participantPeerByAudioSsrc.clear();
			_allParticipantsLoaded = false;

			applyParticipantsSlice(
				participants,
				ApplySliceSource::FullReloaded);
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
	_participantsReloaded.fire({});
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
	_unmutedVideoLimit = data.vunmuted_video_limit().v;
	_allParticipantsLoaded
		= (_serverParticipantsCount == _participants.size());
}

void GroupCall::applyLocalUpdate(
		const MTPDupdateGroupCallParticipants &update) {
	applyParticipantsSlice(
		update.vparticipants().v,
		ApplySliceSource::UpdateConstructed);
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
					_participantPeerByAudioSsrc.erase(i->ssrc);
					_participantPeerByAudioSsrc.erase(
						GetAdditionalAudioSsrc(i->videoParams));
					_speakingByActiveFinishes.remove(participantPeer);
					_participants.erase(i);
					if (sliceSource != ApplySliceSource::FullReloaded) {
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
			const auto videoJoined = data.is_video_joined();
			const auto raisedHandRating
				= data.vraise_hand_rating().value_or_empty();
			const auto localUpdate = (sliceSource
				== ApplySliceSource::UpdateConstructed);
			const auto existingVideoParams = (i != end(_participants))
				? i->videoParams
				: nullptr;
			auto videoParams = localUpdate
				? existingVideoParams
				: Calls::ParseVideoParams(
					data.vvideo(),
					data.vpresentation(),
					existingVideoParams);
			const auto value = Participant{
				.peer = participantPeer,
				.videoParams = std::move(videoParams),
				.date = data.vdate().v,
				.lastActive = lastActive,
				.raisedHandRating = raisedHandRating,
				.ssrc = uint32(data.vsource().v),
				.volume = volume,
				.sounding = canSelfUnmute && was && was->sounding,
				.speaking = canSelfUnmute && was && was->speaking,
				.additionalSounding = (canSelfUnmute
					&& was
					&& was->additionalSounding),
				.additionalSpeaking = (canSelfUnmute
					&& was
					&& was->additionalSpeaking),
				.muted = data.is_muted(),
				.mutedByMe = mutedByMe,
				.canSelfUnmute = canSelfUnmute,
				.onlyMinLoaded = onlyMinLoaded,
				.videoJoined = videoJoined,
				.applyVolumeFromMin = applyVolumeFromMin,
			};
			if (i == end(_participants)) {
				if (value.ssrc) {
					_participantPeerByAudioSsrc.emplace(
						value.ssrc,
						participantPeer);
				}
				if (const auto additional = GetAdditionalAudioSsrc(
						value.videoParams)) {
					_participantPeerByAudioSsrc.emplace(
						additional,
						participantPeer);
				}
				_participants.push_back(value);
				if (const auto user = participantPeer->asUser()) {
					_peer->owner().unregisterInvitedToCallUser(_id, user);
				}
			} else {
				if (i->ssrc != value.ssrc) {
					_participantPeerByAudioSsrc.erase(i->ssrc);
					if (value.ssrc) {
						_participantPeerByAudioSsrc.emplace(
							value.ssrc,
							participantPeer);
					}
				}
				if (GetAdditionalAudioSsrc(i->videoParams)
					!= GetAdditionalAudioSsrc(value.videoParams)) {
					_participantPeerByAudioSsrc.erase(
						GetAdditionalAudioSsrc(i->videoParams));
					if (const auto additional = GetAdditionalAudioSsrc(
						value.videoParams)) {
						_participantPeerByAudioSsrc.emplace(
							additional,
							participantPeer);
					}
				}
				*i = value;
			}
			if (data.is_just_joined()) {
				++_serverParticipantsCount;
			}
			if (sliceSource != ApplySliceSource::FullReloaded) {
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
	const auto i = _participantPeerByAudioSsrc.find(ssrc);
	if (i == end(_participantPeerByAudioSsrc)) {
		_unknownSpokenSsrcs[ssrc] = when;
		requestUnknownParticipants();
		return;
	}
	const auto participant = findParticipant(i->second);
	Assert(participant != nullptr);

	_speakingByActiveFinishes.remove(participant->peer);
	const auto sounding = (when.anything + kSoundStatusKeptFor >= now)
		&& participant->canSelfUnmute;
	const auto speaking = sounding
		&& (when.voice + kSoundStatusKeptFor >= now);
	if (speaking) {
		_participantSpeaking.fire({ participant });
	}
	const auto useAdditional = (ssrc != participant->ssrc);
	const auto nowSounding = useAdditional
		? participant->additionalSounding
		: participant->sounding;
	const auto nowSpeaking = useAdditional
		? participant->additionalSpeaking
		: participant->speaking;
	if (nowSounding != sounding || nowSpeaking != speaking) {
		const auto was = *participant;
		if (useAdditional) {
			participant->additionalSounding = sounding;
			participant->additionalSpeaking = speaking;
		} else {
			participant->sounding = sounding;
			participant->speaking = speaking;
		}
		_participantUpdates.fire({
			.was = was,
			.now = *participant,
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
	const auto participant = participantPeerLoaded
		? findParticipant(participantPeerLoaded)
		: nullptr;
	const auto loadByUserId = !participant || participant->onlyMinLoaded;
	if (loadByUserId) {
		_unknownSpokenPeerIds[participantPeerId] = when;
		requestUnknownParticipants();
	}
	if (!participant || !participant->canSelfUnmute) {
		return;
	}
	const auto was = std::make_optional(*participant);
	const auto now = crl::now();
	const auto elapsed = TimeId((now - when.anything) / crl::time(1000));
	const auto lastActive = base::unixtime::now() - elapsed;
	const auto finishes = when.anything + kSpeakingAfterActive;
	if (lastActive <= participant->lastActive || finishes <= now) {
		return;
	}
	_speakingByActiveFinishes[participant->peer] = finishes;
	if (!_speakingByActiveFinishTimer.isActive()) {
		_speakingByActiveFinishTimer.callOnce(finishes - now);
	}

	participant->lastActive = lastActive;
	participant->speaking = true;
	participant->canSelfUnmute = true;
	if (!was->speaking || !was->canSelfUnmute) {
		_participantUpdates.fire({
			.was = was,
			.now = *participant,
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
		const auto participant = findParticipant(participantPeer);
		Assert(participant != nullptr);
		if (participant->speaking) {
			const auto was = *participant;
			participant->speaking = false;
			_participantUpdates.fire({
				.was = was,
				.now = *participant,
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
		if (!ssrcs.empty()) {
			_participantsResolved.fire(&ssrcs);
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
