/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class PeerData;

class ApiWrap;

namespace Calls {
struct ParticipantVideoParams;
} // namespace Calls

namespace Data {

struct LastSpokeTimes {
	crl::time anything = 0;
	crl::time voice = 0;
};

struct GroupCallParticipant {
	not_null<PeerData*> peer;
	std::shared_ptr<Calls::ParticipantVideoParams> videoParams;
	TimeId date = 0;
	TimeId lastActive = 0;
	uint64 raisedHandRating = 0;
	uint32 ssrc = 0;
	int volume = 0;
	bool sounding : 1;
	bool speaking : 1;
	bool additionalSounding : 1;
	bool additionalSpeaking : 1;
	bool muted : 1;
	bool mutedByMe : 1;
	bool canSelfUnmute : 1;
	bool onlyMinLoaded : 1;
	bool videoJoined = false;
	bool applyVolumeFromMin = true;

	[[nodiscard]] const std::string &cameraEndpoint() const;
	[[nodiscard]] const std::string &screenEndpoint() const;
	[[nodiscard]] bool cameraPaused() const;
	[[nodiscard]] bool screenPaused() const;
};

class GroupCall final {
public:
	GroupCall(
		not_null<PeerData*> peer,
		uint64 id,
		uint64 accessHash,
		TimeId scheduleDate);
	~GroupCall();

	[[nodiscard]] uint64 id() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] not_null<PeerData*> peer() const;
	[[nodiscard]] MTPInputGroupCall input() const;
	[[nodiscard]] QString title() const {
		return _title.current();
	}
	[[nodiscard]] rpl::producer<QString> titleValue() const {
		return _title.value();
	}
	void setTitle(const QString &title) {
		_title = title;
	}
	[[nodiscard]] TimeId recordStartDate() const {
		return _recordStartDate.current();
	}
	[[nodiscard]] rpl::producer<TimeId> recordStartDateValue() const {
		return _recordStartDate.value();
	}
	[[nodiscard]] rpl::producer<TimeId> recordStartDateChanges() const {
		return _recordStartDate.changes();
	}
	[[nodiscard]] TimeId scheduleDate() const {
		return _scheduleDate.current();
	}
	[[nodiscard]] rpl::producer<TimeId> scheduleDateValue() const {
		return _scheduleDate.value();
	}
	[[nodiscard]] rpl::producer<TimeId> scheduleDateChanges() const {
		return _scheduleDate.changes();
	}
	[[nodiscard]] bool scheduleStartSubscribed() const {
		return _scheduleStartSubscribed.current();
	}
	[[nodiscard]] rpl::producer<bool> scheduleStartSubscribedValue() const {
		return _scheduleStartSubscribed.value();
	}
	[[nodiscard]] int unmutedVideoLimit() const {
		return _unmutedVideoLimit.current();
	}

	void setPeer(not_null<PeerData*> peer);

	using Participant = GroupCallParticipant;
	struct ParticipantUpdate {
		std::optional<Participant> was;
		std::optional<Participant> now;
	};

	static constexpr auto kSoundStatusKeptFor = crl::time(1500);

	[[nodiscard]] auto participants() const
		-> const std::vector<Participant> &;
	void requestParticipants();
	[[nodiscard]] bool participantsLoaded() const;
	[[nodiscard]] PeerData *participantPeerByAudioSsrc(uint32 ssrc) const;
	[[nodiscard]] const Participant *participantByPeer(
		not_null<PeerData*> peer) const;
	[[nodiscard]] const Participant *participantByEndpoint(
		const std::string &endpoint) const;

	[[nodiscard]] rpl::producer<> participantsReloaded();
	[[nodiscard]] auto participantUpdated() const
		-> rpl::producer<ParticipantUpdate>;
	[[nodiscard]] auto participantSpeaking() const
		-> rpl::producer<not_null<Participant*>>;

	void enqueueUpdate(const MTPUpdate &update);
	void applyLocalUpdate(
		const MTPDupdateGroupCallParticipants &update);

	void applyLastSpoke(uint32 ssrc, LastSpokeTimes when, crl::time now);
	void applyActiveUpdate(
		PeerId participantPeerId,
		LastSpokeTimes when,
		PeerData *participantPeerLoaded);

	void resolveParticipants(const base::flat_set<uint32> &ssrcs);
	[[nodiscard]] rpl::producer<
		not_null<const base::flat_map<
			uint32,
			LastSpokeTimes>*>> participantsResolved() const {
		return _participantsResolved.events();
	}

	[[nodiscard]] int fullCount() const;
	[[nodiscard]] rpl::producer<int> fullCountValue() const;

	void setInCall();
	void reload();
	void processFullCall(const MTPphone_GroupCall &call);

	void setJoinMutedLocally(bool muted);
	[[nodiscard]] bool joinMuted() const;
	[[nodiscard]] bool canChangeJoinMuted() const;
	[[nodiscard]] bool joinedToTop() const;

private:
	enum class ApplySliceSource {
		FullReloaded,
		SliceLoaded,
		UnknownLoaded,
		UpdateReceived,
		UpdateConstructed,
	};
	enum class QueuedType : uint8 {
		VersionedParticipant,
		Participant,
		Call,
	};
	[[nodiscard]] ApiWrap &api() const;

	void discard(const MTPDgroupCallDiscarded &data);
	[[nodiscard]] bool inCall() const;
	void applyParticipantsSlice(
		const QVector<MTPGroupCallParticipant> &list,
		ApplySliceSource sliceSource);
	void requestUnknownParticipants();
	void changePeerEmptyCallFlag();
	void checkFinishSpeakingByActive();
	void applyCallFields(const MTPDgroupCall &data);
	void applyEnqueuedUpdate(const MTPUpdate &update);
	void setServerParticipantsCount(int count);
	void computeParticipantsCount();
	void processQueuedUpdates();
	void processFullCallUsersChats(const MTPphone_GroupCall &call);
	void processFullCallFields(const MTPphone_GroupCall &call);
	[[nodiscard]] bool requestParticipantsAfterReload(
		const MTPphone_GroupCall &call) const;
	[[nodiscard]] bool processSavedFullCall();
	void finishParticipantsSliceRequest();
	[[nodiscard]] Participant *findParticipant(not_null<PeerData*> peer);

	const uint64 _id = 0;
	const uint64 _accessHash = 0;

	not_null<PeerData*> _peer;
	int _version = 0;
	mtpRequestId _participantsRequestId = 0;
	mtpRequestId _reloadRequestId = 0;
	rpl::variable<QString> _title;

	base::flat_multi_map<
		std::pair<int, QueuedType>,
		MTPUpdate> _queuedUpdates;
	base::Timer _reloadByQueuedUpdatesTimer;
	std::optional<MTPphone_GroupCall> _savedFull;

	std::vector<Participant> _participants;
	base::flat_map<uint32, not_null<PeerData*>> _participantPeerByAudioSsrc;
	base::flat_map<not_null<PeerData*>, crl::time> _speakingByActiveFinishes;
	base::Timer _speakingByActiveFinishTimer;
	QString _nextOffset;
	int _serverParticipantsCount = 0;
	rpl::variable<int> _fullCount = 0;
	rpl::variable<int> _unmutedVideoLimit = 0;
	rpl::variable<TimeId> _recordStartDate = 0;
	rpl::variable<TimeId> _scheduleDate = 0;
	rpl::variable<bool> _scheduleStartSubscribed = false;

	base::flat_map<uint32, LastSpokeTimes> _unknownSpokenSsrcs;
	base::flat_map<PeerId, LastSpokeTimes> _unknownSpokenPeerIds;
	rpl::event_stream<
		not_null<const base::flat_map<
			uint32,
			LastSpokeTimes>*>> _participantsResolved;
	mtpRequestId _unknownParticipantPeersRequestId = 0;

	rpl::event_stream<ParticipantUpdate> _participantUpdates;
	rpl::event_stream<not_null<Participant*>> _participantSpeaking;
	rpl::event_stream<> _participantsReloaded;

	bool _joinMuted = false;
	bool _canChangeJoinMuted = true;
	bool _allParticipantsLoaded = false;
	bool _joinedToTop = false;
	bool _applyingQueuedUpdates = false;

};

} // namespace Data
