/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class UserData;
class PeerData;

class ApiWrap;

namespace Data {

struct LastSpokeTimes {
	crl::time anything = 0;
	crl::time voice = 0;
};

class GroupCall final {
public:
	GroupCall(not_null<PeerData*> peer, uint64 id, uint64 accessHash);
	~GroupCall();

	[[nodiscard]] uint64 id() const;
	[[nodiscard]] not_null<PeerData*> peer() const;
	[[nodiscard]] MTPInputGroupCall input() const;

	void setPeer(not_null<PeerData*> peer);

	struct Participant {
		not_null<UserData*> user;
		TimeId date = 0;
		TimeId lastActive = 0;
		uint32 ssrc = 0;
		bool sounding = false;
		bool speaking = false;
		bool muted = false;
		bool canSelfUnmute = false;
	};
	struct ParticipantUpdate {
		std::optional<Participant> was;
		std::optional<Participant> now;
	};

	static constexpr auto kSoundStatusKeptFor = crl::time(350);

	[[nodiscard]] auto participants() const
		-> const std::vector<Participant> &;
	void requestParticipants();
	[[nodiscard]] bool participantsLoaded() const;
	[[nodiscard]] UserData *userBySsrc(uint32 ssrc) const;

	[[nodiscard]] rpl::producer<> participantsSliceAdded();
	[[nodiscard]] rpl::producer<ParticipantUpdate> participantUpdated() const;

	void applyUpdate(const MTPGroupCall &update);
	void applyUpdate(const MTPDupdateGroupCallParticipants &update);
	void applyUpdateChecked(
		const MTPDupdateGroupCallParticipants &update);
	void applyLastSpoke(uint32 ssrc, LastSpokeTimes when, crl::time now);
	void applyActiveUpdate(
		UserId userId,
		LastSpokeTimes when,
		UserData *userLoaded);

	[[nodiscard]] int fullCount() const;
	[[nodiscard]] rpl::producer<int> fullCountValue() const;

	void setInCall();
	void reload();

	void setJoinMutedLocally(bool muted);
	[[nodiscard]] bool joinMuted() const;
	[[nodiscard]] bool canChangeJoinMuted() const;

private:
	enum class ApplySliceSource {
		SliceLoaded,
		UnknownLoaded,
		UpdateReceived,
	};
	[[nodiscard]] ApiWrap &api() const;

	[[nodiscard]] bool inCall() const;
	void applyCall(const MTPGroupCall &call, bool force);
	void applyParticipantsSlice(
		const QVector<MTPGroupCallParticipant> &list,
		ApplySliceSource sliceSource);
	void requestUnknownParticipants();
	void changePeerEmptyCallFlag();
	void checkFinishSpeakingByActive();

	const uint64 _id = 0;
	const uint64 _accessHash = 0;

	not_null<PeerData*> _peer;
	int _version = 0;
	mtpRequestId _participantsRequestId = 0;
	mtpRequestId _reloadRequestId = 0;

	std::vector<Participant> _participants;
	base::flat_map<uint32, not_null<UserData*>> _userBySsrc;
	base::flat_map<not_null<UserData*>, crl::time> _speakingByActiveFinishes;
	base::Timer _speakingByActiveFinishTimer;
	QString _nextOffset;
	rpl::variable<int> _fullCount = 0;

	base::flat_map<uint32, LastSpokeTimes> _unknownSpokenSsrcs;
	base::flat_map<UserId, LastSpokeTimes> _unknownSpokenUids;
	mtpRequestId _unknownUsersRequestId = 0;

	rpl::event_stream<ParticipantUpdate> _participantUpdates;
	rpl::event_stream<> _participantsSliceAdded;

	bool _joinMuted = false;
	bool _canChangeJoinMuted = true;
	bool _allReceived = false;

};

} // namespace Data
