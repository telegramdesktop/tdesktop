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
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "base/global_shortcuts.h"
#include "base/openssl_help.h"
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_media_devices.h"
#include "webrtc/webrtc_create_adm.h"

#include <tgcalls/group/GroupInstanceCustomImpl.h>
#include <tgcalls/VideoCaptureInterface.h>
#include <tgcalls/StaticThreads.h>
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
constexpr auto kFixManualLargeVideoDuration = 5 * crl::time(1000);
constexpr auto kFixSpeakingLargeVideoDuration = 3 * crl::time(1000);
constexpr auto kFullAsMediumsCount = 4; // 1 Full is like 4 Mediums.
constexpr auto kMaxMediumQualities = 16; // 4 Fulls or 16 Mediums.

[[nodiscard]] std::unique_ptr<Webrtc::MediaDevices> CreateMediaDevices() {
	const auto &settings = Core::App().settings();
	return Webrtc::CreateMediaDevices(
		settings.callInputDeviceId(),
		settings.callOutputDeviceId(),
		settings.callVideoInputDeviceId());
}

[[nodiscard]] const Data::GroupCallParticipant *LookupParticipant(
		not_null<PeerData*> peer,
		uint64 id,
		not_null<PeerData*> participantPeer) {
	const auto call = peer->groupCall();
	return (id && call && call->id() == id)
		? call->participantByPeer(participantPeer)
		: nullptr;
}

[[nodiscard]] double TimestampFromMsgId(mtpMsgId msgId) {
	return msgId / double(1ULL << 32);
}

[[nodiscard]] uint64 FindLocalRaisedHandRating(
		const std::vector<Data::GroupCallParticipant> &list) {
	const auto i = ranges::max_element(
		list,
		ranges::less(),
		&Data::GroupCallParticipant::raisedHandRating);
	return (i == end(list)) ? 1 : (i->raisedHandRating + 1);
}

struct JoinVideoEndpoint {
	std::string id;
};

struct JoinBroadcastStream {
};

using JoinClientFields = std::variant<
	v::null_t,
	JoinVideoEndpoint,
	JoinBroadcastStream>;

[[nodiscard]] JoinClientFields ParseJoinResponse(const QByteArray &json) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("API Error: "
			"Failed to parse join response params, error: %1."
			).arg(error.errorString()));
		return {};
	} else if (!document.isObject()) {
		LOG(("API Error: "
			"Not an object received in join response params."));
		return {};
	}
	if (document.object().value("stream").toBool()) {
		return JoinBroadcastStream{};
	}
	const auto video = document.object().value("video").toObject();
	return JoinVideoEndpoint{
		video.value("endpoint").toString().toStdString(),
	};
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

struct GroupCall::SinkPointer {
	std::weak_ptr<Webrtc::SinkInterface> data;
};

struct GroupCall::VideoTrack {
	VideoTrack(bool paused, bool requireARGB32, not_null<PeerData*> peer);

	Webrtc::VideoTrack track;
	rpl::variable<QSize> trackSize;
	not_null<PeerData*> peer;
	rpl::lifetime lifetime;
	Group::VideoQuality quality = Group::VideoQuality();
	bool shown = false;
};

GroupCall::VideoTrack::VideoTrack(
	bool paused,
	bool requireARGB32,
	not_null<PeerData*> peer)
: track((paused
	? Webrtc::VideoState::Paused
	: Webrtc::VideoState::Active),
	requireARGB32)
, peer(peer) {
}

[[nodiscard]] bool IsGroupCallAdmin(
		not_null<PeerData*> peer,
		not_null<PeerData*> participantPeer) {
	const auto user = participantPeer->asUser();
	if (!user) {
		return (peer == participantPeer);
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
			return (i->second.rights.flags & ChatAdminRight::ManageCall);
		}
	}
	return false;
}

struct VideoParams {
	std::string endpointId;
	std::vector<tgcalls::MediaSsrcGroup> ssrcGroups;
	uint32 additionalSsrc = 0;
	bool paused = false;

	[[nodiscard]] bool empty() const {
		return !additionalSsrc && (endpointId.empty() || ssrcGroups.empty());
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}
};

struct ParticipantVideoParams {
	VideoParams camera;
	VideoParams screen;
};

[[nodiscard]] bool VideoParamsAreEqual(
		const VideoParams &was,
		const tl::conditional<MTPGroupCallParticipantVideo> &now) {
	if (!now) {
		return !was;
	}
	return now->match([&](const MTPDgroupCallParticipantVideo &data) {
		if (data.is_paused() != was.paused
			|| data.vaudio_source().value_or_empty() != was.additionalSsrc) {
			return false;
		}
		if (gsl::make_span(data.vendpoint().v)
			!= gsl::make_span(was.endpointId)) {
			return false;
		}
		const auto &list = data.vsource_groups().v;
		if (list.size() != was.ssrcGroups.size()) {
			return false;
		}
		auto index = 0;
		for (const auto &group : list) {
			const auto equal = group.match([&](
					const MTPDgroupCallParticipantVideoSourceGroup &data) {
				const auto &group = was.ssrcGroups[index++];
				if (gsl::make_span(data.vsemantics().v)
					!= gsl::make_span(group.semantics)) {
					return false;
				}
				const auto list = data.vsources().v;
				if (list.size() != group.ssrcs.size()) {
					return false;
				}
				auto i = 0;
				for (const auto &ssrc : list) {
					if (ssrc.v != group.ssrcs[i++]) {
						return false;
					}
				}
				return true;
			});
			if (!equal) {
				return false;
			}
		}
		return true;
	});
}

[[nodiscard]] VideoParams ParseVideoParams(
		const tl::conditional<MTPGroupCallParticipantVideo> &params) {
	if (!params) {
		return VideoParams();
	}
	auto result = VideoParams();
	params->match([&](const MTPDgroupCallParticipantVideo &data) {
		result.paused = data.is_paused();
		result.endpointId = data.vendpoint().v.toStdString();
		result.additionalSsrc = data.vaudio_source().value_or_empty();
		const auto &list = data.vsource_groups().v;
		result.ssrcGroups.reserve(list.size());
		for (const auto &group : list) {
			group.match([&](
					const MTPDgroupCallParticipantVideoSourceGroup &data) {
				const auto &list = data.vsources().v;
				auto ssrcs = std::vector<uint32_t>();
				ssrcs.reserve(list.size());
				for (const auto &ssrc : list) {
					ssrcs.push_back(ssrc.v);
				}
				result.ssrcGroups.push_back({
					.semantics = data.vsemantics().v.toStdString(),
					.ssrcs = std::move(ssrcs),
				});
			});
		}
	});
	return result;
}

const std::string &GetCameraEndpoint(
		const std::shared_ptr<ParticipantVideoParams> &params) {
	return params ? params->camera.endpointId : EmptyString();
}

const std::string &GetScreenEndpoint(
		const std::shared_ptr<ParticipantVideoParams> &params) {
	return params ? params->screen.endpointId : EmptyString();
}

bool IsCameraPaused(const std::shared_ptr<ParticipantVideoParams> &params) {
	return params && params->camera.paused;
}

bool IsScreenPaused(const std::shared_ptr<ParticipantVideoParams> &params) {
	return params && params->screen.paused;
}

uint32 GetAdditionalAudioSsrc(
		const std::shared_ptr<ParticipantVideoParams> &params) {
	return params ? params->screen.additionalSsrc : 0;
}

std::shared_ptr<ParticipantVideoParams> ParseVideoParams(
		const tl::conditional<MTPGroupCallParticipantVideo> &camera,
		const tl::conditional<MTPGroupCallParticipantVideo> &screen,
		const std::shared_ptr<ParticipantVideoParams> &existing) {
	using namespace tgcalls;

	if (!camera && !screen) {
		return nullptr;
	}
	if (existing
		&& VideoParamsAreEqual(existing->camera, camera)
		&& VideoParamsAreEqual(existing->screen, screen)) {
		return existing;
	}
	// We don't reuse existing pointer, that way we can compare pointers
	// to see if anything was changed in video params.
	const auto data = /*existing
		? existing
		: */std::make_shared<ParticipantVideoParams>();
	data->camera = ParseVideoParams(camera);
	data->screen = ParseVideoParams(screen);
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

not_null<PeerData*> GroupCall::TrackPeer(
		const std::unique_ptr<VideoTrack> &track) {
	return track->peer;
}

not_null<Webrtc::VideoTrack*> GroupCall::TrackPointer(
		const std::unique_ptr<VideoTrack> &track) {
	return &track->track;
}

rpl::producer<QSize> GroupCall::TrackSizeValue(
		const std::unique_ptr<VideoTrack> &track) {
	return track->trackSize.value();
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
, _canManage(Data::CanManageGroupCallValue(_peer))
, _id(inputCall.c_inputGroupCall().vid().v)
, _scheduleDate(info.scheduleDate)
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
		if (_joinState.ssrc
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
		if (!canManage() && real->joinMuted()) {
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
	setupOutgoingVideo();

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
	destroyScreencast();
	destroyController();
}

bool GroupCall::isSharingScreen() const {
	return _isSharingScreen.current();
}

rpl::producer<bool> GroupCall::isSharingScreenValue() const {
	return _isSharingScreen.value();
}

bool GroupCall::isScreenPaused() const {
	return (_screenState.current() == Webrtc::VideoState::Paused);
}

const std::string &GroupCall::screenSharingEndpoint() const {
	return isSharingScreen() ? _screenEndpoint : EmptyString();
}

bool GroupCall::isSharingCamera() const {
	return _isSharingCamera.current();
}

rpl::producer<bool> GroupCall::isSharingCameraValue() const {
	return _isSharingCamera.value();
}

bool GroupCall::isCameraPaused() const {
	return (_cameraState.current() == Webrtc::VideoState::Paused);
}

const std::string &GroupCall::cameraSharingEndpoint() const {
	return isSharingCamera() ? _cameraEndpoint : EmptyString();
}

QString GroupCall::screenSharingDeviceId() const {
	return isSharingScreen() ? _screenDeviceId : QString();
}

bool GroupCall::screenSharingWithAudio() const {
	return isSharingScreen() && _screenWithAudio;
}

bool GroupCall::mutedByAdmin() const {
	const auto mute = muted();
	return mute == MuteState::ForceMuted || mute == MuteState::RaisedHand;
}

bool GroupCall::canManage() const {
	return _canManage.current();
}

rpl::producer<bool> GroupCall::canManageValue() const {
	return _canManage.value();
}

void GroupCall::toggleVideo(bool active) {
	if (!_instance || !_id) {
		return;
	}
	_cameraState = active
		? Webrtc::VideoState::Active
		: Webrtc::VideoState::Inactive;
}

void GroupCall::toggleScreenSharing(
		std::optional<QString> uniqueId,
		bool withAudio) {
	if (!_instance || !_id) {
		return;
	} else if (!uniqueId) {
		_screenState = Webrtc::VideoState::Inactive;
		return;
	}
	const auto changed = (_screenDeviceId != *uniqueId);
	const auto wasSharing = isSharingScreen();
	_screenDeviceId = *uniqueId;
	_screenWithAudio = withAudio;
	_screenState = Webrtc::VideoState::Active;
	if (changed && wasSharing && isSharingScreen()) {
		_screenCapture->switchToDevice(uniqueId->toStdString());
	}
	if (_screenInstance) {
		_screenInstance->setIsMuted(!withAudio);
	}
}

bool GroupCall::hasVideoWithFrames() const {
	return !_shownVideoTracks.empty();
}

rpl::producer<bool> GroupCall::hasVideoWithFramesValue() const {
	return _videoStreamShownUpdates.events_starting_with(
		VideoStateToggle()
	) | rpl::map([=] {
		return hasVideoWithFrames();
	}) | rpl::distinct_until_changed();
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

	// Postpone creating video tracks, so that we know if Panel
	// supports OpenGL and we don't need ARGB32 frames at all.
	Ui::PostponeCall(this, [=] {
		if (const auto real = lookupReal()) {
			real->participantsReloaded(
			) | rpl::start_with_next([=] {
				fillActiveVideoEndpoints();
			}, _lifetime);
			fillActiveVideoEndpoints();
		}
	});

	using Update = Data::GroupCall::ParticipantUpdate;
	real->participantUpdated(
	) | rpl::start_with_next([=](const Update &data) {
		const auto regularEndpoint = [&](const std::string &endpoint)
		-> const std::string & {
			return (endpoint.empty()
					|| endpoint == _cameraEndpoint
					|| endpoint == _screenEndpoint)
				? EmptyString()
				: endpoint;
		};

		const auto peer = data.was ? data.was->peer : data.now->peer;
		if (peer == _joinAs) {
			const auto working = data.now && data.now->videoJoined;
			if (videoIsWorking() != working) {
				fillActiveVideoEndpoints();
			}
			return;
		}
		const auto &wasCameraEndpoint = data.was
			? regularEndpoint(GetCameraEndpoint(data.was->videoParams))
			: EmptyString();
		const auto &nowCameraEndpoint = data.now
			? regularEndpoint(GetCameraEndpoint(data.now->videoParams))
			: EmptyString();
		const auto wasCameraPaused = !wasCameraEndpoint.empty()
			&& IsCameraPaused(data.was->videoParams);
		const auto nowCameraPaused = !nowCameraEndpoint.empty()
			&& IsCameraPaused(data.now->videoParams);
		if (wasCameraEndpoint != nowCameraEndpoint) {
			markEndpointActive({
				VideoEndpointType::Camera,
				peer,
				nowCameraEndpoint,
			}, true, nowCameraPaused);
			markEndpointActive({
				VideoEndpointType::Camera,
				peer,
				wasCameraEndpoint,
			}, false, false);
		} else if (wasCameraPaused != nowCameraPaused) {
			markTrackPaused({
				VideoEndpointType::Camera,
				peer,
				nowCameraEndpoint,
			}, nowCameraPaused);
		}
		const auto &wasScreenEndpoint = data.was
			? regularEndpoint(data.was->screenEndpoint())
			: EmptyString();
		const auto &nowScreenEndpoint = data.now
			? regularEndpoint(data.now->screenEndpoint())
			: EmptyString();
		const auto wasScreenPaused = !wasScreenEndpoint.empty()
			&& IsScreenPaused(data.was->videoParams);
		const auto nowScreenPaused = !nowScreenEndpoint.empty()
			&& IsScreenPaused(data.now->videoParams);
		if (wasScreenEndpoint != nowScreenEndpoint) {
			markEndpointActive({
				VideoEndpointType::Screen,
				peer,
				nowScreenEndpoint,
			}, true, nowScreenPaused);
			markEndpointActive({
				VideoEndpointType::Screen,
				peer,
				wasScreenEndpoint,
			}, false, false);
		} else if (wasScreenPaused != nowScreenPaused) {
			markTrackPaused({
				VideoEndpointType::Screen,
				peer,
				wasScreenEndpoint,
			}, nowScreenPaused);
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

	real->participantSpeaking(
	) | rpl::filter([=] {
		return _videoEndpointLarge.current();
	}) | rpl::start_with_next([=](not_null<Data::GroupCallParticipant*> p) {
		const auto now = crl::now();
		if (_videoEndpointLarge.current().peer == p->peer) {
			_videoLargeTillTime = std::max(
				_videoLargeTillTime,
				now + kFixSpeakingLargeVideoDuration);
			return;
		} else if (videoEndpointPinned() || _videoLargeTillTime > now) {
			return;
		}
		using Type = VideoEndpointType;
		const auto &params = p->videoParams;
		if (GetCameraEndpoint(params).empty()
			&& GetScreenEndpoint(params).empty()) {
			return;
		}
		const auto tryEndpoint = [&](Type type, const std::string &id) {
			if (id.empty()) {
				return false;
			}
			const auto endpoint = VideoEndpoint{ type, p->peer, id };
			if (!shownVideoTracks().contains(endpoint)) {
				return false;
			}
			setVideoEndpointLarge(endpoint);
			return true;
		};
		if (tryEndpoint(Type::Screen, GetScreenEndpoint(params))
			|| tryEndpoint(Type::Camera, GetCameraEndpoint(params))) {
			_videoLargeTillTime = now + kFixSpeakingLargeVideoDuration;
		}
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
	const auto current = _state.current();
	if (current == State::Failed) {
		return;
	} else if (current == State::Ended && state != State::Failed) {
		return;
	} else if (current == State::FailedHangingUp && state != State::Failed) {
		return;
	} else if (current == State::HangingUp
		&& state != State::Ended
		&& state != State::Failed) {
		return;
	}
	if (current == state) {
		return;
	}
	_state = state;

	if (state == State::Joined) {
		stopConnectingSound();
		if (const auto call = _peer->groupCall(); call && call->id() == _id) {
			call->setInCall();
		}
		if (!videoIsWorking()) {
			refreshHasNotShownVideo();
		}
	}

	if (false
		|| state == State::Ended
		|| state == State::Failed) {
		// Destroy controller before destroying Call Panel,
		// so that the panel hide animation is smooth.
		destroyScreencast();
		destroyController();
	}
	switch (state) {
	case State::HangingUp:
	case State::FailedHangingUp:
		stopConnectingSound();
		_delegate->groupCallPlaySound(Delegate::GroupCallSound::Ended);
		break;
	case State::Ended:
		stopConnectingSound();
		_delegate->groupCallFinished(this);
		break;
	case State::Failed:
		stopConnectingSound();
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
	const auto state = _state.current();
	if (_connectingSoundTimer.isActive()
		|| state == State::HangingUp
		|| state == State::FailedHangingUp
		|| state == State::Ended
		|| state == State::Failed) {
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
			_instance->removeSsrcs({
				update.was->ssrc,
				GetAdditionalAudioSsrc(update.was->videoParams),
			});
		} else {
			updateInstanceVolume(update.was, *update.now);
		}
	}, _lifetime);

	_peer->session().updates().addActiveChat(
		_peerStream.events_starting_with_copy(_peer));
	SubscribeToMigration(_peer, _lifetime, [=](not_null<ChannelData*> group) {
		_peer = group;
		_canManage = Data::CanManageGroupCallValue(_peer);
		_peerStream.fire_copy(group);
	});
}

void GroupCall::setScreenEndpoint(std::string endpoint) {
	if (_screenEndpoint == endpoint) {
		return;
	}
	if (!_screenEndpoint.empty()) {
		markEndpointActive({
			VideoEndpointType::Screen,
			_joinAs,
			_screenEndpoint
		}, false, false);
	}
	_screenEndpoint = std::move(endpoint);
	if (_screenEndpoint.empty()) {
		return;
	}
	if (isSharingScreen()) {
		markEndpointActive({
			VideoEndpointType::Screen,
			_joinAs,
			_screenEndpoint
		}, true, isScreenPaused());
	}
}

void GroupCall::setCameraEndpoint(std::string endpoint) {
	if (_cameraEndpoint == endpoint) {
		return;
	}
	if (!_cameraEndpoint.empty()) {
		markEndpointActive({
			VideoEndpointType::Camera,
			_joinAs,
			_cameraEndpoint
		}, false, false);
	}
	_cameraEndpoint = std::move(endpoint);
	if (_cameraEndpoint.empty()) {
		return;
	}
	if (isSharingCamera()) {
		markEndpointActive({
			VideoEndpointType::Camera,
			_joinAs,
			_cameraEndpoint
		}, true, isCameraPaused());
	}
}

void GroupCall::addVideoOutput(
		const std::string &endpoint,
		SinkPointer sink) {
	if (_cameraEndpoint == endpoint) {
		if (auto strong = sink.data.lock()) {
			_cameraCapture->setOutput(std::move(strong));
		}
	} else if (_screenEndpoint == endpoint) {
		if (auto strong = sink.data.lock()) {
			_screenCapture->setOutput(std::move(strong));
		}
	} else if (_instance) {
		_instance->addIncomingVideoOutput(endpoint, std::move(sink.data));
	} else {
		_pendingVideoOutputs.emplace(endpoint, std::move(sink));
	}
}

void GroupCall::markEndpointActive(
		VideoEndpoint endpoint,
		bool active,
		bool paused) {
	if (!endpoint) {
		return;
	} else if (active && !videoIsWorking()) {
		refreshHasNotShownVideo();
		return;
	}
	const auto i = _activeVideoTracks.find(endpoint);
	const auto changed = active
		? (i == end(_activeVideoTracks))
		: (i != end(_activeVideoTracks));
	if (!changed) {
		if (active) {
			markTrackPaused(endpoint, paused);
		}
		return;
	}
	auto shown = false;
	if (active) {
		const auto i = _activeVideoTracks.emplace(
			endpoint,
			std::make_unique<VideoTrack>(
				paused,
				_requireARGB32,
				endpoint.peer)).first;
		const auto track = &i->second->track;

		track->renderNextFrame(
		) | rpl::start_with_next([=] {
			const auto activeTrack = _activeVideoTracks[endpoint].get();
			const auto size = track->frameSize();
			if (size.isEmpty()) {
				track->markFrameShown();
			} else if (!activeTrack->shown) {
				activeTrack->shown = true;
				markTrackShown(endpoint, true);
			}
			activeTrack->trackSize = size;
		}, i->second->lifetime);

		const auto size = track->frameSize();
		i->second->trackSize = size;
		if (!size.isEmpty() || paused) {
			i->second->shown = true;
			shown = true;
		} else {
			track->stateValue(
			) | rpl::filter([=](Webrtc::VideoState state) {
				return (state == Webrtc::VideoState::Paused)
					&& !_activeVideoTracks[endpoint]->shown;
			}) | rpl::start_with_next([=] {
				_activeVideoTracks[endpoint]->shown = true;
				markTrackShown(endpoint, true);
			}, i->second->lifetime);
		}
		addVideoOutput(i->first.id, { track->sink() });
	} else {
		if (_videoEndpointLarge.current() == endpoint) {
			setVideoEndpointLarge({});
		}
		markTrackShown(endpoint, false);
		markTrackPaused(endpoint, false);
		_activeVideoTracks.erase(i);
	}
	updateRequestedVideoChannelsDelayed();
	_videoStreamActiveUpdates.fire({ endpoint, active });
	if (active) {
		markTrackShown(endpoint, shown);
		markTrackPaused(endpoint, paused);
	}
}

void GroupCall::markTrackShown(const VideoEndpoint &endpoint, bool shown) {
	const auto changed = shown
		? _shownVideoTracks.emplace(endpoint).second
		: _shownVideoTracks.remove(endpoint);
	if (!changed) {
		return;
	}
	_videoStreamShownUpdates.fire_copy({ endpoint, shown });
	if (shown && endpoint.type == VideoEndpointType::Screen) {
		crl::on_main(this, [=] {
			if (_shownVideoTracks.contains(endpoint)) {
				pinVideoEndpoint(endpoint);
			}
		});
	}
}

void GroupCall::markTrackPaused(const VideoEndpoint &endpoint, bool paused) {
	if (!endpoint) {
		return;
	}

	const auto i = _activeVideoTracks.find(endpoint);
	Assert(i != end(_activeVideoTracks));

	i->second->track.setState(paused
		? Webrtc::VideoState::Paused
		: Webrtc::VideoState::Active);
}

void GroupCall::rejoin() {
	rejoin(_joinAs);
}

void GroupCall::rejoinWithHash(const QString &hash) {
	if (!hash.isEmpty() && mutedByAdmin()) {
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
	} else if (_joinState.action != JoinAction::None) {
		return;
	}

	if (_joinAs != as) {
		toggleVideo(false);
		toggleScreenSharing(std::nullopt);
	}

	_joinState.action = JoinAction::Joining;
	_joinState.ssrc = 0;
	_initialMuteStateSent = false;
	setState(State::Joining);
	if (!tryCreateController()) {
		setInstanceMode(InstanceMode::None);
	}
	applyMeInCallLocally();
	LOG(("Call Info: Requesting join payload."));

	setJoinAs(as);

	const auto weak = base::make_weak(&_instanceGuard);
	_instance->emitJoinPayload([=](tgcalls::GroupJoinPayload payload) {
		crl::on_main(weak, [=, payload = std::move(payload)] {
			if (state() != State::Joining) {
				_joinState.finish();
				checkNextJoinAction();
				return;
			}
			const auto ssrc = payload.audioSsrc;
			LOG(("Call Info: Join payload received, joining with ssrc: %1."
				).arg(ssrc));

			const auto json = QByteArray::fromStdString(payload.json);
			const auto wasMuteState = muted();
			const auto wasVideoStopped = !isSharingCamera();
			using Flag = MTPphone_JoinGroupCall::Flag;
			const auto flags = (wasMuteState != MuteState::Active
				? Flag::f_muted
				: Flag(0))
				| (_joinHash.isEmpty()
					? Flag(0)
					: Flag::f_invite_hash)
				| (wasVideoStopped
					? Flag::f_video_stopped
					: Flag(0));
			_api.request(MTPphone_JoinGroupCall(
				MTP_flags(flags),
				inputCall(),
				_joinAs->input,
				MTP_string(_joinHash),
				MTP_dataJSON(MTP_bytes(json))
			)).done([=](const MTPUpdates &updates) {
				_joinState.finish(ssrc);
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
				_screenJoinState.nextActionPending = true;
				checkNextJoinAction();
				if (wasVideoStopped == isSharingCamera()) {
					sendSelfUpdate(SendUpdateType::CameraStopped);
				}
				if (isCameraPaused()) {
					sendSelfUpdate(SendUpdateType::CameraPaused);
				}
				sendPendingSelfUpdates();
			}).fail([=](const MTP::Error &error) {
				_joinState.finish();

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

void GroupCall::checkNextJoinAction() {
	if (_joinState.action != JoinAction::None) {
		return;
	} else if (_joinState.nextActionPending) {
		_joinState.nextActionPending = false;
		const auto state = _state.current();
		if (state != State::HangingUp && state != State::FailedHangingUp) {
			rejoin();
		} else {
			leave();
		}
	} else if (!_joinState.ssrc) {
		rejoin();
	} else if (_screenJoinState.action != JoinAction::None
		|| !_screenJoinState.nextActionPending) {
		return;
	} else {
		_screenJoinState.nextActionPending = false;
		if (isSharingScreen()) {
			rejoinPresentation();
		} else {
			leavePresentation();
		}
	}
}

void GroupCall::rejoinPresentation() {
	if (!_joinState.ssrc
		|| _screenJoinState.action == JoinAction::Joining
		|| !isSharingScreen()) {
		return;
	} else if (_screenJoinState.action != JoinAction::None) {
		_screenJoinState.nextActionPending = true;
		return;
	}

	_screenJoinState.action = JoinAction::Joining;
	_screenJoinState.ssrc = 0;
	if (!tryCreateScreencast()) {
		setScreenInstanceMode(InstanceMode::None);
	}
	LOG(("Call Info: Requesting join screen payload."));

	const auto weak = base::make_weak(&_screenInstanceGuard);
	_screenInstance->emitJoinPayload([=](tgcalls::GroupJoinPayload payload) {
		crl::on_main(weak, [=, payload = std::move(payload)]{
			if (!isSharingScreen() || !_joinState.ssrc) {
				_screenJoinState.finish();
				checkNextJoinAction();
				return;
			}
			const auto withMainSsrc = _joinState.ssrc;
			const auto ssrc = payload.audioSsrc;
			LOG(("Call Info: Join screen payload received, ssrc: %1."
				).arg(ssrc));

			const auto json = QByteArray::fromStdString(payload.json);
			_api.request(
				MTPphone_JoinGroupCallPresentation(
					inputCall(),
					MTP_dataJSON(MTP_bytes(json)))
			).done([=](const MTPUpdates &updates) {
				_screenJoinState.finish(ssrc);
				_mySsrcs.emplace(ssrc);

				_peer->session().api().applyUpdates(updates);
				checkNextJoinAction();
				if (isScreenPaused()) {
					sendSelfUpdate(SendUpdateType::ScreenPaused);
				}
				sendPendingSelfUpdates();
			}).fail([=](const MTP::Error &error) {
				_screenJoinState.finish();

				const auto type = error.type();
				if (type == u"GROUPCALL_SSRC_DUPLICATE_MUCH") {
					_screenJoinState.nextActionPending = true;
					checkNextJoinAction();
				} else if (type == u"GROUPCALL_JOIN_MISSING"_q
					|| type == u"GROUPCALL_FORBIDDEN"_q) {
					if (_joinState.ssrc != withMainSsrc) {
						// We've rejoined, rejoin presentation again.
						_screenJoinState.nextActionPending = true;
						checkNextJoinAction();
					}
				} else {
					LOG(("Call Error: "
						"Could not screen join, error: %1").arg(type));
					_screenState = Webrtc::VideoState::Inactive;
					_errors.fire_copy(mutedByAdmin()
						? Error::MutedNoScreen
						: Error::ScreenFailed);
				}
			}).send();
		});
	});
}

void GroupCall::leavePresentation() {
	destroyScreencast();
	if (!_screenJoinState.ssrc) {
		setScreenEndpoint(std::string());
		return;
	} else if (_screenJoinState.action == JoinAction::Leaving) {
		return;
	} else if (_screenJoinState.action != JoinAction::None) {
		_screenJoinState.nextActionPending = true;
		return;
	}
	_api.request(
		MTPphone_LeaveGroupCallPresentation(inputCall())
	).done([=](const MTPUpdates &updates) {
		_screenJoinState.finish();

		_peer->session().api().applyUpdates(updates);
		setScreenEndpoint(std::string());
		checkNextJoinAction();
	}).fail([=](const MTP::Error &error) {
		_screenJoinState.finish();

		const auto type = error.type();
		LOG(("Call Error: "
			"Could not screen leave, error: %1").arg(type));
		setScreenEndpoint(std::string());
		checkNextJoinAction();
	}).send();
}

void GroupCall::applyMeInCallLocally() {
	const auto real = lookupReal();
	if (!real) {
		return;
	}
	using Flag = MTPDgroupCallParticipant::Flag;
	const auto participant = real->participantByPeer(_joinAs);
	const auto date = participant
		? participant->date
		: base::unixtime::now();
	const auto lastActive = participant
		? participant->lastActive
		: TimeId(0);
	const auto volume = participant
		? participant->volume
		: Group::kDefaultVolume;
	const auto canSelfUnmute = !mutedByAdmin();
	const auto raisedHandRating = (muted() != MuteState::RaisedHand)
		? uint64(0)
		: participant
		? participant->raisedHandRating
		: FindLocalRaisedHandRating(real->participants());
	const auto flags = (canSelfUnmute ? Flag::f_can_self_unmute : Flag(0))
		| (lastActive ? Flag::f_active_date : Flag(0))
		| (_joinState.ssrc ? Flag(0) : Flag::f_left)
		| (_videoIsWorking.current() ? Flag::f_video_joined : Flag(0))
		| Flag::f_self
		| Flag::f_volume // Without flag the volume is reset to 100%.
		| Flag::f_volume_by_admin // Self volume can only be set by admin.
		| ((muted() != MuteState::Active) ? Flag::f_muted : Flag(0))
		| (raisedHandRating > 0 ? Flag::f_raise_hand_rating : Flag(0));
	real->applyLocalUpdate(
		MTP_updateGroupCallParticipants(
			inputCall(),
			MTP_vector<MTPGroupCallParticipant>(
				1,
				MTP_groupCallParticipant(
					MTP_flags(flags),
					peerToMTP(_joinAs->id),
					MTP_int(date),
					MTP_int(lastActive),
					MTP_int(_joinState.ssrc),
					MTP_int(volume),
					MTPstring(), // Don't update about text in local updates.
					MTP_long(raisedHandRating),
					MTPGroupCallParticipantVideo(),
					MTPGroupCallParticipantVideo())),
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
	const auto canManageCall = canManage();
	const auto isMuted = participant->muted || (mute && canManageCall);
	const auto canSelfUnmute = !canManageCall
		? participant->canSelfUnmute
		: (!mute || IsGroupCallAdmin(_peer, participantPeer));
	const auto isMutedByYou = mute && !canManageCall;
	using Flag = MTPDgroupCallParticipant::Flag;
	const auto flags = (canSelfUnmute ? Flag::f_can_self_unmute : Flag(0))
		| Flag::f_volume // Without flag the volume is reset to 100%.
		| ((participant->applyVolumeFromMin && !volume)
			? Flag::f_volume_by_admin
			: Flag(0))
		| (participant->videoJoined ? Flag::f_video_joined : Flag(0))
		| (participant->lastActive ? Flag::f_active_date : Flag(0))
		| (isMuted ? Flag::f_muted : Flag(0))
		| (isMutedByYou ? Flag::f_muted_by_you : Flag(0))
		| (participantPeer == _joinAs ? Flag::f_self : Flag(0))
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
					MTPGroupCallParticipantVideo(),
					MTPGroupCallParticipantVideo())),
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
	} else if (_joinState.action == JoinAction::None && !_joinState.ssrc) {
		setState(finalState);
		return;
	}
	setState(hangupState);
	_joinState.nextActionPending = true;
	checkNextJoinAction();
}

void GroupCall::leave() {
	Expects(_joinState.action == JoinAction::None);

	_joinState.action = JoinAction::Leaving;

	const auto finalState = (_state.current() == State::HangingUp)
		? State::Ended
		: State::Failed;

	// We want to leave request still being sent and processed even if
	// the call is already destroyed.
	const auto session = &_peer->session();
	const auto weak = base::make_weak(this);
	session->api().request(MTPphone_LeaveGroupCall(
		inputCall(),
		MTP_int(base::take(_joinState.ssrc))
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

void GroupCall::setNoiseSuppression(bool enabled) {
	if (_instance) {
		_instance->setIsNoiseSuppressionEnabled(enabled);
	}
}

void GroupCall::addVideoOutput(
		const std::string &endpoint,
		not_null<Webrtc::VideoTrack*> track) {
	addVideoOutput(endpoint, { track->sink() });
}

void GroupCall::setMuted(MuteState mute) {
	const auto set = [=] {
		const auto was = muted();
		const auto wasSpeaking = (was == MuteState::Active)
			|| (was == MuteState::PushToTalk);
		const auto wasMuted = (was == MuteState::Muted)
			|| (was == MuteState::PushToTalk);
		const auto wasRaiseHand = (was == MuteState::RaisedHand);
		_muted = mute;
		const auto now = muted();
		const auto nowSpeaking = (now == MuteState::Active)
			|| (now == MuteState::PushToTalk);
		const auto nowMuted = (now == MuteState::Muted)
			|| (now == MuteState::PushToTalk);
		const auto nowRaiseHand = (now == MuteState::RaisedHand);
		if (wasMuted != nowMuted || wasRaiseHand != nowRaiseHand) {
			applyMeInCallLocally();
		}
		if (mutedByAdmin()) {
			toggleVideo(false);
			toggleScreenSharing(std::nullopt);
		}
		if (wasSpeaking && !nowSpeaking && _joinState.ssrc) {
			_levelUpdates.fire(LevelUpdate{
				.ssrc = _joinState.ssrc,
				.value = 0.f,
				.voice = false,
				.me = true,
			});
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
		if (!_screenInstance) {
			return;
		}
		setScreenInstanceMode(InstanceMode::Rtc);
		data.vparams().match([&](const MTPDdataJSON &data) {
			const auto json = data.vdata().v;
			const auto response = ParseJoinResponse(json);
			const auto endpoint = std::get_if<JoinVideoEndpoint>(&response);
			if (endpoint) {
				setScreenEndpoint(endpoint->id);
			} else {
				LOG(("Call Error: Bad response for 'presentation' flag."));
			}
			_screenInstance->setJoinResponsePayload(json.toStdString());
		});
	} else {
		if (!_instance) {
			return;
		}
		data.vparams().match([&](const MTPDdataJSON &data) {
			const auto json = data.vdata().v;
			const auto response = ParseJoinResponse(json);
			const auto endpoint = std::get_if<JoinVideoEndpoint>(&response);
			if (v::is<JoinBroadcastStream>(response)) {
				if (!_broadcastDcId) {
					LOG(("Api Error: Empty stream_dc_id in groupCall."));
					_broadcastDcId = _peer->session().mtp().mainDcId();
				}
				setInstanceMode(InstanceMode::Stream);
			} else {
				setInstanceMode(InstanceMode::Rtc);
				setCameraEndpoint(endpoint ? endpoint->id : std::string());
				_instance->setJoinResponsePayload(json.toStdString());
			}
			updateRequestedVideoChannels();
			checkMediaChannelDescriptions();
		});
	}
}

void GroupCall::handlePossibleDiscarded(const MTPDgroupCallDiscarded &data) {
	if (data.vid().v == _id) {
		LOG(("Call Info: Hangup after groupCallDiscarded."));
		_joinState.finish();
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
		if (data.vsource().v == _joinState.ssrc) {
			// I was removed from the call, rejoin.
			LOG(("Call Info: "
				"Rejoin after got 'left' with my ssrc."));
			setState(State::Joining);
			rejoin();
		}
		return;
	} else if (data.vsource().v != _joinState.ssrc) {
		if (!_mySsrcs.contains(data.vsource().v)) {
			// I joined from another device, hangup.
			LOG(("Call Info: "
				"Hangup after '!left' with ssrc %1, my %2."
				).arg(data.vsource().v
				).arg(_joinState.ssrc));
			_joinState.finish();
			hangup();
		} else {
			LOG(("Call Info: "
				"Some old 'self' with '!left' and ssrc %1, my %2."
				).arg(data.vsource().v
				).arg(_joinState.ssrc));
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
	} else if (mutedByAdmin()) {
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

int GroupCall::activeVideoSendersCount() const {
	auto result = 0;
	for (const auto &[endpoint, track] : _activeVideoTracks) {
		if (endpoint.type == VideoEndpointType::Camera) {
			++result;
		} else {
			auto sharesCameraToo = false;
			for (const auto &[other, _] : _activeVideoTracks) {
				if (other.type == VideoEndpointType::Camera
					&& other.peer == endpoint.peer) {
					sharesCameraToo = true;
					break;
				}
			}
			if (!sharesCameraToo) {
				++result;
			}
		}
	}
	return result;
}

bool GroupCall::emitShareCameraError() {
	const auto emitError = [=](Error error) {
		emitShareCameraError(error);
		return true;
	};
	if (const auto real = lookupReal()
		; real && activeVideoSendersCount() >= real->unmutedVideoLimit()) {
		return emitError(Error::DisabledNoCamera);
	} else if (!videoIsWorking()) {
		return emitError(Error::DisabledNoCamera);
	} else if (mutedByAdmin()) {
		return emitError(Error::MutedNoCamera);
	} else if (Webrtc::GetVideoInputList().empty()) {
		return emitError(Error::NoCamera);
	}
	return false;
}

void GroupCall::emitShareCameraError(Error error) {
	_cameraState = Webrtc::VideoState::Inactive;
	if (error == Error::CameraFailed
		&& Webrtc::GetVideoInputList().empty()) {
		error = Error::NoCamera;
	}
	_errors.fire_copy(error);
}

bool GroupCall::emitShareScreenError() {
	const auto emitError = [=](Error error) {
		emitShareScreenError(error);
		return true;
	};
	if (const auto real = lookupReal()
		; real && activeVideoSendersCount() >= real->unmutedVideoLimit()) {
		return emitError(Error::DisabledNoScreen);
	} else if (!videoIsWorking()) {
		return emitError(Error::DisabledNoScreen);
	} else if (mutedByAdmin()) {
		return emitError(Error::MutedNoScreen);
	}
	return false;
}

void GroupCall::emitShareScreenError(Error error) {
	_screenState = Webrtc::VideoState::Inactive;
	_errors.fire_copy(error);
}

void GroupCall::setupOutgoingVideo() {
	using Webrtc::VideoState;

	_cameraState.value(
	) | rpl::combine_previous(
	) | rpl::filter([=](VideoState previous, VideoState state) {
		// Recursive entrance may happen if error happens when activating.
		return (previous != state);
	}) | rpl::start_with_next([=](VideoState previous, VideoState state) {
		const auto wasActive = (previous != VideoState::Inactive);
		const auto nowPaused = (state == VideoState::Paused);
		const auto nowActive = (state != VideoState::Inactive);
		if (wasActive == nowActive) {
			Assert(wasActive && nowActive);
			sendSelfUpdate(SendUpdateType::CameraPaused);
			markTrackPaused({
				VideoEndpointType::Camera,
				_joinAs,
				_cameraEndpoint
			}, nowPaused);
			return;
		}
		if (nowActive) {
			if (emitShareCameraError()) {
				return;
			} else if (!_cameraCapture) {
				_cameraCapture = _delegate->groupCallGetVideoCapture(
					_cameraInputId);
				if (!_cameraCapture) {
					return emitShareCameraError(Error::CameraFailed);
				}
				const auto weak = base::make_weak(this);
				_cameraCapture->setOnFatalError([=] {
					crl::on_main(weak, [=] {
						emitShareCameraError(Error::CameraFailed);
					});
				});
			} else {
				_cameraCapture->switchToDevice(_cameraInputId.toStdString());
			}
			if (_instance) {
				_instance->setVideoCapture(_cameraCapture);
			}
			_cameraCapture->setState(tgcalls::VideoState::Active);
		} else if (_cameraCapture) {
			_cameraCapture->setState(tgcalls::VideoState::Inactive);
		}
		_isSharingCamera = nowActive;
		markEndpointActive({
			VideoEndpointType::Camera,
			_joinAs,
			_cameraEndpoint
		}, nowActive, nowPaused);
		sendSelfUpdate(SendUpdateType::CameraStopped);
		applyMeInCallLocally();
	}, _lifetime);

	_screenState.value(
	) | rpl::combine_previous(
	) | rpl::filter([=](VideoState previous, VideoState state) {
		// Recursive entrance may happen if error happens when activating.
		return (previous != state);
	}) | rpl::start_with_next([=](VideoState previous, VideoState state) {
		const auto wasActive = (previous != VideoState::Inactive);
		const auto nowPaused = (state == VideoState::Paused);
		const auto nowActive = (state != VideoState::Inactive);
		if (wasActive == nowActive) {
			Assert(wasActive && nowActive);
			sendSelfUpdate(SendUpdateType::ScreenPaused);
			markTrackPaused({
				VideoEndpointType::Screen,
				_joinAs,
				_screenEndpoint
			}, nowPaused);
			return;
		}
		if (nowActive) {
			if (emitShareScreenError()) {
				return;
			} else if (!_screenCapture) {
				_screenCapture = std::shared_ptr<
					tgcalls::VideoCaptureInterface
				>(tgcalls::VideoCaptureInterface::Create(
					tgcalls::StaticThreads::getThreads(),
					_screenDeviceId.toStdString()));
				if (!_screenCapture) {
					return emitShareScreenError(Error::ScreenFailed);
				}
				const auto weak = base::make_weak(this);
				_screenCapture->setOnFatalError([=] {
					crl::on_main(weak, [=] {
						emitShareScreenError(Error::ScreenFailed);
					});
				});
				_screenCapture->setOnPause([=](bool paused) {
					crl::on_main(weak, [=] {
						if (isSharingScreen()) {
							_screenState = paused
								? VideoState::Paused
								: VideoState::Active;
						}
					});
				});
			} else {
				_screenCapture->switchToDevice(
					_screenDeviceId.toStdString());
			}
			if (_screenInstance) {
				_screenInstance->setVideoCapture(_screenCapture);
			}
			_screenCapture->setState(tgcalls::VideoState::Active);
		} else if (_screenCapture) {
			_screenCapture->setState(tgcalls::VideoState::Inactive);
		}
		_isSharingScreen = nowActive;
		markEndpointActive({
			VideoEndpointType::Screen,
			_joinAs,
			_screenEndpoint
		}, nowActive, nowPaused);
		_screenJoinState.nextActionPending = true;
		checkNextJoinAction();
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

bool GroupCall::tryCreateController() {
	if (_instance) {
		return false;
	}
	const auto &settings = Core::App().settings();

	const auto weak = base::make_weak(&_instanceGuard);
	const auto myLevel = std::make_shared<tgcalls::GroupLevelValue>();
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
		.requestBroadcastPart = [=, call = base::make_weak(this)](
				int64_t time,
				int64_t period,
				std::function<void(tgcalls::BroadcastPart &&)> done) {
			auto result = std::make_shared<LoadPartTask>(
				call,
				time,
				period,
				std::move(done));
			crl::on_main(weak, [=]() mutable {
				broadcastPartStart(std::move(result));
			});
			return result;
		},
		.videoContentType = tgcalls::VideoContentType::Generic,
		.initialEnableNoiseSuppression
			= settings.groupCallNoiseSuppression(),
		.requestMediaChannelDescriptions = [=, call = base::make_weak(this)](
			const std::vector<uint32_t> &ssrcs,
			std::function<void(
				std::vector<tgcalls::MediaChannelDescription> &&)> done) {
			auto result = std::make_shared<MediaChannelDescriptionsTask>(
				call,
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

	updateInstanceMuteState();
	updateInstanceVolumes();
	for (auto &[endpoint, sink] : base::take(_pendingVideoOutputs)) {
		_instance->addIncomingVideoOutput(endpoint, std::move(sink.data));
	}
	//raw->setAudioOutputDuckingEnabled(settings.callAudioDuckingEnabled());
	return true;
}

bool GroupCall::tryCreateScreencast() {
	if (_screenInstance) {
		return false;
	}

	const auto weak = base::make_weak(&_screenInstanceGuard);
	tgcalls::GroupInstanceDescriptor descriptor = {
		.threads = tgcalls::StaticThreads::getThreads(),
		.config = tgcalls::GroupConfig{
		},
		.networkStateUpdated = [=](tgcalls::GroupNetworkState networkState) {
			crl::on_main(weak, [=] {
				setScreenInstanceConnected(networkState);
			});
		},
		.createAudioDeviceModule = Webrtc::LoopbackAudioDeviceModuleCreator(),
		.videoCapture = _screenCapture,
		.videoContentType = tgcalls::VideoContentType::Screencast,
	};

	LOG(("Call Info: Creating group screen instance"));
	_screenInstance = std::make_unique<tgcalls::GroupInstanceCustomImpl>(
		std::move(descriptor));

	_screenInstance->setIsMuted(!_screenWithAudio);

	return true;
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
	for (const auto ssrc : task->ssrcs()) {
		const auto add = [&](
				std::optional<Channel> channel,
				bool screen = false) {
			if (task->finishWithAdding(ssrc, std::move(channel), screen)) {
				result = true;
			}
		};
		if (const auto byAudio = real->participantPeerByAudioSsrc(ssrc)) {
			add(Channel{
				.type = Channel::Type::Audio,
				.audioSsrc = ssrc,
			});
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

void GroupCall::updateRequestedVideoChannels() {
	_requestedVideoChannelsUpdateScheduled = false;
	const auto real = lookupReal();
	if (!real || !_instance) {
		return;
	}
	auto channels = std::vector<tgcalls::VideoChannelDescription>();
	using Quality = tgcalls::VideoChannelDescription::Quality;
	channels.reserve(_activeVideoTracks.size());
	const auto &camera = cameraSharingEndpoint();
	const auto &screen = screenSharingEndpoint();
	auto mediums = 0;
	auto fullcameras = 0;
	auto fullscreencasts = 0;
	for (const auto &[endpoint, video] : _activeVideoTracks) {
		const auto &endpointId = endpoint.id;
		if (endpointId == camera || endpointId == screen) {
			continue;
		}
		const auto participant = real->participantByEndpoint(endpointId);
		const auto params = (participant && participant->ssrc)
			? participant->videoParams.get()
			: nullptr;
		if (!params) {
			continue;
		}
		const auto min = (video->quality == Group::VideoQuality::Full
			&& endpoint.type == VideoEndpointType::Screen)
			? Quality::Full
			: Quality::Thumbnail;
		const auto max = (video->quality == Group::VideoQuality::Full)
			? Quality::Full
			: (video->quality == Group::VideoQuality::Medium
				&& endpoint.type != VideoEndpointType::Screen)
			? Quality::Medium
			: Quality::Thumbnail;
		if (max == Quality::Full) {
			if (endpoint.type == VideoEndpointType::Screen) {
				++fullscreencasts;
			} else {
				++fullcameras;
			}
		} else if (max == Quality::Medium) {
			++mediums;
		}
		channels.push_back({
			.audioSsrc = participant->ssrc,
			.endpointId = endpointId,
			.ssrcGroups = (params->camera.endpointId == endpointId
				? params->camera.ssrcGroups
				: params->screen.ssrcGroups),
			.minQuality = min,
			.maxQuality = max,
		});
	}

	// We limit `count(Full) * kFullAsMediumsCount + count(medium)`.
	//
	// Try to preserve all qualities; If not
	// Try to preserve all screencasts as Full and cameras as Medium; If not
	// Try to preserve all screencasts as Full; If not
	// Try to preserve all cameras as Medium;
	const auto mediumsCount = mediums
		+ (fullcameras + fullscreencasts) * kFullAsMediumsCount;
	const auto downgradeSome = (mediumsCount > kMaxMediumQualities);
	const auto downgradeAll = (fullscreencasts * kFullAsMediumsCount)
		> kMaxMediumQualities;
	if (downgradeSome) {
		for (auto &channel : channels) {
			if (channel.maxQuality == Quality::Full) {
				const auto camera = (channel.minQuality != Quality::Full);
				if (camera) {
					channel.maxQuality = Quality::Medium;
				} else if (downgradeAll) {
					channel.maxQuality
						= channel.minQuality
						= Quality::Thumbnail;
					--fullscreencasts;
				}
			}
		}
		mediums += fullcameras;
		fullcameras = 0;
		if (downgradeAll) {
			fullscreencasts = 0;
		}
	}
	if (mediums > kMaxMediumQualities) {
		for (auto &channel : channels) {
			if (channel.maxQuality == Quality::Medium) {
				channel.maxQuality = Quality::Thumbnail;
			}
		}
	}
	_instance->setRequestedVideoChannels(std::move(channels));
}

void GroupCall::updateRequestedVideoChannelsDelayed() {
	if (_requestedVideoChannelsUpdateScheduled) {
		return;
	}
	_requestedVideoChannelsUpdateScheduled = true;
	crl::on_main(this, [=] {
		if (_requestedVideoChannelsUpdateScheduled) {
			updateRequestedVideoChannels();
		}
	});
}

void GroupCall::refreshHasNotShownVideo() {
	if (!_joinState.ssrc || hasNotShownVideo()) {
		return;
	}
	const auto real = lookupReal();
	Assert(real != nullptr);

	const auto hasVideo = [&](const Data::GroupCallParticipant &data) {
		return (data.peer != _joinAs)
			&& (!GetCameraEndpoint(data.videoParams).empty()
				|| !GetScreenEndpoint(data.videoParams).empty());
	};
	_hasNotShownVideo = _joinState.ssrc
		&& ranges::any_of(real->participants(), hasVideo);
}

void GroupCall::fillActiveVideoEndpoints() {
	const auto real = lookupReal();
	Assert(real != nullptr);

	const auto me = real->participantByPeer(_joinAs);
	if (me && me->videoJoined) {
		_videoIsWorking = true;
		_hasNotShownVideo = false;
	} else {
		refreshHasNotShownVideo();
		_videoIsWorking = false;
		toggleVideo(false);
		toggleScreenSharing(std::nullopt);
	}

	const auto &large = _videoEndpointLarge.current();
	auto largeFound = false;
	auto endpoints = _activeVideoTracks | ranges::views::transform([](
			const auto &pair) {
		return pair.first;
	});
	auto removed = base::flat_set<VideoEndpoint>(
		begin(endpoints),
		end(endpoints));
	const auto feedOne = [&](VideoEndpoint endpoint, bool paused) {
		if (endpoint.empty()) {
			return;
		} else if (endpoint == large) {
			largeFound = true;
		}
		if (removed.remove(endpoint)) {
			markTrackPaused(endpoint, paused);
		} else {
			markEndpointActive(std::move(endpoint), true, paused);
		}
	};
	using Type = VideoEndpointType;
	if (_videoIsWorking.current()) {
		for (const auto &participant : real->participants()) {
			const auto camera = GetCameraEndpoint(participant.videoParams);
			if (camera != _cameraEndpoint
				&& camera != _screenEndpoint
				&& participant.peer != _joinAs) {
				const auto paused = IsCameraPaused(participant.videoParams);
				feedOne({ Type::Camera, participant.peer, camera }, paused);
			}
			const auto screen = GetScreenEndpoint(participant.videoParams);
			if (screen != _cameraEndpoint
				&& screen != _screenEndpoint
				&& participant.peer != _joinAs) {
				const auto paused = IsScreenPaused(participant.videoParams);
				feedOne({ Type::Screen, participant.peer, screen }, paused);
			}
		}
		feedOne(
			{ Type::Camera, _joinAs, cameraSharingEndpoint() },
			isCameraPaused());
		feedOne(
			{ Type::Screen, _joinAs, screenSharingEndpoint() },
			isScreenPaused());
	}
	if (large && !largeFound) {
		setVideoEndpointLarge({});
	}
	for (const auto &endpoint : removed) {
		markEndpointActive(endpoint, false, false);
	}
	updateRequestedVideoChannels();
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
		updateInstanceVolume(std::nullopt, participant);
	}
}

void GroupCall::updateInstanceVolume(
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant &now) {
	const auto nonDefault = now.mutedByMe
		|| (now.volume != Group::kDefaultVolume);
	const auto volumeChanged = was
		? (was->volume != now.volume || was->mutedByMe != now.mutedByMe)
		: nonDefault;
	const auto additionalSsrc = GetAdditionalAudioSsrc(now.videoParams);
	const auto set = now.ssrc
		&& (volumeChanged || (was && was->ssrc != now.ssrc));
	const auto additionalSet = additionalSsrc
		&& (volumeChanged
			|| (was && (GetAdditionalAudioSsrc(was->videoParams)
				!= additionalSsrc)));
	const auto localVolume = now.mutedByMe
		? 0.
		: (now.volume / float64(Group::kDefaultVolume));
	if (set) {
		_instance->setVolume(now.ssrc, localVolume);
	}
	if (additionalSet) {
		_instance->setVolume(additionalSsrc, localVolume);
	}
}

void GroupCall::audioLevelsUpdated(const tgcalls::GroupLevelsUpdate &data) {
	Expects(!data.updates.empty());

	auto check = false;
	auto checkNow = false;
	const auto now = crl::now();
	const auto meMuted = [&] {
		const auto state = muted();
		return (state != MuteState::Active)
			&& (state != MuteState::PushToTalk);
	};
	for (const auto &[ssrcOrZero, value] : data.updates) {
		const auto ssrc = ssrcOrZero ? ssrcOrZero : _joinState.ssrc;
		if (!ssrc) {
			continue;
		}
		const auto level = value.level;
		const auto voice = value.voice;
		const auto me = (ssrc == _joinState.ssrc);
		const auto ignore = me && meMuted();
		_levelUpdates.fire(LevelUpdate{
			.ssrc = ssrc,
			.value = ignore ? 0.f : level,
			.voice = (!ignore && voice),
			.me = me,
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

	constexpr auto kKeepInListFor = kCheckLastSpokeInterval * 2;
	static_assert(Data::GroupCall::kSoundStatusKeptFor
		<= kKeepInListFor - (kCheckLastSpokeInterval / 3));

	auto hasRecent = false;
	const auto now = crl::now();
	auto list = base::take(_lastSpoke);
	for (auto i = list.begin(); i != list.end();) {
		const auto [ssrc, when] = *i;
		if (when.anything + kKeepInListFor >= now) {
			hasRecent = true;
			++i;
		} else {
			i = list.erase(i);
		}

		// Ignore my levels from microphone if I'm already muted.
		if (ssrc != _joinState.ssrc
			|| muted() == MuteState::Active
			|| muted() == MuteState::PushToTalk) {
			real->applyLastSpoke(ssrc, when, now);
		}
	}
	_lastSpoke = std::move(list);

	if (!hasRecent) {
		_lastSpokeCheckTimer.cancel();
	} else if (!_lastSpokeCheckTimer.isActive()) {
		_lastSpokeCheckTimer.callEach(kCheckLastSpokeInterval / 3);
	}
}

void GroupCall::checkJoined() {
	if (state() != State::Connecting || !_id || !_joinState.ssrc) {
		return;
	}
	auto sources = QVector<MTPint>(1, MTP_int(_joinState.ssrc));
	if (_screenJoinState.ssrc) {
		sources.push_back(MTP_int(_screenJoinState.ssrc));
	}
	_api.request(MTPphone_CheckGroupCall(
		inputCall(),
		MTP_vector<MTPint>(std::move(sources))
	)).done([=](const MTPVector<MTPint> &result) {
		if (!ranges::contains(result.v, MTP_int(_joinState.ssrc))) {
			LOG(("Call Info: Rejoin after no my ssrc in checkGroupCall."));
			_joinState.nextActionPending = true;
			checkNextJoinAction();
		} else {
			if (state() == State::Connecting) {
				_checkJoinedTimer.callOnce(kCheckJoinedTimeout);
			}
			if (_screenJoinState.ssrc
				&& !ranges::contains(
					result.v,
					MTP_int(_screenJoinState.ssrc))) {
				LOG(("Call Info: "
					"Screen rejoin after _screenSsrc not found."));
				_screenJoinState.nextActionPending = true;
				checkNextJoinAction();
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
		switch (_screenInstanceMode) {
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

void GroupCall::sendPendingSelfUpdates() {
	if ((state() != State::Connecting && state() != State::Joined)
		|| _selfUpdateRequestId) {
		return;
	}
	const auto updates = {
		SendUpdateType::Mute,
		SendUpdateType::RaiseHand,
		SendUpdateType::CameraStopped,
		SendUpdateType::CameraPaused,
		SendUpdateType::ScreenPaused,
	};
	for (const auto type : updates) {
		if (type == SendUpdateType::ScreenPaused
			&& _screenJoinState.action != JoinAction::None) {
			continue;
		}
		if (_pendingSelfUpdates & type) {
			_pendingSelfUpdates &= ~type;
			sendSelfUpdate(type);
			return;
		}
	}
}

void GroupCall::sendSelfUpdate(SendUpdateType type) {
	if ((state() != State::Connecting && state() != State::Joined)
		|| _selfUpdateRequestId) {
		_pendingSelfUpdates |= type;
		return;
	}
	using Flag = MTPphone_EditGroupCallParticipant::Flag;
	_selfUpdateRequestId = _api.request(MTPphone_EditGroupCallParticipant(
		MTP_flags((type == SendUpdateType::RaiseHand)
			? Flag::f_raise_hand
			: (type == SendUpdateType::CameraStopped)
			? Flag::f_video_stopped
			: (type == SendUpdateType::CameraPaused)
			? Flag::f_video_paused
			: (type == SendUpdateType::ScreenPaused)
			? Flag::f_presentation_paused
			: Flag::f_muted),
		inputCall(),
		_joinAs->input,
		MTP_bool(muted() != MuteState::Active),
		MTP_int(100000), // volume
		MTP_bool(muted() == MuteState::RaisedHand),
		MTP_bool(!isSharingCamera()),
		MTP_bool(isCameraPaused()),
		MTP_bool(isScreenPaused())
	)).done([=](const MTPUpdates &result) {
		_selfUpdateRequestId = 0;
		_peer->session().api().applyUpdates(result);
		sendPendingSelfUpdates();
	}).fail([=](const MTP::Error &error) {
		_selfUpdateRequestId = 0;
		if (error.type() == u"GROUPCALL_FORBIDDEN"_q) {
			LOG(("Call Info: Rejoin after error '%1' in editGroupCallMember."
				).arg(error.type()));
			rejoin();
		}
	}).send();
}

void GroupCall::pinVideoEndpoint(VideoEndpoint endpoint) {
	_videoEndpointPinned = false;
	if (endpoint) {
		setVideoEndpointLarge(std::move(endpoint));
		_videoEndpointPinned = true;
	}
}

void GroupCall::showVideoEndpointLarge(VideoEndpoint endpoint) {
	if (_videoEndpointLarge.current() == endpoint) {
		return;
	}
	_videoEndpointPinned = false;
	setVideoEndpointLarge(std::move(endpoint));
	_videoLargeTillTime = crl::now() + kFixManualLargeVideoDuration;
}

void GroupCall::setVideoEndpointLarge(VideoEndpoint endpoint) {
	if (!endpoint) {
		_videoEndpointPinned = false;
	}
	_videoEndpointLarge = endpoint;
}

void GroupCall::requestVideoQuality(
		const VideoEndpoint &endpoint,
		Group::VideoQuality quality) {
	if (!endpoint) {
		return;
	}
	const auto i = _activeVideoTracks.find(endpoint);
	if (i == end(_activeVideoTracks) || i->second->quality == quality) {
		return;
	}
	i->second->quality = quality;
	updateRequestedVideoChannelsDelayed();
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
		MTPBool(), // video_muted
		MTPBool(), // video_paused
		MTPBool() // presentation_paused
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
	if (mutedByAdmin() || muted() == MuteState::Active) {
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

void GroupCall::setNotRequireARGB32() {
	_requireARGB32 = false;
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
		invalidate_weak_ptrs(&_instanceGuard);

		crl::async([
			instance = base::take(_instance),
			done = _delegate->groupCallAddAsyncWaiter()
		]() mutable {
			instance = nullptr;
			DEBUG_LOG(("Call Info: Call controller destroyed."));
			done();
		});
	}
}

void GroupCall::destroyScreencast() {
	if (_screenInstance) {
		DEBUG_LOG(("Call Info: Destroying call screen controller.."));
		invalidate_weak_ptrs(&_screenInstanceGuard);
		crl::async([
			instance = base::take(_screenInstance),
			done = _delegate->groupCallAddAsyncWaiter()
		]() mutable {
			instance = nullptr;
			DEBUG_LOG(("Call Info: Call screen controller destroyed."));
			done();
		});
	}
}

} // namespace Calls
