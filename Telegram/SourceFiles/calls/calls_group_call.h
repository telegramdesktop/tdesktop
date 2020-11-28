/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "base/bytes.h"
#include "mtproto/sender.h"
#include "mtproto/mtproto_auth_key.h"

namespace tgcalls {
class GroupInstanceImpl;
} // namespace tgcalls

namespace Calls {

enum class MuteState {
	Active,
	Muted,
	ForceMuted,
};

class GroupCall final : public base::has_weak_ptr {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;

		virtual void groupCallFinished(not_null<GroupCall*> call) = 0;
		virtual void groupCallFailed(not_null<GroupCall*> call) = 0;

	};

	GroupCall(
		not_null<Delegate*> delegate,
		not_null<ChannelData*> channel,
		const MTPInputGroupCall &inputCall);
	~GroupCall();

	[[nodiscard]] uint64 id() const {
		return _id;
	}
	[[nodiscard]] not_null<ChannelData*> channel() const {
		return _channel;
	}

	void start();
	void hangup();
	void join(const MTPInputGroupCall &inputCall);
	void handleUpdate(const MTPGroupCall &call);
	void handleUpdate(const MTPDupdateGroupCallParticipants &data);

	void setMuted(MuteState mute);
	[[nodiscard]] MuteState muted() const {
		return _muted.current();
	}
	[[nodiscard]] rpl::producer<MuteState> mutedValue() const {
		return _muted.value();
	}
	[[nodiscard]] bool joined() const {
		return (_state.current() == State::Joined);
	}

	enum State {
		Creating,
		Joining,
		Joined,
		FailedHangingUp,
		Failed,
		HangingUp,
		Ended,
	};
	[[nodiscard]] State state() const {
		return _state.current();
	}
	[[nodiscard]] rpl::producer<State> stateValue() const {
		return _state.value();
	}

	void setCurrentAudioDevice(bool input, const QString &deviceId);
	//void setAudioVolume(bool input, float level);
	void setAudioDuckingEnabled(bool enabled);

	void toggleMute(not_null<UserData*> user, bool mute);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	enum class FinishType {
		None,
		Ended,
		Failed,
	};

	void handleRequestError(const RPCError &error);
	void handleControllerError(const QString &error);
	void createAndStartController();
	void destroyController();

	void setState(State state);
	void finish(FinishType type);
	void sendMutedUpdate();
	void applySelfInCallLocally();
	void rejoin();

	void myLevelUpdated(float level);

	[[nodiscard]] MTPInputGroupCall inputCall() const;

	const not_null<Delegate*> _delegate;
	const not_null<ChannelData*> _channel;
	MTP::Sender _api;
	rpl::variable<State> _state = State::Creating;

	rpl::variable<MuteState> _muted = MuteState::Muted;
	bool _acceptFields = false;

	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint32 _mySsrc = 0;
	mtpRequestId _updateMuteRequestId = 0;

	std::unique_ptr<tgcalls::GroupInstanceImpl> _instance;

	rpl::lifetime _lifetime;

};

} // namespace Calls
