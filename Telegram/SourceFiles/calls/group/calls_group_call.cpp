/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_call.h"

#include "calls/group/calls_group_common.h"
#include "main/main_session.h"
#include "api/api_send_progress.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "lang/lang_hardcoded.h"
#include "boxes/peers/edit_participants_box.h" // SubscribeToMigration.
#include "ui/toasts/common_toasts.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "base/global_shortcuts.h"
#include "base/openssl_help.h"
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_media_devices.h"
#include "webrtc/webrtc_create_adm.h"

#include <tgcalls/group/GroupInstanceCustomImpl.h>
#include <tgcalls/VideoCaptureInterface.h>
#include <tgcalls/StaticThreads.h>
#include <xxhash.h>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

namespace Calls {
namespace {

constexpr auto kMaxInvitePerSlice = 10;
constexpr auto kCheckLastSpokeInterval = crl::time(1000);
constexpr auto kCheckJoinedTimeout = 4 * crl::time(1000);
constexpr auto kUpdateSendActionEach = crl::time(500);
constexpr auto kPlayConnectingEach = crl::time(1056) + 2 * crl::time(1000);

[[nodiscard]] std::unique_ptr<Webrtc::MediaDevices> CreateMediaDevices() {
	const auto &settings = Core::App().settings();
	return Webrtc::CreateMediaDevices(
		settings.callInputDeviceId(),
		settings.callOutputDeviceId(),
		settings.callVideoInputDeviceId());
}

[[nodiscard]] const Data::GroupCall::Participant *LookupParticipant(
		not_null<PeerData*> peer,
		uint64 id,
		not_null<PeerData*> participantPeer) {
	const auto call = peer->groupCall();
	if (!id || !call || call->id() != id) {
		return nullptr;
	}
	const auto &participants = call->participants();
	const auto i = ranges::find(
		participants,
		participantPeer,
		&Data::GroupCall::Participant::peer);
	return (i != end(participants)) ? &*i : nullptr;
}

[[nodiscard]] double TimestampFromMsgId(mtpMsgId msgId) {
	return msgId / double(1ULL << 32);
}

[[nodiscard]] std::string ReadJsonString(
		const QJsonObject &object,
		const char *key) {
	return object.value(key).toString().toStdString();
}

[[nodiscard]] uint64 FindLocalRaisedHandRating(
		const std::vector<Data::GroupCallParticipant> &list) {
	const auto i = ranges::max_element(
		list,
		ranges::less(),
		&Data::GroupCallParticipant::raisedHandRating);
	return (i == end(list)) ? 1 : (i->raisedHandRating + 1);
}

[[nodiscard]] std::string ParseVideoEndpoint(const QByteArray &json) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("API Error: "
			"Failed to parse presentation video params, error: %1."
			).arg(error.errorString()));
		return {};
	} else if (!document.isObject()) {
		LOG(("API Error: "
			"Not an object received in presentation video params."));
		return {};
	}
	const auto video = document.object().value("video").toObject();
	return video.value("endpoint").toString().toStdString();
}

[[nodiscard]] const std::string &EmptyString() {
	static const auto result = std::string();
	return result;
}

} // namespace

class GroupCall::LoadPartTask final : public tgcalls::BroadcastPartTask {
public:
	LoadPartTask(
		base::weak_ptr<GroupCall> call,
		int64 time,
		int64 period,
		Fn<void(tgcalls::BroadcastPart&&)> done);

	[[nodiscard]] int64 time() const {
		return _time;
	}
	[[nodiscard]] int32 scale() const {
		return _scale;
	}

	void done(tgcalls::BroadcastPart &&part);
	void cancel() override;

private:
	const base::weak_ptr<GroupCall> _call;
	const int64 _time = 0;
	const int32 _scale = 0;
	Fn<void(tgcalls::BroadcastPart &&)> _done;
	QMutex _mutex;

};

class GroupCall::MediaChannelDescriptionsTask final
	: public tgcalls::RequestMediaChannelDescriptionTask {
public:
	MediaChannelDescriptionsTask(
		base::weak_ptr<GroupCall> call,
		const std::vector<std::uint32_t> &ssrcs,
		Fn<void(std::vector<tgcalls::MediaChannelDescription>&&)> done);

	[[nodiscard]] base::flat_set<uint32> ssrcs() const;

	[[nodiscard]] bool finishWithAdding(
		uint32 ssrc,
		std::optional<tgcalls::MediaChannelDescription> description,
		bool screen = false);

	void cancel() override;

private:
	const base::weak_ptr<GroupCall> _call;
	base::flat_set<uint32> _ssrcs;
	base::flat_set<uint32> _cameraAdded;
	base::flat_set<uint32> _screenAdded;
	std::vector<tgcalls::MediaChannelDescription> _result;
	Fn<void(std::vector<tgcalls::MediaChannelDescription>&&)> _done;
	QMutex _mutex;

};

struct GroupCall::LargeTrack {
	LargeTrack() : track(Webrtc::VideoState::Active) {
	}

	Webrtc::VideoTrack track;
	std::shared_ptr<Webrtc::SinkInterface> sink;
};

struct GroupCall::SinkPointer {
	std::shared_ptr<Webrtc::SinkInterface> data;
};

[[nodiscard]] bool IsGroupCallAdmin(
		not_null<PeerData*> peer,
		not_null<PeerData*> participantPeer) {
	const auto user = participantPeer->asUser();
	if (!user) {
		return false;
	}
	if (const auto chat = peer->asChat()) {
		return chat->admins.contains(user)
			|| (chat->creator == peerToUser(user->id));
	} else if (const auto group = peer->asChannel()) {
		if (const auto mgInfo = group->mgInfo.get()) {
			if (mgInfo->creator == user) {
				return true;
			}
			const auto i = mgInfo->lastAdmins.find(user);
			if (i == mgInfo->lastAdmins.end()) {
				return false;
			}
			const auto &rights = i->second.rights;
			return rights.c_chatAdminRights().is_manage_call();
		}
	}
	return false;
}

[[nodiscard]] VideoParams ParseVideoParams(const QByteArray &json) {
	if (json.isEmpty()) {
		return {};
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("API Error: "
			"Failed to parse group call video params, error: %1."
			).arg(error.errorString()));
		return {};
	} else if (!document.isObject()) {
		LOG(("API Error: "
			"Not an object received in group call video params."));
		return {};
	}

	const auto object = document.object();
	auto result = VideoParams{
		.endpoint = ReadJsonString(object, "endpoint"),
		.json = json,
	};
	const auto ssrcGroups = object.value("ssrc-groups").toArray();
	for (const auto &value : ssrcGroups) {
		const auto inner = value.toObject();
		const auto list = inner.value("sources").toArray();
		for (const auto &source : list) {
			const auto ssrc = uint32_t(source.toDouble());
			result.ssrcs.emplace(ssrc);
		}
	}
	return result.empty() ? VideoParams() : result;
}

std::shared_ptr<ParticipantVideoParams> ParseVideoParams(
		const QByteArray &camera,
		const QByteArray &screen,
		const std::shared_ptr<ParticipantVideoParams> &existing) {
	using namespace tgcalls;

	if (camera.isEmpty() && screen.isEmpty()) {
		return nullptr;
	}
	const auto cameraHash = camera.isEmpty()
		? 0
		: XXH32(camera.data(), camera.size(), uint32(0));
	const auto screenHash = screen.isEmpty()
		? 0
		: XXH32(screen.data(), screen.size(), uint32(0));
	if (existing
		&& existing->camera.hash == cameraHash
		&& existing->screen.hash == screenHash) {
		return existing;
	}
	// We don't reuse existing pointer, that way we can compare pointers
	// to see if anything was changed in video params.
	const auto data = /*existing
		? existing
		: */std::make_shared<ParticipantVideoParams>();
	data->camera = ParseVideoParams(camera);
	data->camera.hash = cameraHash;
	data->screen = ParseVideoParams(screen);
	data->screen.hash = screenHash;
	return data;
}

GroupCall::LoadPartTask::LoadPartTask(
	base::weak_ptr<GroupCall> call,
	int64 time,
	int64 period,
	Fn<void(tgcalls::BroadcastPart &&)> done)
: _call(std::move(call))
, _time(time ? time : (base::unixtime::now() * int64(1000)))
, _scale([&] {
	switch (period) {
	case 1000: return 0;
	case 500: return 1;
	case 250: return 2;
	case 125: return 3;
	}
	Unexpected("Period in LoadPartTask.");
}())
, _done(std::move(done)) {
}

void GroupCall::LoadPartTask::done(tgcalls::BroadcastPart &&part) {
	QMutexLocker lock(&_mutex);
	if (_done) {
		base::take(_done)(std::move(part));
	}
}

void GroupCall::LoadPartTask::cancel() {
	QMutexLocker lock(&_mutex);
	if (!_done) {
		return;
	}
	_done = nullptr;
	lock.unlock();

	if (_call) {
		const auto that = this;
		crl::on_main(_call, [weak = _call, that] {
			if (const auto strong = weak.get()) {
				strong->broadcastPartCancel(that);
			}
		});
	}
}

GroupCall::MediaChannelDescriptionsTask::MediaChannelDescriptionsTask(
	base::weak_ptr<GroupCall> call,
	const std::vector<std::uint32_t> &ssrcs,
	Fn<void(std::vector<tgcalls::MediaChannelDescription>&&)> done)
: _call(std::move(call))
, _ssrcs(ssrcs.begin(), ssrcs.end())
, _done(std::move(done)) {
}

auto GroupCall::MediaChannelDescriptionsTask::ssrcs() const
-> base::flat_set<uint32> {
	return _ssrcs;
}

bool GroupCall::MediaChannelDescriptionsTask::finishWithAdding(
		uint32 ssrc,
		std::optional<tgcalls::MediaChannelDescription> description,
		bool screen) {
	Expects(_ssrcs.contains(ssrc));

	using Type = tgcalls::MediaChannelDescription::Type;
	_ssrcs.remove(ssrc);
	if (!description) {
	} else if (description->type == Type::Audio
		|| (!screen && _cameraAdded.emplace(description->audioSsrc).second)
		|| (screen && _screenAdded.emplace(description->audioSsrc).second)) {
		_result.push_back(std::move(*description));
	}

	if (!_ssrcs.empty()) {
		return false;
	}
	QMutexLocker lock(&_mutex);
	if (_done) {
		base::take(_done)(std::move(_result));
	}
	return true;
}

void GroupCall::MediaChannelDescriptionsTask::cancel() {
	QMutexLocker lock(&_mutex);
	if (!_done) {
		return;
	}
	_done = nullptr;
	lock.unlock();

	if (_call) {
		const auto that = this;
		crl::on_main(_call, [weak = _call, that] {
			if (const auto strong = weak.get()) {
				strong->mediaChannelDescriptionsCancel(that);
			}
		});
	}
}

GroupCall::GroupCall(
	not_null<Delegate*> delegate,
	Group::JoinInfo info,
	const MTPInputGroupCall &inputCall)
: _delegate(delegate)
, _peer(info.peer)
, _history(_peer->owner().history(_peer))
, _api(&_peer->session().mtp())
, _joinAs(info.joinAs)
, _possibleJoinAs(std::move(info.possibleJoinAs))
, _joinHash(info.joinHash)
, _id(inputCall.c_inputGroupCall().vid().v)
, _scheduleDate(info.scheduleDate)
, _cameraOutgoing(std::make_unique<Webrtc::VideoTrack>(
	Webrtc::VideoState::Inactive))
, _screenOutgoing(std::make_unique<Webrtc::VideoTrack>(
	Webrtc::VideoState::Inactive))
, _lastSpokeCheckTimer([=] { checkLastSpoke(); })
, _checkJoinedTimer([=] { checkJoined(); })
, _pushToTalkCancelTimer([=] { pushToTalkCancel(); })
, _connectingSoundTimer([=] { playConnectingSoundOnce(); })
, _mediaDevices(CreateMediaDevices()) {
	_muted.value(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](MuteState previous, MuteState state) {
		if (_instance) {
			updateInstanceMuteState();
		}
		if (_mySsrc
			&& (!_initialMuteStateSent || state == MuteState::Active)) {
			_initialMuteStateSent = true;
			maybeSendMutedUpdate(previous);
		}
	}, _lifetime);

	_instanceState.value(
	) | rpl::filter([=] {
		return _hadJoinedState;
	}) | rpl::start_with_next([=](InstanceState state) {
		if (state == InstanceState::Disconnected) {
			playConnectingSound();
		} else {
			stopConnectingSound();
		}
	}, _lifetime);

	checkGlobalShortcutAvailability();

	if (const auto real = lookupReal()) {
		subscribeToReal(real);
		if (!_peer->canManageGroupCall() && real->joinMuted()) {
			_muted = MuteState::ForceMuted;
		}
	} else {
		_peer->session().changes().peerFlagsValue(
			_peer,
			Data::PeerUpdate::Flag::GroupCall
		) | rpl::map([=] {
			return lookupReal();
		}) | rpl::filter([](Data::GroupCall *real) {
			return real != nullptr;
		}) | rpl::map([](Data::GroupCall *real) {
			return not_null{ real };
		}) | rpl::take(
			1
		) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
			subscribeToReal(real);
			_realChanges.fire_copy(real);
		}, _lifetime);
	}

	setupMediaDevices();

	if (_id) {
		join(inputCall);
	} else {
		start(info.scheduleDate);
	}
	if (_scheduleDate) {
		saveDefaultJoinAs(_joinAs);
	}
}

GroupCall::~GroupCall() {
	destroyController();
	destroyScreencast();
}

bool GroupCall::isSharingScreen() const {
	return (_screenOutgoing->state() == Webrtc::VideoState::Active);
}

rpl::producer<bool> GroupCall::isSharingScreenValue() const {
	using namespace rpl::mappers;
	return _screenOutgoing->stateValue(
	) | rpl::map(
		_1 == Webrtc::VideoState::Active
	) | rpl::distinct_until_changed();
}

const std::string &GroupCall::screenSharingEndpoint() const {
	return isSharingScreen() ? _screenEndpoint : EmptyString();
}

bool GroupCall::isSharingCamera() const {
	return (_cameraOutgoing->state() == Webrtc::VideoState::Active);
}

rpl::producer<bool> GroupCall::isSharingCameraValue() const {
	using namespace rpl::mappers;
	return _cameraOutgoing->stateValue(
	) | rpl::map(
		_1 == Webrtc::VideoState::Active
	) | rpl::distinct_until_changed();
}

const std::string &GroupCall::cameraSharingEndpoint() const {
	return isSharingCamera() ? _cameraEndpoint : EmptyString();
}

QString GroupCall::screenSharingDeviceId() const {
	return isSharingScreen() ? _screenDeviceId : QString();
}

void GroupCall::toggleVideo(bool active) {
	if (!_instance || !_id) {
		return;
	}
	ensureOutgoingVideo();
	const auto state = active
		? Webrtc::VideoState::Active
		: Webrtc::VideoState::Inactive;
	if (_cameraOutgoing->state() != state) {
		_cameraOutgoing->setState(state);
	}
}

void GroupCall::toggleScreenSharing(std::optional<QString> uniqueId) {
	ensureOutgoingVideo();
	if (!uniqueId) {
		_screenOutgoing->setState(Webrtc::VideoState::Inactive);
		return;
	}
	const auto changed = (_screenDeviceId != *uniqueId);
	_screenDeviceId = *uniqueId;
	if (_screenOutgoing->state() != Webrtc::VideoState::Active) {
		_screenOutgoing->setState(Webrtc::VideoState::Active);
	}
	if (changed) {
		_screenCapture->switchToDevice(uniqueId->toStdString());
	}
}

void GroupCall::setScheduledDate(TimeId date) {
	const auto was = _scheduleDate;
	_scheduleDate = date;
	if (was && !date) {
		join(inputCall());
	}
}

void GroupCall::subscribeToReal(not_null<Data::GroupCall*> real) {
	real->scheduleDateValue(
	) | rpl::start_with_next([=](TimeId date) {
		setScheduledDate(date);
	}, _lifetime);

	real->participantsReloaded(
	) | rpl::start_with_next([=] {
		fillActiveVideoEndpoints();
	}, _lifetime);
	fillActiveVideoEndpoints();

	using Update = Data::GroupCall::ParticipantUpdate;
	real->participantUpdated(
	) | rpl::start_with_next([=](const Update &data) {
		auto newLarge = _videoEndpointLarge.current();
		auto updateCameraNotStreams = std::string();
		auto updateScreenNotStreams = std::string();
		const auto regularEndpoint = [&](const std::string &endpoint)
		-> const std::string & {
			return (endpoint.empty()
					|| endpoint == _cameraEndpoint
					|| endpoint == _screenEndpoint)
				? EmptyString()
				: endpoint;
		};
		const auto guard = gsl::finally([&] {
			if (newLarge.empty()) {
				newLarge = chooseLargeVideoEndpoint();
			}
			if (_videoEndpointLarge.current() != newLarge) {
				_videoEndpointLarge = newLarge;
			}
			if (!updateCameraNotStreams.empty()) {
				_streamsVideoUpdated.fire({ updateCameraNotStreams, false });
			}
			if (!updateScreenNotStreams.empty()) {
				_streamsVideoUpdated.fire({ updateScreenNotStreams, false });
			}
		});

		const auto &wasCameraEndpoint = (data.was && data.was->videoParams)
			? regularEndpoint(data.was->videoParams->camera.endpoint)
			: EmptyString();
		const auto &nowCameraEndpoint = (data.now && data.now->videoParams)
			? regularEndpoint(data.now->videoParams->camera.endpoint)
			: EmptyString();
		if (wasCameraEndpoint != nowCameraEndpoint) {
			if (!nowCameraEndpoint.empty()
				&& _activeVideoEndpoints.emplace(
					nowCameraEndpoint,
					EndpointType::Camera).second
				&& _incomingVideoEndpoints.contains(nowCameraEndpoint)) {
				_streamsVideoUpdated.fire({ nowCameraEndpoint, true });
			}
			if (!wasCameraEndpoint.empty()
				&& _activeVideoEndpoints.remove(wasCameraEndpoint)
				&& _incomingVideoEndpoints.contains(wasCameraEndpoint)) {
				updateCameraNotStreams = wasCameraEndpoint;
				if (newLarge == wasCameraEndpoint) {
					_videoEndpointPinned = newLarge = std::string();
				}
			}
		}
		const auto &wasScreenEndpoint = (data.was && data.was->videoParams)
			? data.was->videoParams->screen.endpoint
			: EmptyString();
		const auto &nowScreenEndpoint = (data.now && data.now->videoParams)
			? data.now->videoParams->screen.endpoint
			: EmptyString();
		if (wasScreenEndpoint != nowScreenEndpoint) {
			if (!nowScreenEndpoint.empty()
				&& _activeVideoEndpoints.emplace(
					nowScreenEndpoint,
					EndpointType::Screen).second
				&& _incomingVideoEndpoints.contains(nowScreenEndpoint)) {
				_streamsVideoUpdated.fire({ nowScreenEndpoint, true });
			}
			if (!wasScreenEndpoint.empty()
				&& _activeVideoEndpoints.remove(wasScreenEndpoint)
				&& _incomingVideoEndpoints.contains(wasScreenEndpoint)) {
				updateScreenNotStreams = wasScreenEndpoint;
				if (newLarge == wasScreenEndpoint) {
					_videoEndpointPinned = newLarge = std::string();
				}
			}
		}
		const auto nowSpeaking = data.now && data.now->speaking;
		const auto nowSounding = data.now && data.now->sounding;
		const auto wasSpeaking = data.was && data.was->speaking;
		const auto wasSounding = data.was && data.was->sounding;
		if (nowSpeaking == wasSpeaking && nowSounding == wasSounding) {
			return;
		} else if (!_videoEndpointPinned.current().empty()) {
			return;
		}
		if (nowScreenEndpoint != newLarge
			&& streamsVideo(nowScreenEndpoint)
			&& activeVideoEndpointType(newLarge) != EndpointType::Screen) {
			newLarge = nowScreenEndpoint;
		}
		const auto &participants = real->participants();
		if (!nowSpeaking
			&& (wasSpeaking || wasSounding)
			&& (wasCameraEndpoint == newLarge)) {
			auto screenEndpoint = std::string();
			auto speakingEndpoint = std::string();
			auto soundingEndpoint = std::string();
			for (const auto &participant : participants) {
				const auto params = participant.videoParams.get();
				if (!params) {
					continue;
				}
				if (streamsVideo(params->screen.endpoint)) {
					screenEndpoint = params->screen.endpoint;
					break;
				} else if (participant.speaking
					&& speakingEndpoint.empty()) {
					if (streamsVideo(params->camera.endpoint)) {
						speakingEndpoint = params->camera.endpoint;
					}
				} else if (!nowSounding
					&& participant.sounding
					&& soundingEndpoint.empty()) {
					if (streamsVideo(params->camera.endpoint)) {
						soundingEndpoint = params->camera.endpoint;
					}
				}
			}
			if (!screenEndpoint.empty()) {
				newLarge = screenEndpoint;
			} else if (!speakingEndpoint.empty()) {
				newLarge = speakingEndpoint;
			} else if (!soundingEndpoint.empty()) {
				newLarge = soundingEndpoint;
			}
		} else if ((nowSpeaking || nowSounding)
			&& (nowCameraEndpoint != newLarge)
			&& (activeVideoEndpointType(newLarge) != EndpointType::Screen)
			&& streamsVideo(nowCameraEndpoint)) {
			const auto participant = real->participantByEndpoint(newLarge);
			const auto screen = participant
				&& (participant->videoParams->screen.endpoint == newLarge);
			const auto speaking = participant && participant->speaking;
			const auto sounding = participant && participant->sounding;
			if (!screen
				&& ((nowSpeaking && !speaking)
					|| (nowSounding && !sounding))) {
				newLarge = nowCameraEndpoint;
			}
		}
	}, _lifetime);

	real->participantsResolved(
	) | rpl::start_with_next([=](
		not_null<const base::flat_map<
			uint32,
			Data::LastSpokeTimes>*> ssrcs) {
		checkMediaChannelDescriptions([&](uint32 ssrc) {
			return ssrcs->contains(ssrc);
		});
	}, _lifetime);
}

void GroupCall::checkGlobalShortcutAvailability() {
	auto &settings = Core::App().settings();
	if (!settings.groupCallPushToTalk()) {
		return;
	} else if (!base::GlobalShortcutsAllowed()) {
		settings.setGroupCallPushToTalk(false);
		Core::App().saveSettingsDelayed();
	}
}

void GroupCall::setState(State state) {
	if (_state.current() == State::Failed) {
		return;
	} else if (_state.current() == State::FailedHangingUp
		&& state != State::Failed) {
		return;
	}
	if (_state.current() == state) {
		return;
	}
	_state = state;

	if (state == State::Joined) {
		stopConnectingSound();
		if (const auto call = _peer->groupCall(); call && call->id() == _id) {
			call->setInCall();
		}
	}

	if (false
		|| state == State::Ended
		|| state == State::Failed) {
		// Destroy controller before destroying Call Panel,
		// so that the panel hide animation is smooth.
		destroyController();
		destroyScreencast();
	}
	switch (state) {
	case State::HangingUp:
	case State::FailedHangingUp:
		_delegate->groupCallPlaySound(Delegate::GroupCallSound::Ended);
		break;
	case State::Ended:
		_delegate->groupCallFinished(this);
		break;
	case State::Failed:
		_delegate->groupCallFailed(this);
		break;
	case State::Connecting:
		if (!_checkJoinedTimer.isActive()) {
			_checkJoinedTimer.callOnce(kCheckJoinedTimeout);
		}
		break;
	}
}

void GroupCall::playConnectingSound() {
	if (_connectingSoundTimer.isActive()) {
		return;
	}
	playConnectingSoundOnce();
	_connectingSoundTimer.callEach(kPlayConnectingEach);
}

void GroupCall::stopConnectingSound() {
	_connectingSoundTimer.cancel();
}

void GroupCall::playConnectingSoundOnce() {
	_delegate->groupCallPlaySound(Delegate::GroupCallSound::Connecting);
}

bool GroupCall::showChooseJoinAs() const {
	return (_possibleJoinAs.size() > 1)
		|| (_possibleJoinAs.size() == 1
			&& !_possibleJoinAs.front()->isSelf());
}

bool GroupCall::scheduleStartSubscribed() const {
	if (const auto real = lookupReal()) {
		return real->scheduleStartSubscribed();
	}
	return false;
}

Data::GroupCall *GroupCall::lookupReal() const {
	const auto real = _peer->groupCall();
	return (real && real->id() == _id) ? real : nullptr;
}

rpl::producer<not_null<Data::GroupCall*>> GroupCall::real() const {
	if (const auto real = lookupReal()) {
		return rpl::single(not_null{ real });
	}
	return _realChanges.events();
}

void GroupCall::start(TimeId scheduleDate) {
	using Flag = MTPphone_CreateGroupCall::Flag;
	_createRequestId = _api.request(MTPphone_CreateGroupCall(
		MTP_flags(scheduleDate ? Flag::f_schedule_date : Flag(0)),
		_peer->input,
		MTP_int(openssl::RandomValue<int32>()),
		MTPstring(), // title
		MTP_int(scheduleDate)
	)).done([=](const MTPUpdates &result) {
		_acceptFields = true;
		_peer->session().api().applyUpdates(result);
		_acceptFields = false;
	}).fail([=](const MTP::Error &error) {
		LOG(("Call Error: Could not create, error: %1"
			).arg(error.type()));
		hangup();
		if (error.type() == u"GROUPCALL_ANONYMOUS_FORBIDDEN"_q) {
			Ui::ShowMultilineToast({
				.text = { tr::lng_group_call_no_anonymous(tr::now) },
			});
		}
	}).send();
}

void GroupCall::join(const MTPInputGroupCall &inputCall) {
	inputCall.match([&](const MTPDinputGroupCall &data) {
		_id = data.vid().v;
		_accessHash = data.vaccess_hash().v;
	});
	setState(_scheduleDate ? State::Waiting : State::Joining);

	if (_scheduleDate) {
		return;
	}
	rejoin();

	using Update = Data::GroupCall::ParticipantUpdate;
	const auto real = lookupReal();
	Assert(real != nullptr);
	real->participantUpdated(
	) | rpl::filter([=](const Update &update) {
		return (_instance != nullptr);
	}) | rpl::start_with_next([=](const Update &update) {
		if (!update.now) {
			_instance->removeSsrcs({ update.was->ssrc });
		} else {
			const auto &now = *update.now;
			const auto &was = update.was;
			const auto volumeChanged = was
				? (was->volume != now.volume
					|| was->mutedByMe != now.mutedByMe)
				: (now.volume != Group::kDefaultVolume || now.mutedByMe);
			if (volumeChanged) {
				_instance->setVolume(
					now.ssrc,
					(now.mutedByMe
						? 0.
						: (now.volume
							/ float64(Group::kDefaultVolume))));
			}
		}
	}, _lifetime);

	_peer->session().updates().addActiveChat(
		_peerStream.events_starting_with_copy(_peer));
	SubscribeToMigration(_peer, _lifetime, [=](not_null<ChannelData*> group) {
		_peer = group;
		_peerStream.fire_copy(group);
	});
}

void GroupCall::setMyEndpointType(
		const std::string &endpoint,
		EndpointType type) {
	if (endpoint.empty()) {
		return;
	} else if (type == EndpointType::None) {
		const auto was1 = _incomingVideoEndpoints.remove(endpoint);
		const auto was2 = _activeVideoEndpoints.remove(endpoint);
		if (was1 && was2) {
			auto newLarge = _videoEndpointLarge.current();
			if (newLarge == endpoint) {
				_videoEndpointPinned = std::string();
				_videoEndpointLarge = chooseLargeVideoEndpoint();
			}
			_streamsVideoUpdated.fire({ endpoint, false });
		}
	} else {
		const auto now1 = _incomingVideoEndpoints.emplace(endpoint).second;
		const auto now2 = _activeVideoEndpoints.emplace(
			endpoint,
			type).second;
		if (now1 && now2) {
			_streamsVideoUpdated.fire({ endpoint, true });
		}
		const auto nowLarge = activeVideoEndpointType(
			_videoEndpointLarge.current());
		if (_videoEndpointPinned.current().empty()
			&& ((type == EndpointType::Screen
				&& nowLarge != EndpointType::Screen)
				|| (type == EndpointType::Camera
					&& nowLarge == EndpointType::None))) {
			_videoEndpointLarge = endpoint;
		}
	}
}

void GroupCall::setScreenEndpoint(std::string endpoint) {
	if (_screenEndpoint == endpoint) {
		return;
	}
	if (!_screenEndpoint.empty()) {
		setMyEndpointType(_screenEndpoint, EndpointType::None);
	}
	_screenEndpoint = std::move(endpoint);
	if (_screenEndpoint.empty()) {
		return;
	}
	if (_instance) {
		_instance->setIgnoreVideoEndpointIds({ _screenEndpoint });
	}
	if (isSharingScreen()) {
		setMyEndpointType(_screenEndpoint, EndpointType::Screen);
	}
}

void GroupCall::setCameraEndpoint(std::string endpoint) {
	if (_cameraEndpoint == endpoint) {
		return;
	}
	if (!_cameraEndpoint.empty()) {
		setMyEndpointType(_cameraEndpoint, EndpointType::None);
	}
	_cameraEndpoint = std::move(endpoint);
	if (_cameraEndpoint.empty()) {
		return;
	}
	if (isSharingCamera()) {
		setMyEndpointType(_cameraEndpoint, EndpointType::Camera);
	}
}

void GroupCall::addVideoOutput(
		const std::string &endpoint,
		SinkPointer sink) {
	if (_cameraEndpoint == endpoint) {
		_cameraCapture->setOutput(sink.data);
	} else if (_screenEndpoint == endpoint) {
		_screenCapture->setOutput(sink.data);
	} else {
		Assert(_instance != nullptr);
		_instance->addIncomingVideoOutput(endpoint, sink.data);
	}
}

void GroupCall::rejoin() {
	rejoin(_joinAs);
}

void GroupCall::rejoinWithHash(const QString &hash) {
	if (!hash.isEmpty()
		&& (muted() == MuteState::ForceMuted
			|| muted() == MuteState::RaisedHand)) {
		_joinHash = hash;
		rejoin();
	}
}

void GroupCall::setJoinAs(not_null<PeerData*> as) {
	_joinAs = as;
	if (const auto chat = _peer->asChat()) {
		chat->setGroupCallDefaultJoinAs(_joinAs->id);
	} else if (const auto channel = _peer->asChannel()) {
		channel->setGroupCallDefaultJoinAs(_joinAs->id);
	}
}

void GroupCall::saveDefaultJoinAs(not_null<PeerData*> as) {
	setJoinAs(as);
	_api.request(MTPphone_SaveDefaultGroupCallJoinAs(
		_peer->input,
		_joinAs->input
	)).send();
}

void GroupCall::rejoin(not_null<PeerData*> as) {
	if (state() != State::Joining
		&& state() != State::Joined
		&& state() != State::Connecting) {
		return;
	}

	_mySsrc = 0;
	_initialMuteStateSent = false;
	setState(State::Joining);
	ensureControllerCreated();
	setInstanceMode(InstanceMode::None);
	applyMeInCallLocally();
	LOG(("Call Info: Requesting join payload."));

	setJoinAs(as);

	const auto weak = base::make_weak(this);
	_instance->emitJoinPayload([=](tgcalls::GroupJoinPayload payload) {
		crl::on_main(weak, [=, payload = std::move(payload)]{
			const auto ssrc = payload.audioSsrc;
			LOG(("Call Info: Join payload received, joining with ssrc: %1."
				).arg(ssrc));

			const auto json = QByteArray::fromStdString(payload.json);
			const auto wasMuteState = muted();
			using Flag = MTPphone_JoinGroupCall::Flag;
			_api.request(MTPphone_JoinGroupCall(
				MTP_flags((wasMuteState != MuteState::Active
					? Flag::f_muted
					: Flag(0)) | (_joinHash.isEmpty()
						? Flag(0)
						: Flag::f_invite_hash)),
				inputCall(),
				_joinAs->input,
				MTP_string(_joinHash),
				MTP_dataJSON(MTP_bytes(json))
			)).done([=](const MTPUpdates &updates) {
				_mySsrc = ssrc;
				_mySsrcs.emplace(ssrc);
				setState((_instanceState.current()
					== InstanceState::Disconnected)
					? State::Connecting
					: State::Joined);
				applyMeInCallLocally();
				maybeSendMutedUpdate(wasMuteState);
				_peer->session().api().applyUpdates(updates);
				applyQueuedSelfUpdates();
				checkFirstTimeJoined();
				sendSelfUpdate(SendUpdateType::VideoMuted);
				if (_screenSsrc && isSharingScreen()) {
					LOG(("Call Info: Screen rejoin after rejoin()."));
					rejoinPresentation();
				}
			}).fail([=](const MTP::Error &error) {
				const auto type = error.type();
				LOG(("Call Error: Could not join, error: %1").arg(type));

				if (type == u"GROUPCALL_SSRC_DUPLICATE_MUCH") {
					rejoin();
					return;
				}

				hangup();
				Ui::ShowMultilineToast({
					.text = { type == u"GROUPCALL_ANONYMOUS_FORBIDDEN"_q
						? tr::lng_group_call_no_anonymous(tr::now)
						: type == u"GROUPCALL_PARTICIPANTS_TOO_MUCH"_q
						? tr::lng_group_call_too_many(tr::now)
						: type == u"GROUPCALL_FORBIDDEN"_q
						? tr::lng_group_not_accessible(tr::now)
						: Lang::Hard::ServerError() },
				});
			}).send();
		});
	});
}

void GroupCall::joinLeavePresentation() {
	if (_screenOutgoing->state() == Webrtc::VideoState::Active) {
		rejoinPresentation();
	} else {
		leavePresentation();
	}
}

void GroupCall::rejoinPresentation() {
	_screenSsrc = 0;
	ensureScreencastCreated();
	setScreenInstanceMode(InstanceMode::None);
	LOG(("Call Info: Requesting join payload."));

	const auto weak = base::make_weak(this);
	_screenInstance->emitJoinPayload([=](tgcalls::GroupJoinPayload payload) {
		crl::on_main(weak, [=, payload = std::move(payload)]{
			const auto ssrc = payload.audioSsrc;
			LOG(("Call Info: Join payload received, joining with ssrc: %1."
				).arg(ssrc));

			const auto json = QByteArray::fromStdString(payload.json);
			_api.request(MTPphone_JoinGroupCallPresentation(
				inputCall(),
				MTP_dataJSON(MTP_bytes(json))
			)).done([=](const MTPUpdates &updates) {
				_screenSsrc = ssrc;
				_mySsrcs.emplace(ssrc);
				_peer->session().api().applyUpdates(updates);
			}).fail([=](const MTP::Error &error) {
				const auto type = error.type();
				LOG(("Call Error: "
					"Could not screen join, error: %1").arg(type));
				if (type == u"GROUPCALL_SSRC_DUPLICATE_MUCH") {
					rejoinPresentation();
				} else if (type == u"GROUPCALL_JOIN_MISSING"_q
					|| type == u"GROUPCALL_FORBIDDEN"_q) {
					_screenSsrc = ssrc;
					rejoin();
				} else {
					_screenSsrc = 0;
					setScreenEndpoint(std::string());
				}
			}).send();
		});
	});
}

void GroupCall::leavePresentation() {
	if (!_screenSsrc) {
		return;
	}
	_api.request(MTPphone_LeaveGroupCallPresentation(
		inputCall()
	)).done([=](const MTPUpdates &updates) {
		_screenSsrc = 0;
		setScreenEndpoint(std::string());
		_peer->session().api().applyUpdates(updates);
	}).fail([=](const MTP::Error &error) {
		const auto type = error.type();
		LOG(("Call Error: "
			"Could not screen leave, error: %1").arg(type));
		_screenSsrc = 0;
		setScreenEndpoint(std::string());
	}).send();
}

void GroupCall::applyMeInCallLocally() {
	const auto call = _peer->groupCall();
	if (!call || call->id() != _id) {
		return;
	}
	using Flag = MTPDgroupCallParticipant::Flag;
	const auto &participants = call->participants();
	const auto i = ranges::find(
		participants,
		_joinAs,
		&Data::GroupCall::Participant::peer);
	const auto date = (i != end(participants))
		? i->date
		: base::unixtime::now();
	const auto lastActive = (i != end(participants))
		? i->lastActive
		: TimeId(0);
	const auto volume = (i != end(participants))
		? i->volume
		: Group::kDefaultVolume;
	const auto canSelfUnmute = (muted() != MuteState::ForceMuted)
		&& (muted() != MuteState::RaisedHand);
	const auto raisedHandRating = (muted() != MuteState::RaisedHand)
		? uint64(0)
		: (i != end(participants))
		? i->raisedHandRating
		: FindLocalRaisedHandRating(participants);
	const auto params = (i != end(participants))
		? i->videoParams.get()
		: nullptr;
	const auto flags = (canSelfUnmute ? Flag::f_can_self_unmute : Flag(0))
		| (lastActive ? Flag::f_active_date : Flag(0))
		| (_mySsrc ? Flag(0) : Flag::f_left)
		| Flag::f_self
		| Flag::f_volume // Without flag the volume is reset to 100%.
		| Flag::f_volume_by_admin // Self volume can only be set by admin.
		| ((muted() != MuteState::Active) ? Flag::f_muted : Flag(0))
		| ((params && !params->camera.empty()) ? Flag::f_video : Flag(0))
		| ((params && !params->screen.empty())
			? Flag::f_presentation
			: Flag(0))
		| (raisedHandRating > 0 ? Flag::f_raise_hand_rating : Flag(0));
	call->applyLocalUpdate(
		MTP_updateGroupCallParticipants(
			inputCall(),
			MTP_vector<MTPGroupCallParticipant>(
				1,
				MTP_groupCallParticipant(
					MTP_flags(flags),
					peerToMTP(_joinAs->id),
					MTP_int(date),
					MTP_int(lastActive),
					MTP_int(_mySsrc),
					MTP_int(volume),
					MTPstring(), // Don't update about text in local updates.
					MTP_long(raisedHandRating),
					(params
						? MTP_dataJSON(MTP_bytes(params->camera.json))
						: MTPDataJSON()),
					(params
						? MTP_dataJSON(MTP_bytes(params->screen.json))
						: MTPDataJSON()))),
			MTP_int(0)).c_updateGroupCallParticipants());
}

void GroupCall::applyParticipantLocally(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume) {
	const auto participant = LookupParticipant(_peer, _id, participantPeer);
	if (!participant || !participant->ssrc) {
		return;
	}
	const auto canManageCall = _peer->canManageGroupCall();
	const auto isMuted = participant->muted || (mute && canManageCall);
	const auto canSelfUnmute = !canManageCall
		? participant->canSelfUnmute
		: (!mute || IsGroupCallAdmin(_peer, participantPeer));
	const auto isMutedByYou = mute && !canManageCall;
	const auto params = participant->videoParams.get();
	const auto mutedCount = 0/*participant->mutedCount*/;
	using Flag = MTPDgroupCallParticipant::Flag;
	const auto flags = (canSelfUnmute ? Flag::f_can_self_unmute : Flag(0))
		| Flag::f_volume // Without flag the volume is reset to 100%.
		| ((participant->applyVolumeFromMin && !volume)
			? Flag::f_volume_by_admin
			: Flag(0))
		| (participant->lastActive ? Flag::f_active_date : Flag(0))
		| (isMuted ? Flag::f_muted : Flag(0))
		| (isMutedByYou ? Flag::f_muted_by_you : Flag(0))
		| (participantPeer == _joinAs ? Flag::f_self : Flag(0))
		| ((params && !params->camera.empty()) ? Flag::f_video : Flag(0))
		| ((params && !params->screen.empty())
			? Flag::f_presentation
			: Flag(0))
		| (participant->raisedHandRating
			? Flag::f_raise_hand_rating
			: Flag(0));
	_peer->groupCall()->applyLocalUpdate(
		MTP_updateGroupCallParticipants(
			inputCall(),
			MTP_vector<MTPGroupCallParticipant>(
				1,
				MTP_groupCallParticipant(
					MTP_flags(flags),
					peerToMTP(participantPeer->id),
					MTP_int(participant->date),
					MTP_int(participant->lastActive),
					MTP_int(participant->ssrc),
					MTP_int(volume.value_or(participant->volume)),
					MTPstring(), // Don't update about text in local updates.
					MTP_long(participant->raisedHandRating),
					(params
						? MTP_dataJSON(MTP_bytes(params->camera.json))
						: MTPDataJSON()),
					(params
						? MTP_dataJSON(MTP_bytes(params->screen.json))
						: MTPDataJSON()))),
			MTP_int(0)).c_updateGroupCallParticipants());
}

void GroupCall::hangup() {
	finish(FinishType::Ended);
}

void GroupCall::discard() {
	if (!_id) {
		_api.request(_createRequestId).cancel();
		hangup();
		return;
	}
	_api.request(MTPphone_DiscardGroupCall(
		inputCall()
	)).done([=](const MTPUpdates &result) {
		// Here 'this' could be destroyed by updates, so we set Ended after
		// updates being handled, but in a guarded way.
		crl::on_main(this, [=] { hangup(); });
		_peer->session().api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		hangup();
	}).send();
}

void GroupCall::rejoinAs(Group::JoinInfo info) {
	_possibleJoinAs = std::move(info.possibleJoinAs);
	if (info.joinAs == _joinAs) {
		return;
	}
	const auto event = Group::RejoinEvent{
		.wasJoinAs = _joinAs,
		.nowJoinAs = info.joinAs,
	};
	if (_scheduleDate) {
		saveDefaultJoinAs(info.joinAs);
	} else {
		setState(State::Joining);
		rejoin(info.joinAs);
	}
	_rejoinEvents.fire_copy(event);
}

void GroupCall::finish(FinishType type) {
	Expects(type != FinishType::None);

	const auto finalState = (type == FinishType::Ended)
		? State::Ended
		: State::Failed;
	const auto hangupState = (type == FinishType::Ended)
		? State::HangingUp
		: State::FailedHangingUp;
	const auto state = _state.current();
	if (state == State::HangingUp
		|| state == State::FailedHangingUp
		|| state == State::Ended
		|| state == State::Failed) {
		return;
	}
	if (!_mySsrc) {
		setState(finalState);
		return;
	}

	setState(hangupState);

	// We want to leave request still being sent and processed even if
	// the call is already destroyed.
	const auto session = &_peer->session();
	const auto weak = base::make_weak(this);
	session->api().request(MTPphone_LeaveGroupCall(
		inputCall(),
		MTP_int(_mySsrc)
	)).done([=](const MTPUpdates &result) {
		// Here 'this' could be destroyed by updates, so we set Ended after
		// updates being handled, but in a guarded way.
		crl::on_main(weak, [=] { setState(finalState); });
		session->api().applyUpdates(result);
	}).fail(crl::guard(weak, [=](const MTP::Error &error) {
		setState(finalState);
	})).send();
}

void GroupCall::startScheduledNow() {
	if (!lookupReal()) {
		return;
	}
	_api.request(MTPphone_StartScheduledGroupCall(
		inputCall()
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
	}).send();
}

void GroupCall::toggleScheduleStartSubscribed(bool subscribed) {
	if (!lookupReal()) {
		return;
	}
	_api.request(MTPphone_ToggleGroupCallStartSubscription(
		inputCall(),
		MTP_bool(subscribed)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
	}).send();
}

void GroupCall::addVideoOutput(
		const std::string &endpoint,
		not_null<Webrtc::VideoTrack*> track) {
	addVideoOutput(endpoint, { track->sink() });
}

void GroupCall::setMuted(MuteState mute) {
	const auto set = [=] {
		const auto wasMuted = (muted() == MuteState::Muted)
			|| (muted() == MuteState::PushToTalk);
		const auto wasRaiseHand = (muted() == MuteState::RaisedHand);
		_muted = mute;
		const auto nowMuted = (muted() == MuteState::Muted)
			|| (muted() == MuteState::PushToTalk);
		const auto nowRaiseHand = (muted() == MuteState::RaisedHand);
		if (wasMuted != nowMuted || wasRaiseHand != nowRaiseHand) {
			applyMeInCallLocally();
		}
	};
	if (mute == MuteState::Active || mute == MuteState::PushToTalk) {
		_delegate->groupCallRequestPermissionsOrFail(crl::guard(this, set));
	} else {
		set();
	}
}

void GroupCall::setMutedAndUpdate(MuteState mute) {
	const auto was = muted();

	// Active state is sent from _muted changes,
	// because it may be set delayed, after permissions request, not now.
	const auto send = _initialMuteStateSent && (mute != MuteState::Active);
	setMuted(mute);
	if (send) {
		maybeSendMutedUpdate(was);
	}
}

void GroupCall::handlePossibleCreateOrJoinResponse(
		const MTPDupdateGroupCall &data) {
	data.vcall().match([&](const MTPDgroupCall &data) {
		handlePossibleCreateOrJoinResponse(data);
	}, [&](const MTPDgroupCallDiscarded &data) {
		handlePossibleDiscarded(data);
	});
}

void GroupCall::handlePossibleCreateOrJoinResponse(
		const MTPDgroupCall &data) {
	setScheduledDate(data.vschedule_date().value_or_empty());
	if (_acceptFields) {
		if (!_instance && !_id) {
			const auto input = MTP_inputGroupCall(
				data.vid(),
				data.vaccess_hash());
			const auto scheduleDate = data.vschedule_date().value_or_empty();
			if (const auto chat = _peer->asChat()) {
				chat->setGroupCall(input, scheduleDate);
			} else if (const auto group = _peer->asChannel()) {
				group->setGroupCall(input, scheduleDate);
			} else {
				Unexpected("Peer type in GroupCall::join.");
			}
			join(input);
		}
		return;
	} else if (_id != data.vid().v || !_instance) {
		return;
	}
	if (const auto streamDcId = data.vstream_dc_id()) {
		_broadcastDcId = MTP::BareDcId(streamDcId->v);
	}
}

void GroupCall::handlePossibleCreateOrJoinResponse(
		const MTPDupdateGroupCallConnection &data) {
	if (data.is_presentation()) {
		setScreenInstanceMode(InstanceMode::Rtc);
		data.vparams().match([&](const MTPDdataJSON &data) {
			const auto json = data.vdata().v;
			setScreenEndpoint(ParseVideoEndpoint(json));
			_screenInstance->setJoinResponsePayload(json.toStdString());
		});
	} else {
		setInstanceMode(InstanceMode::Rtc);
		data.vparams().match([&](const MTPDdataJSON &data) {
			const auto json = data.vdata().v;
			setCameraEndpoint(ParseVideoEndpoint(json));
			_instance->setJoinResponsePayload(json.toStdString());
		});
		checkMediaChannelDescriptions();
	}
}

void GroupCall::handlePossibleDiscarded(const MTPDgroupCallDiscarded &data) {
	if (data.vid().v == _id) {
		LOG(("Call Info: Hangup after groupCallDiscarded."));
		_mySsrc = 0;
		hangup();
	}
}

void GroupCall::checkMediaChannelDescriptions(
		Fn<bool(uint32)> resolved) {
	const auto real = lookupReal();
	if (!real || (_instanceMode == InstanceMode::None)) {
		return;
	}
	for (auto i = begin(_mediaChannelDescriptionses)
		; i != end(_mediaChannelDescriptionses);) {
		if (mediaChannelDescriptionsFill(i->get(), resolved)) {
			i = _mediaChannelDescriptionses.erase(i);
		} else {
			++i;
		}
	}
	if (!_unresolvedSsrcs.empty()) {
		real->resolveParticipants(base::take(_unresolvedSsrcs));
	}
}

void GroupCall::handleUpdate(const MTPUpdate &update) {
	update.match([&](const MTPDupdateGroupCall &data) {
		handleUpdate(data);
	}, [&](const MTPDupdateGroupCallParticipants &data) {
		handleUpdate(data);
	}, [](const auto &) {
		Unexpected("Type in Instance::applyGroupCallUpdateChecked.");
	});
}

void GroupCall::handleUpdate(const MTPDupdateGroupCall &data) {
	data.vcall().match([](const MTPDgroupCall &) {
	}, [&](const MTPDgroupCallDiscarded &data) {
		handlePossibleDiscarded(data);
	});
}

void GroupCall::handleUpdate(const MTPDupdateGroupCallParticipants &data) {
	const auto callId = data.vcall().match([](const auto &data) {
		return data.vid().v;
	});
	if (_id != callId) {
		return;
	}
	const auto state = _state.current();
	const auto joined = (state == State::Joined)
		|| (state == State::Connecting);
	for (const auto &participant : data.vparticipants().v) {
		participant.match([&](const MTPDgroupCallParticipant &data) {
			const auto isSelf = data.is_self()
				|| (data.is_min()
					&& peerFromMTP(data.vpeer()) == _joinAs->id);
			if (!isSelf) {
				applyOtherParticipantUpdate(data);
			} else if (joined) {
				applySelfUpdate(data);
			} else {
				_queuedSelfUpdates.push_back(participant);
			}
		});
	}
}

void GroupCall::applyQueuedSelfUpdates() {
	const auto weak = base::make_weak(this);
	while (weak
		&& !_queuedSelfUpdates.empty()
		&& (_state.current() == State::Joined
			|| _state.current() == State::Connecting)) {
		const auto update = _queuedSelfUpdates.front();
		_queuedSelfUpdates.erase(_queuedSelfUpdates.begin());
		update.match([&](const MTPDgroupCallParticipant &data) {
			applySelfUpdate(data);
		});
	}
}

void GroupCall::applySelfUpdate(const MTPDgroupCallParticipant &data) {
	if (data.is_left()) {
		if (data.vsource().v == _mySsrc) {
			// I was removed from the call, rejoin.
			LOG(("Call Info: "
				"Rejoin after got 'left' with my ssrc."));
			setState(State::Joining);
			rejoin();
		}
		return;
	} else if (data.vsource().v != _mySsrc) {
		if (!_mySsrcs.contains(data.vsource().v)) {
			// I joined from another device, hangup.
			LOG(("Call Info: "
				"Hangup after '!left' with ssrc %1, my %2."
				).arg(data.vsource().v
				).arg(_mySsrc));
			_mySsrc = 0;
			hangup();
		} else {
			LOG(("Call Info: "
				"Some old 'self' with '!left' and ssrc %1, my %2."
				).arg(data.vsource().v
				).arg(_mySsrc));
		}
		return;
	}
	if (data.is_muted() && !data.is_can_self_unmute()) {
		setMuted(data.vraise_hand_rating().value_or_empty()
			? MuteState::RaisedHand
			: MuteState::ForceMuted);
	} else if (_instanceMode == InstanceMode::Stream) {
		LOG(("Call Info: Rejoin after unforcemute in stream mode."));
		setState(State::Joining);
		rejoin();
	} else if (muted() == MuteState::ForceMuted
		|| muted() == MuteState::RaisedHand) {
		setMuted(MuteState::Muted);
		if (!_instanceTransitioning) {
			notifyAboutAllowedToSpeak();
		}
	} else if (data.is_muted() && muted() != MuteState::Muted) {
		setMuted(MuteState::Muted);
	}
}

void GroupCall::applyOtherParticipantUpdate(
		const MTPDgroupCallParticipant &data) {
	if (data.is_min()) {
		// No real information about mutedByMe or my custom volume.
		return;
	}
	const auto participantPeer = _peer->owner().peer(
		peerFromMTP(data.vpeer()));
	if (!LookupParticipant(_peer, _id, participantPeer)) {
		return;
	}
	_otherParticipantStateValue.fire(Group::ParticipantState{
		.peer = participantPeer,
		.volume = data.vvolume().value_or_empty(),
		.mutedByMe = data.is_muted_by_you(),
	});
}

void GroupCall::setupMediaDevices() {
	_mediaDevices->audioInputId(
	) | rpl::start_with_next([=](QString id) {
		_audioInputId = id;
		if (_instance) {
			_instance->setAudioInputDevice(id.toStdString());
		}
	}, _lifetime);

	_mediaDevices->audioOutputId(
	) | rpl::start_with_next([=](QString id) {
		_audioOutputId = id;
		if (_instance) {
			_instance->setAudioOutputDevice(id.toStdString());
		}
	}, _lifetime);

	_mediaDevices->videoInputId(
	) | rpl::start_with_next([=](QString id) {
		_cameraInputId = id;
		if (_cameraCapture) {
			_cameraCapture->switchToDevice(id.toStdString());
		}
	}, _lifetime);
}

void GroupCall::ensureOutgoingVideo() {
	Expects(_id != 0);

	if (_videoInited) {
		return;
	}
	_videoInited = true;

	//static const auto hasDevices = [] {
	//	return !Webrtc::GetVideoInputList().empty();
	//};
	_cameraOutgoing->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		//if (state != Webrtc::VideoState::Inactive && !hasDevices()) {
			//_errors.fire({ ErrorType::NoCamera }); // #TODO calls
			//_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		//} else if (state != Webrtc::VideoState::Inactive
		//	&& _instance
		//	&& !_instance->supportsVideo()) {
		//	_errors.fire({ ErrorType::NotVideoCall });
		//	_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		/*} else */if (state != Webrtc::VideoState::Inactive) {
			// Paused not supported right now.
			Assert(state == Webrtc::VideoState::Active);
			if (!_cameraCapture) {
				_cameraCapture = _delegate->groupCallGetVideoCapture(
					_cameraInputId);
			} else {
				_cameraCapture->switchToDevice(_cameraInputId.toStdString());
			}
			if (_instance) {
				_instance->setVideoCapture(_cameraCapture);
			}
			_cameraCapture->setState(tgcalls::VideoState::Active);
			setMyEndpointType(_cameraEndpoint, EndpointType::Camera);
		} else {
			if (_cameraCapture) {
				_cameraCapture->setState(tgcalls::VideoState::Inactive);
			}
			setMyEndpointType(_cameraEndpoint, EndpointType::None);
		}
		sendSelfUpdate(SendUpdateType::VideoMuted);
		applyMeInCallLocally();
	}, _lifetime);

	_screenOutgoing->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		if (state != Webrtc::VideoState::Inactive) {
			// Paused not supported right now.
			Assert(state == Webrtc::VideoState::Active);
			if (!_screenCapture) {
				_screenCapture = std::shared_ptr<tgcalls::VideoCaptureInterface>(
					tgcalls::VideoCaptureInterface::Create(
						tgcalls::StaticThreads::getThreads(),
						_screenDeviceId.toStdString()));
			} else {
				_screenCapture->switchToDevice(_screenDeviceId.toStdString());
			}
			if (_screenInstance) {
				_screenInstance->setVideoCapture(_screenCapture);
			}
			_screenCapture->setState(tgcalls::VideoState::Active);
			setMyEndpointType(_screenEndpoint, EndpointType::Screen);
		} else {
			if (_screenCapture) {
				_screenCapture->setState(tgcalls::VideoState::Inactive);
			}
			setMyEndpointType(_screenEndpoint, EndpointType::None);
		}
		joinLeavePresentation();
	}, _lifetime);
}

void GroupCall::changeTitle(const QString &title) {
	const auto real = lookupReal();
	if (!real || real->title() == title) {
		return;
	}

	_api.request(MTPphone_EditGroupCallTitle(
		inputCall(),
		MTP_string(title)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		_titleChanged.fire({});
	}).fail([=](const MTP::Error &error) {
	}).send();
}

void GroupCall::toggleRecording(bool enabled, const QString &title) {
	const auto real = lookupReal();
	if (!real) {
		return;
	}

	const auto already = (real->recordStartDate() != 0);
	if (already == enabled) {
		return;
	}

	if (!enabled) {
		_recordingStoppedByMe = true;
	}
	using Flag = MTPphone_ToggleGroupCallRecord::Flag;
	_api.request(MTPphone_ToggleGroupCallRecord(
		MTP_flags((enabled ? Flag::f_start : Flag(0))
			| (title.isEmpty() ? Flag(0) : Flag::f_title)),
		inputCall(),
		MTP_string(title)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		_recordingStoppedByMe = false;
	}).fail([=](const MTP::Error &error) {
		_recordingStoppedByMe = false;
	}).send();
}

void GroupCall::ensureControllerCreated() {
	if (_instance) {
		return;
	}
	const auto &settings = Core::App().settings();

	const auto weak = base::make_weak(this);
	const auto myLevel = std::make_shared<tgcalls::GroupLevelValue>();
	_videoCall = true;
	tgcalls::GroupInstanceDescriptor descriptor = {
		.threads = tgcalls::StaticThreads::getThreads(),
		.config = tgcalls::GroupConfig{
		},
		.networkStateUpdated = [=](tgcalls::GroupNetworkState networkState) {
			crl::on_main(weak, [=] { setInstanceConnected(networkState); });
		},
		.audioLevelsUpdated = [=](const tgcalls::GroupLevelsUpdate &data) {
			const auto &updates = data.updates;
			if (updates.empty()) {
				return;
			} else if (updates.size() == 1 && !updates.front().ssrc) {
				const auto &value = updates.front().value;
				// Don't send many 0 while we're muted.
				if (myLevel->level == value.level
					&& myLevel->voice == value.voice) {
					return;
				}
				*myLevel = updates.front().value;
			}
			crl::on_main(weak, [=] { audioLevelsUpdated(data); });
		},
		.initialInputDeviceId = _audioInputId.toStdString(),
		.initialOutputDeviceId = _audioOutputId.toStdString(),
		.createAudioDeviceModule = Webrtc::AudioDeviceModuleCreator(
			settings.callAudioBackend()),
		.videoCapture = _cameraCapture,
		.incomingVideoSourcesUpdated = [=](
				std::vector<std::string> endpointIds) {
			crl::on_main(weak, [=, endpoints = std::move(endpointIds)] {
				setIncomingVideoEndpoints(endpoints);
			});
		},
		.requestBroadcastPart = [=](
				int64_t time,
				int64_t period,
				std::function<void(tgcalls::BroadcastPart &&)> done) {
			auto result = std::make_shared<LoadPartTask>(
				weak,
				time,
				period,
				std::move(done));
			crl::on_main(weak, [=]() mutable {
				broadcastPartStart(std::move(result));
			});
			return result;
		},
		.videoContentType = tgcalls::VideoContentType::Generic,
		.requestMediaChannelDescriptions = [=](
			const std::vector<uint32_t> &ssrcs,
			std::function<void(
				std::vector<tgcalls::MediaChannelDescription> &&)> done) {
			auto result = std::make_shared<MediaChannelDescriptionsTask>(
				weak,
				ssrcs,
				std::move(done));
			crl::on_main(weak, [=]() mutable {
				mediaChannelDescriptionsStart(std::move(result));
			});
			return result;
		},
	};
	if (Logs::DebugEnabled()) {
		auto callLogFolder = cWorkingDir() + qsl("DebugLogs");
		auto callLogPath = callLogFolder + qsl("/last_group_call_log.txt");
		auto callLogNative = QDir::toNativeSeparators(callLogPath);
#ifdef Q_OS_WIN
		descriptor.config.logPath.data = callLogNative.toStdWString();
#else // Q_OS_WIN
		const auto callLogUtf = QFile::encodeName(callLogNative);
		descriptor.config.logPath.data.resize(callLogUtf.size());
		ranges::copy(callLogUtf, descriptor.config.logPath.data.begin());
#endif // Q_OS_WIN
		QFile(callLogPath).remove();
		QDir().mkpath(callLogFolder);
	}

	LOG(("Call Info: Creating group instance"));
	_instance = std::make_unique<tgcalls::GroupInstanceCustomImpl>(
		std::move(descriptor));

	_videoEndpointLarge.changes(
	) | rpl::start_with_next([=](const std::string &endpoint) {
		_instance->setFullSizeVideoEndpointId(endpoint);
		_videoLargeTrack = nullptr;
		_videoLargeTrackWrap = nullptr;
		if (endpoint.empty()) {
			return;
		}
		if (!_videoLargeTrackWrap) {
			_videoLargeTrackWrap = std::make_unique<LargeTrack>();
			_videoLargeTrack = &_videoLargeTrackWrap->track;
		}
		_videoLargeTrackWrap->sink = Webrtc::CreateProxySink(
			_videoLargeTrackWrap->track.sink());
		addVideoOutput(endpoint, { _videoLargeTrackWrap->sink });
	}, _lifetime);

	updateInstanceMuteState();
	updateInstanceVolumes();

	if (!_screenEndpoint.empty()) {
		_instance->setIgnoreVideoEndpointIds({ _screenEndpoint });
	}
	//raw->setAudioOutputDuckingEnabled(settings.callAudioDuckingEnabled());
}

void GroupCall::ensureScreencastCreated() {
	if (_screenInstance) {
		return;
	}
	//const auto &settings = Core::App().settings();

	const auto weak = base::make_weak(this);
	//const auto myLevel = std::make_shared<tgcalls::GroupLevelValue>();
	tgcalls::GroupInstanceDescriptor descriptor = {
		.threads = tgcalls::StaticThreads::getThreads(),
		.config = tgcalls::GroupConfig{
		},
		.networkStateUpdated = [=](tgcalls::GroupNetworkState networkState) {
			crl::on_main(weak, [=] {
				setScreenInstanceConnected(networkState);
			});
		},
		.videoCapture = _screenCapture,
		.videoContentType = tgcalls::VideoContentType::Screencast,
	};
//	if (Logs::DebugEnabled()) {
//		auto callLogFolder = cWorkingDir() + qsl("DebugLogs");
//		auto callLogPath = callLogFolder + qsl("/last_group_call_log.txt");
//		auto callLogNative = QDir::toNativeSeparators(callLogPath);
//#ifdef Q_OS_WIN
//		descriptor.config.logPath.data = callLogNative.toStdWString();
//#else // Q_OS_WIN
//		const auto callLogUtf = QFile::encodeName(callLogNative);
//		descriptor.config.logPath.data.resize(callLogUtf.size());
//		ranges::copy(callLogUtf, descriptor.config.logPath.data.begin());
//#endif // Q_OS_WIN
//		QFile(callLogPath).remove();
//		QDir().mkpath(callLogFolder);
//	}

	LOG(("Call Info: Creating group screen instance"));
	_screenInstance = std::make_unique<tgcalls::GroupInstanceCustomImpl>(
		std::move(descriptor));
}

void GroupCall::broadcastPartStart(std::shared_ptr<LoadPartTask> task) {
	const auto raw = task.get();
	const auto time = raw->time();
	const auto scale = raw->scale();
	const auto finish = [=](tgcalls::BroadcastPart &&part) {
		raw->done(std::move(part));
		_broadcastParts.erase(raw);
	};
	using Status = tgcalls::BroadcastPart::Status;
	const auto requestId = _api.request(MTPupload_GetFile(
		MTP_flags(0),
		MTP_inputGroupCallStream(
			inputCall(),
			MTP_long(time),
			MTP_int(scale)),
		MTP_int(0),
		MTP_int(128 * 1024)
	)).done([=](
			const MTPupload_File &result,
			const MTP::Response &response) {
		result.match([&](const MTPDupload_file &data) {
			const auto size = data.vbytes().v.size();
			auto bytes = std::vector<uint8_t>(size);
			memcpy(bytes.data(), data.vbytes().v.constData(), size);
			finish({
				.timestampMilliseconds = time,
				.responseTimestamp = TimestampFromMsgId(response.outerMsgId),
				.status = Status::Success,
				.oggData = std::move(bytes),
			});
		}, [&](const MTPDupload_fileCdnRedirect &data) {
			LOG(("Voice Chat Stream Error: fileCdnRedirect received."));
			finish({
				.timestampMilliseconds = time,
				.responseTimestamp = TimestampFromMsgId(response.outerMsgId),
				.status = Status::ResyncNeeded,
			});
		});
	}).fail([=](const MTP::Error &error, const MTP::Response &response) {
		if (error.type() == u"GROUPCALL_JOIN_MISSING"_q
			|| error.type() == u"GROUPCALL_FORBIDDEN"_q) {
			for (const auto &[task, part] : _broadcastParts) {
				_api.request(part.requestId).cancel();
			}
			setState(State::Joining);
			rejoin();
			return;
		}
		const auto status = (MTP::IsFloodError(error)
			|| error.type() == u"TIME_TOO_BIG"_q)
			? Status::NotReady
			: Status::ResyncNeeded;
		finish({
			.timestampMilliseconds = time,
			.responseTimestamp = TimestampFromMsgId(response.outerMsgId),
			.status = status,
		});
	}).handleAllErrors().toDC(
		MTP::groupCallStreamDcId(_broadcastDcId)
	).send();
	_broadcastParts.emplace(raw, LoadingPart{ std::move(task), requestId });
}

void GroupCall::broadcastPartCancel(not_null<LoadPartTask*> task) {
	const auto i = _broadcastParts.find(task);
	if (i != end(_broadcastParts)) {
		_api.request(i->second.requestId).cancel();
		_broadcastParts.erase(i);
	}
}

void GroupCall::mediaChannelDescriptionsStart(
		std::shared_ptr<MediaChannelDescriptionsTask> task) {
	const auto raw = task.get();

	const auto real = lookupReal();
	if (!real || (_instanceMode == InstanceMode::None)) {
		for (const auto ssrc : task->ssrcs()) {
			_unresolvedSsrcs.emplace(ssrc);
		}
		_mediaChannelDescriptionses.emplace(std::move(task));
		return;
	}
	if (!mediaChannelDescriptionsFill(task.get())) {
		_mediaChannelDescriptionses.emplace(std::move(task));
		Assert(!_unresolvedSsrcs.empty());
	}
	if (!_unresolvedSsrcs.empty()) {
		real->resolveParticipants(base::take(_unresolvedSsrcs));
	}
}

bool GroupCall::mediaChannelDescriptionsFill(
		not_null<MediaChannelDescriptionsTask*> task,
		Fn<bool(uint32)> resolved) {
	using Channel = tgcalls::MediaChannelDescription;
	auto result = false;
	const auto real = lookupReal();
	Assert(real != nullptr);
	const auto &existing = real->participants();
	for (const auto ssrc : task->ssrcs()) {
		const auto add = [&](
				std::optional<Channel> channel,
				bool screen = false) {
			if (task->finishWithAdding(ssrc, std::move(channel), screen)) {
				result = true;
			}
		};
		const auto addVideoChannel = [&](
				not_null<PeerData*> participantPeer,
				const auto field) {
			const auto i = ranges::find(
				existing,
				participantPeer,
				&Data::GroupCallParticipant::peer);
			Assert(i != end(existing));
			Assert(i->videoParams != nullptr);
			const auto &params = i->videoParams.get()->*field;
			Assert(!params.empty());
			add(Channel{
				.type = Channel::Type::Video,
				.audioSsrc = i->ssrc,
				.videoInformation = params.json.toStdString(),
			}, (field == &ParticipantVideoParams::screen));
		};
		if (const auto byAudio = real->participantPeerByAudioSsrc(ssrc)) {
			add(Channel{
				.type = Channel::Type::Audio,
				.audioSsrc = ssrc,
			});
		} else if (const auto byCamera
			= real->participantPeerByCameraSsrc(ssrc)) {
			addVideoChannel(byCamera, &ParticipantVideoParams::camera);
		} else if (const auto byScreen
			= real->participantPeerByScreenSsrc(ssrc)) {
			addVideoChannel(byScreen, &ParticipantVideoParams::screen);
		} else if (!resolved) {
			_unresolvedSsrcs.emplace(ssrc);
		} else if (resolved(ssrc)) {
			add(std::nullopt);
		}
	}
	return result;
}

void GroupCall::mediaChannelDescriptionsCancel(
		not_null<MediaChannelDescriptionsTask*> task) {
	const auto i = _mediaChannelDescriptionses.find(task.get());
	if (i != end(_mediaChannelDescriptionses)) {
		_mediaChannelDescriptionses.erase(i);
	}
}

void GroupCall::setIncomingVideoEndpoints(
		const std::vector<std::string> &endpoints) {
	auto newLarge = _videoEndpointLarge.current();
	auto newLargeFound = false;
	auto removed = _incomingVideoEndpoints;
	const auto feedOne = [&](const std::string &endpoint) {
		if (endpoint.empty()) {
			return;
		} else if (endpoint == newLarge) {
			newLargeFound = true;
		}
		if (!removed.remove(endpoint)) {
			_incomingVideoEndpoints.emplace(endpoint);
			if (_activeVideoEndpoints.contains(endpoint)) {
				_streamsVideoUpdated.fire({ endpoint, true });
			}
		}
	};
	for (const auto &endpoint : endpoints) {
		if (endpoint != _cameraEndpoint && endpoint != _screenEndpoint) {
			feedOne(endpoint);
		}
	}
	feedOne(cameraSharingEndpoint());
	feedOne(screenSharingEndpoint());
	if (!newLarge.empty() && !newLargeFound) {
		_videoEndpointPinned = newLarge = std::string();
	}
	if (newLarge.empty()) {
		_videoEndpointLarge = chooseLargeVideoEndpoint();
	}
	for (const auto &endpoint : removed) {
		if (_activeVideoEndpoints.contains(endpoint)) {
			_streamsVideoUpdated.fire({ endpoint, false });
		}
	}
}

void GroupCall::fillActiveVideoEndpoints() {
	const auto real = lookupReal();
	Assert(real != nullptr);

	const auto &participants = real->participants();
	auto newLarge = _videoEndpointLarge.current();
	auto newLargeFound = false;
	auto removed = _activeVideoEndpoints;
	const auto feedOne = [&](
			const std::string &endpoint,
			EndpointType type) {
		if (endpoint.empty()) {
			return;
		} else if (endpoint == newLarge) {
			newLargeFound = true;
		}
		if (!removed.remove(endpoint)) {
			_activeVideoEndpoints.emplace(endpoint, type);
			if (_incomingVideoEndpoints.contains(endpoint)) {
				_streamsVideoUpdated.fire({ endpoint, true });
			}
		}
	};
	for (const auto &participant : participants) {
		const auto camera = participant.cameraEndpoint();
		if (camera != _cameraEndpoint && camera != _screenEndpoint) {
			feedOne(camera, EndpointType::Camera);
		}
		const auto screen = participant.screenEndpoint();
		if (screen != _cameraEndpoint && screen != _screenEndpoint) {
			feedOne(screen, EndpointType::Screen);
		}
	}
	feedOne(cameraSharingEndpoint(), EndpointType::Camera);
	feedOne(screenSharingEndpoint(), EndpointType::Screen);
	if (!newLarge.empty() && !newLargeFound) {
		_videoEndpointPinned = newLarge = std::string();
	}
	if (newLarge.empty()) {
		_videoEndpointLarge = chooseLargeVideoEndpoint();
	}
	for (const auto &[endpoint, type] : removed) {
		if (_activeVideoEndpoints.remove(endpoint)) {
			_streamsVideoUpdated.fire({ endpoint, false });
		}
	}
}

GroupCall::EndpointType GroupCall::activeVideoEndpointType(
		const std::string &endpoint) const {
	if (endpoint.empty()) {
		return EndpointType::None;
	}
	const auto i = _activeVideoEndpoints.find(endpoint);
	return (i != end(_activeVideoEndpoints))
		? i->second
		: EndpointType::None;
}

std::string GroupCall::chooseLargeVideoEndpoint() const {
	const auto real = lookupReal();
	if (!real) {
		return std::string();
	}
	auto anyEndpoint = std::string();
	auto screenEndpoint = std::string();
	auto speakingEndpoint = std::string();
	auto soundingEndpoint = std::string();
	const auto &myCameraEndpoint = cameraSharingEndpoint();
	const auto &myScreenEndpoint = screenSharingEndpoint();
	const auto &participants = real->participants();
	for (const auto &endpoint : _incomingVideoEndpoints) {
		if (!_activeVideoEndpoints.contains(endpoint)
			|| endpoint == _cameraEndpoint
			|| endpoint == _screenEndpoint) {
			continue;
		}
		if (const auto participant = real->participantByEndpoint(endpoint)) {
			if (screenEndpoint.empty()
				&& participant->videoParams->screen.endpoint == endpoint) {
				screenEndpoint = endpoint;
				break;
			}
			if (speakingEndpoint.empty() && participant->speaking) {
				speakingEndpoint = endpoint;
			}
			if (soundingEndpoint.empty() && participant->sounding) {
				soundingEndpoint = endpoint;
			}
			if (anyEndpoint.empty()) {
				anyEndpoint = endpoint;
			}
		}
	}
	return !screenEndpoint.empty()
		? screenEndpoint
		: streamsVideo(myScreenEndpoint)
		? myScreenEndpoint
		: !speakingEndpoint.empty()
		? speakingEndpoint
		: !soundingEndpoint.empty()
		? soundingEndpoint
		: !anyEndpoint.empty()
		? anyEndpoint
		: streamsVideo(myCameraEndpoint)
		? myCameraEndpoint
		: std::string();
}

void GroupCall::updateInstanceMuteState() {
	Expects(_instance != nullptr);

	const auto state = muted();
	_instance->setIsMuted(state != MuteState::Active
		&& state != MuteState::PushToTalk);
}

void GroupCall::updateInstanceVolumes() {
	const auto real = lookupReal();
	if (!real) {
		return;
	}

	const auto &participants = real->participants();
	for (const auto &participant : participants) {
		const auto setVolume = participant.mutedByMe
			|| (participant.volume != Group::kDefaultVolume);
		if (setVolume && participant.ssrc) {
			_instance->setVolume(
				participant.ssrc,
				(participant.mutedByMe
					? 0.
					: (participant.volume / float64(Group::kDefaultVolume))));
		}
	}
}

void GroupCall::audioLevelsUpdated(const tgcalls::GroupLevelsUpdate &data) {
	Expects(!data.updates.empty());

	auto check = false;
	auto checkNow = false;
	const auto now = crl::now();
	for (const auto &[ssrcOrZero, value] : data.updates) {
		const auto ssrc = ssrcOrZero ? ssrcOrZero : _mySsrc;
		const auto level = value.level;
		const auto voice = value.voice;
		const auto me = (ssrc == _mySsrc);
		_levelUpdates.fire(LevelUpdate{
			.ssrc = ssrc,
			.value = level,
			.voice = voice,
			.me = me
		});
		if (level <= kSpeakLevelThreshold) {
			continue;
		}
		if (me
			&& voice
			&& (!_lastSendProgressUpdate
				|| _lastSendProgressUpdate + kUpdateSendActionEach < now)) {
			_lastSendProgressUpdate = now;
			_peer->session().sendProgressManager().update(
				_history,
				Api::SendProgressType::Speaking);
		}

		check = true;
		const auto i = _lastSpoke.find(ssrc);
		if (i == _lastSpoke.end()) {
			_lastSpoke.emplace(ssrc, Data::LastSpokeTimes{
				.anything = now,
				.voice = voice ? now : 0,
			});
			checkNow = true;
		} else {
			if ((i->second.anything + kCheckLastSpokeInterval / 3 <= now)
				|| (voice
					&& i->second.voice + kCheckLastSpokeInterval / 3 <= now)) {
				checkNow = true;
			}
			i->second.anything = now;
			if (voice) {
				i->second.voice = now;
			}
		}
	}
	if (checkNow) {
		checkLastSpoke();
	} else if (check && !_lastSpokeCheckTimer.isActive()) {
		_lastSpokeCheckTimer.callEach(kCheckLastSpokeInterval / 2);
	}
}

void GroupCall::checkLastSpoke() {
	const auto real = lookupReal();
	if (!real) {
		return;
	}

	auto hasRecent = false;
	const auto now = crl::now();
	auto list = base::take(_lastSpoke);
	for (auto i = list.begin(); i != list.end();) {
		const auto [ssrc, when] = *i;
		if (when.anything + kCheckLastSpokeInterval >= now) {
			hasRecent = true;
			++i;
		} else {
			i = list.erase(i);
		}
		real->applyLastSpoke(ssrc, when, now);
	}
	_lastSpoke = std::move(list);

	if (!hasRecent) {
		_lastSpokeCheckTimer.cancel();
	} else if (!_lastSpokeCheckTimer.isActive()) {
		_lastSpokeCheckTimer.callEach(kCheckLastSpokeInterval / 3);
	}
}

void GroupCall::checkJoined() {
	if (state() != State::Connecting || !_id || !_mySsrc) {
		return;
	}
	auto sources = QVector<MTPint>(1, MTP_int(_mySsrc));
	if (_screenSsrc) {
		sources.push_back(MTP_int(_screenSsrc));
	}
	_api.request(MTPphone_CheckGroupCall(
		inputCall(),
		MTP_vector<MTPint>(std::move(sources))
	)).done([=](const MTPVector<MTPint> &result) {
		if (!ranges::contains(result.v, MTP_int(_mySsrc))) {
			LOG(("Call Info: Rejoin after no _mySsrc in checkGroupCall."));
			rejoin();
		} else {
			if (state() == State::Connecting) {
				_checkJoinedTimer.callOnce(kCheckJoinedTimeout);
			}
			if (_screenSsrc
				&& !ranges::contains(result.v, MTP_int(_screenSsrc))
				&& isSharingScreen()) {
				LOG(("Call Info: "
					"Screen rejoin after _screenSsrc not found."));
				rejoinPresentation();
			}
		}
	}).fail([=](const MTP::Error &error) {
 		LOG(("Call Info: Full rejoin after error '%1' in checkGroupCall."
			).arg(error.type()));
		rejoin();
	}).send();
}

void GroupCall::setInstanceConnected(
		tgcalls::GroupNetworkState networkState) {
	const auto inTransit = networkState.isTransitioningFromBroadcastToRtc;
	const auto instanceState = !networkState.isConnected
		? InstanceState::Disconnected
		: inTransit
		? InstanceState::TransitionToRtc
		: InstanceState::Connected;
	const auto connected = (instanceState != InstanceState::Disconnected);
	if (_instanceState.current() == instanceState
		&& _instanceTransitioning == inTransit) {
		return;
	}
	const auto nowCanSpeak = connected
		&& _instanceTransitioning
		&& !inTransit
		&& (muted() == MuteState::Muted);
	_instanceTransitioning = inTransit;
	_instanceState = instanceState;
	if (state() == State::Connecting && connected) {
		setState(State::Joined);
	} else if (state() == State::Joined && !connected) {
		setState(State::Connecting);
	}
	if (nowCanSpeak) {
		notifyAboutAllowedToSpeak();
	}
	if (!_hadJoinedState && state() == State::Joined) {
		checkFirstTimeJoined();
	}
}

void GroupCall::setScreenInstanceConnected(
		tgcalls::GroupNetworkState networkState) {
	const auto inTransit = networkState.isTransitioningFromBroadcastToRtc;
	const auto screenInstanceState = !networkState.isConnected
		? InstanceState::Disconnected
		: inTransit
		? InstanceState::TransitionToRtc
		: InstanceState::Connected;
	const auto connected = (screenInstanceState
		!= InstanceState::Disconnected);
	if (_screenInstanceState.current() == screenInstanceState) {
		return;
	}
	_screenInstanceState = screenInstanceState;
}

void GroupCall::checkFirstTimeJoined() {
	if (_hadJoinedState || state() != State::Joined) {
		return;
	}
	_hadJoinedState = true;
	applyGlobalShortcutChanges();
	_delegate->groupCallPlaySound(Delegate::GroupCallSound::Started);
}

void GroupCall::notifyAboutAllowedToSpeak() {
	if (!_hadJoinedState) {
		return;
	}
	_delegate->groupCallPlaySound(
		Delegate::GroupCallSound::AllowedToSpeak);
	_allowedToSpeakNotifications.fire({});
}

void GroupCall::setInstanceMode(InstanceMode mode) {
	Expects(_instance != nullptr);

	_instanceMode = mode;

	using Mode = tgcalls::GroupConnectionMode;
	_instance->setConnectionMode([&] {
		switch (_instanceMode) {
		case InstanceMode::None: return Mode::GroupConnectionModeNone;
		case InstanceMode::Rtc: return Mode::GroupConnectionModeRtc;
		case InstanceMode::Stream: return Mode::GroupConnectionModeBroadcast;
		}
		Unexpected("Mode in GroupCall::setInstanceMode.");
	}(), true);
}

void GroupCall::setScreenInstanceMode(InstanceMode mode) {
	Expects(_screenInstance != nullptr);

	_screenInstanceMode = mode;

	using Mode = tgcalls::GroupConnectionMode;
	_screenInstance->setConnectionMode([&] {
		switch (_instanceMode) {
		case InstanceMode::None: return Mode::GroupConnectionModeNone;
		case InstanceMode::Rtc: return Mode::GroupConnectionModeRtc;
		case InstanceMode::Stream: return Mode::GroupConnectionModeBroadcast;
		}
		Unexpected("Mode in GroupCall::setInstanceMode.");
	}(), true);
}

void GroupCall::maybeSendMutedUpdate(MuteState previous) {
	// Send Active <-> !Active or ForceMuted <-> RaisedHand changes.
	const auto now = muted();
	if ((previous == MuteState::Active && now == MuteState::Muted)
		|| (now == MuteState::Active
			&& (previous == MuteState::Muted
				|| previous == MuteState::PushToTalk))) {
		sendSelfUpdate(SendUpdateType::Mute);
	} else if ((now == MuteState::ForceMuted
		&& previous == MuteState::RaisedHand)
		|| (now == MuteState::RaisedHand
			&& previous == MuteState::ForceMuted)) {
		sendSelfUpdate(SendUpdateType::RaiseHand);
	}
}

void GroupCall::sendSelfUpdate(SendUpdateType type) {
	_api.request(_updateMuteRequestId).cancel();
	using Flag = MTPphone_EditGroupCallParticipant::Flag;
	_updateMuteRequestId = _api.request(MTPphone_EditGroupCallParticipant(
		MTP_flags((type == SendUpdateType::RaiseHand)
			? Flag::f_raise_hand
			: (type == SendUpdateType::VideoMuted)
			? Flag::f_video_muted
			: Flag::f_muted),
		inputCall(),
		_joinAs->input,
		MTP_bool(muted() != MuteState::Active),
		MTP_int(100000), // volume
		MTP_bool(muted() == MuteState::RaisedHand),
		MTP_bool(_cameraOutgoing->state() != Webrtc::VideoState::Active)
	)).done([=](const MTPUpdates &result) {
		_updateMuteRequestId = 0;
		_peer->session().api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		_updateMuteRequestId = 0;
		if (error.type() == u"GROUPCALL_FORBIDDEN"_q) {
			LOG(("Call Info: Rejoin after error '%1' in editGroupCallMember."
				).arg(error.type()));
			rejoin();
		}
	}).send();
}

void GroupCall::pinVideoEndpoint(const std::string &endpoint) {
	if (endpoint.empty()) {
		_videoEndpointPinned = endpoint;
	} else if (streamsVideo(endpoint)) {
		_videoEndpointPinned = std::string();
		_videoEndpointLarge = endpoint;
		_videoEndpointPinned = endpoint;
	}
}

void GroupCall::setCurrentAudioDevice(bool input, const QString &deviceId) {
	if (input) {
		_mediaDevices->switchToAudioInput(deviceId);
	} else {
		_mediaDevices->switchToAudioOutput(deviceId);
	}
}

void GroupCall::setCurrentVideoDevice(const QString &deviceId) {
	_mediaDevices->switchToVideoInput(deviceId);
}

void GroupCall::toggleMute(const Group::MuteRequest &data) {
	if (data.locallyOnly) {
		applyParticipantLocally(data.peer, data.mute, std::nullopt);
	} else {
		editParticipant(data.peer, data.mute, std::nullopt);
	}
}

void GroupCall::changeVolume(const Group::VolumeRequest &data) {
	if (data.locallyOnly) {
		applyParticipantLocally(data.peer, false, data.volume);
	} else {
		editParticipant(data.peer, false, data.volume);
	}
}

void GroupCall::editParticipant(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume) {
	const auto participant = LookupParticipant(_peer, _id, participantPeer);
	if (!participant) {
		return;
	}
	applyParticipantLocally(participantPeer, mute, volume);

	using Flag = MTPphone_EditGroupCallParticipant::Flag;
	const auto flags = Flag::f_muted
		| (volume.has_value() ? Flag::f_volume : Flag(0));
	_api.request(MTPphone_EditGroupCallParticipant(
		MTP_flags(flags),
		inputCall(),
		participantPeer->input,
		MTP_bool(mute),
		MTP_int(std::clamp(volume.value_or(0), 1, Group::kMaxVolume)),
		MTPBool(), // raise_hand
		MTPBool() // video_muted
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"GROUPCALL_FORBIDDEN"_q) {
			LOG(("Call Info: Rejoin after error '%1' in editGroupCallMember."
				).arg(error.type()));
			rejoin();
		}
	}).send();
}

std::variant<int, not_null<UserData*>> GroupCall::inviteUsers(
		const std::vector<not_null<UserData*>> &users) {
	const auto real = lookupReal();
	if (!real) {
		return 0;
	}
	const auto owner = &_peer->owner();
	const auto &invited = owner->invitedToCallUsers(_id);
	const auto &participants = real->participants();
	auto &&toInvite = users | ranges::views::filter([&](
			not_null<UserData*> user) {
		return !invited.contains(user) && !ranges::contains(
			participants,
			user,
			&Data::GroupCall::Participant::peer);
	});

	auto count = 0;
	auto slice = QVector<MTPInputUser>();
	auto result = std::variant<int, not_null<UserData*>>(0);
	slice.reserve(kMaxInvitePerSlice);
	const auto sendSlice = [&] {
		count += slice.size();
		_api.request(MTPphone_InviteToGroupCall(
			inputCall(),
			MTP_vector<MTPInputUser>(slice)
		)).done([=](const MTPUpdates &result) {
			_peer->session().api().applyUpdates(result);
		}).send();
		slice.clear();
	};
	for (const auto user : users) {
		if (!count && slice.empty()) {
			result = user;
		}
		owner->registerInvitedToCallUser(_id, _peer, user);
		slice.push_back(user->inputUser);
		if (slice.size() == kMaxInvitePerSlice) {
			sendSlice();
		}
	}
	if (count != 0 || slice.size() != 1) {
		result = int(count + slice.size());
	}
	if (!slice.empty()) {
		sendSlice();
	}
	return result;
}

auto GroupCall::ensureGlobalShortcutManager()
-> std::shared_ptr<GlobalShortcutManager> {
	if (!_shortcutManager) {
		_shortcutManager = base::CreateGlobalShortcutManager();
	}
	return _shortcutManager;
}

void GroupCall::applyGlobalShortcutChanges() {
	auto &settings = Core::App().settings();
	if (!settings.groupCallPushToTalk()
		|| settings.groupCallPushToTalkShortcut().isEmpty()
		|| !base::GlobalShortcutsAvailable()
		|| !base::GlobalShortcutsAllowed()) {
		_shortcutManager = nullptr;
		_pushToTalk = nullptr;
		return;
	}
	ensureGlobalShortcutManager();
	const auto shortcut = _shortcutManager->shortcutFromSerialized(
		settings.groupCallPushToTalkShortcut());
	if (!shortcut) {
		settings.setGroupCallPushToTalkShortcut(QByteArray());
		settings.setGroupCallPushToTalk(false);
		Core::App().saveSettingsDelayed();
		_shortcutManager = nullptr;
		_pushToTalk = nullptr;
		return;
	}
	if (_pushToTalk) {
		if (shortcut->serialize() == _pushToTalk->serialize()) {
			return;
		}
		_shortcutManager->stopWatching(_pushToTalk);
	}
	_pushToTalk = shortcut;
	_shortcutManager->startWatching(_pushToTalk, [=](bool pressed) {
		pushToTalk(
			pressed,
			Core::App().settings().groupCallPushToTalkDelay());
	});
}

void GroupCall::pushToTalk(bool pressed, crl::time delay) {
	if (muted() == MuteState::ForceMuted
		|| muted() == MuteState::RaisedHand
		|| muted() == MuteState::Active) {
		return;
	} else if (pressed) {
		_pushToTalkCancelTimer.cancel();
		setMuted(MuteState::PushToTalk);
	} else if (delay) {
		_pushToTalkCancelTimer.callOnce(delay);
	} else {
		pushToTalkCancel();
	}
}

void GroupCall::pushToTalkCancel() {
	_pushToTalkCancelTimer.cancel();
	if (muted() == MuteState::PushToTalk) {
		setMuted(MuteState::Muted);
	}
}

auto GroupCall::otherParticipantStateValue() const
-> rpl::producer<Group::ParticipantState> {
	return _otherParticipantStateValue.events();
}

MTPInputGroupCall GroupCall::inputCall() const {
	Expects(_id != 0);

	return MTP_inputGroupCall(
		MTP_long(_id),
		MTP_long(_accessHash));
}

void GroupCall::destroyController() {
	if (_instance) {
		DEBUG_LOG(("Call Info: Destroying call controller.."));
		_instance.reset();
		DEBUG_LOG(("Call Info: Call controller destroyed."));
	}
}

void GroupCall::destroyScreencast() {
	if (_screenInstance) {
		DEBUG_LOG(("Call Info: Destroying call screen controller.."));
		_screenInstance.reset();
		DEBUG_LOG(("Call Info: Call screen controller destroyed."));
	}
}

} // namespace Calls
