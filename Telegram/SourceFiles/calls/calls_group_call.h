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

class History;

namespace tgcalls {
class GroupInstanceImpl;
} // namespace tgcalls

namespace base {
class GlobalShortcutManager;
class GlobalShortcutValue;
} // namespace base

namespace Calls {

enum class MuteState {
	Active,
	PushToTalk,
	Muted,
	ForceMuted,
};

[[nodiscard]] inline auto MapPushToTalkToActive() {
	return rpl::map([=](MuteState state) {
		return (state == MuteState::PushToTalk) ? MuteState::Active : state;
	});
}

struct LevelUpdate {
	uint32 ssrc = 0;
	float value = 0.;
	bool self = false;
};

class GroupCall final : public base::has_weak_ptr {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;

		virtual void groupCallFinished(not_null<GroupCall*> call) = 0;
		virtual void groupCallFailed(not_null<GroupCall*> call) = 0;
		virtual void groupCallRequestPermissionsOrFail(
			Fn<void()> onSuccess) = 0;
	};

	using GlobalShortcutManager = base::GlobalShortcutManager;

	GroupCall(
		not_null<Delegate*> delegate,
		not_null<PeerData*> peer,
		const MTPInputGroupCall &inputCall);
	~GroupCall();

	[[nodiscard]] uint64 id() const {
		return _id;
	}
	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}

	void start();
	void hangup();
	void discard();
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

	enum State {
		Creating,
		Joining,
		Connecting,
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

	[[nodiscard]] rpl::producer<LevelUpdate> levelUpdates() const {
		return _levelUpdates.events();
	}
	static constexpr auto kSpeakLevelThreshold = 0.2;

	void setCurrentAudioDevice(bool input, const QString &deviceId);
	//void setAudioVolume(bool input, float level);
	void setAudioDuckingEnabled(bool enabled);

	void toggleMute(not_null<UserData*> user, bool mute);
	std::variant<int, not_null<UserData*>> inviteUsers(
		const std::vector<not_null<UserData*>> &users);

	std::shared_ptr<GlobalShortcutManager> ensureGlobalShortcutManager();
	void applyGlobalShortcutChanges();

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	using GlobalShortcutValue = base::GlobalShortcutValue;

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
	void maybeSendMutedUpdate(MuteState previous);
	void sendMutedUpdate();
	void updateInstanceMuteState();
	void applySelfInCallLocally();
	void rejoin();

	void myLevelUpdated(float level);
	void audioLevelsUpdated(
		const std::vector<std::pair<std::uint32_t, float>> &data);
	void handleLevelsUpdated(
		gsl::span<const std::pair<std::uint32_t, float>> data);
	void setInstanceConnected(bool connected);
	void checkLastSpoke();
	void pushToTalkCancel();

	void checkGlobalShortcutAvailability();
	void checkJoined();

	[[nodiscard]] MTPInputGroupCall inputCall() const;

	const not_null<Delegate*> _delegate;
	not_null<PeerData*> _peer; // Can change in legacy group migration.
	not_null<History*> _history; // Can change in legacy group migration.
	MTP::Sender _api;
	rpl::variable<State> _state = State::Creating;
	bool _instanceConnected = false;

	rpl::variable<MuteState> _muted = MuteState::Muted;
	bool _acceptFields = false;

	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint32 _mySsrc = 0;
	mtpRequestId _createRequestId = 0;
	mtpRequestId _updateMuteRequestId = 0;

	std::unique_ptr<tgcalls::GroupInstanceImpl> _instance;
	rpl::event_stream<LevelUpdate> _levelUpdates;
	base::flat_map<uint32, crl::time> _lastSpoke;
	base::Timer _lastSpokeCheckTimer;
	base::Timer _checkJoinedTimer;

	crl::time _lastSendProgressUpdate = 0;

	std::shared_ptr<GlobalShortcutManager> _shortcutManager;
	std::shared_ptr<GlobalShortcutValue> _pushToTalk;
	base::Timer _pushToTalkCancelTimer;
	bool _pushToTalkStarted = false;

	rpl::lifetime _lifetime;

};

} // namespace Calls
