/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_call.h"

#include "apiwrap.h"
#include "base/openssl_help.h"
#include "base/platform/base_platform_info.h"
#include "base/random.h"
#include "boxes/abstract_box.h"
#include "calls/group/calls_group_common.h"
#include "calls/calls_instance.h"
#include "calls/calls_panel.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "media/audio/media_audio_track.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dh_utils.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/rate_call_box.h"
#include "webrtc/webrtc_create_adm.h"
#include "webrtc/webrtc_environment.h"
#include "webrtc/webrtc_video_track.h"
#include "window/window_controller.h"

#include <tgcalls/Instance.h>
#include <tgcalls/VideoCaptureInterface.h>
#include <tgcalls/StaticThreads.h>

namespace tgcalls {
class InstanceImpl;
class InstanceV2Impl;
class InstanceV2ReferenceImpl;
} // namespace tgcalls

namespace Calls {
namespace {

constexpr auto kMinLayer = 65;
constexpr auto kHangupTimeoutMs = 5000;
constexpr auto kSha256Size = 32;
constexpr auto kAuthKeySize = 256;
const auto kDefaultVersion = "2.4.4"_q;

const auto Register = tgcalls::Register<tgcalls::InstanceImpl>();
const auto RegisterV2 = tgcalls::Register<tgcalls::InstanceV2Impl>();
const auto RegV2Ref = tgcalls::Register<tgcalls::InstanceV2ReferenceImpl>();

[[nodiscard]] base::flat_set<int64> CollectEndpointIds(
		const QVector<MTPPhoneConnection> &list) {
	auto result = base::flat_set<int64>();
	result.reserve(list.size());
	for (const auto &connection : list) {
		connection.match([&](const MTPDphoneConnection &data) {
			result.emplace(int64(data.vid().v));
		}, [](const MTPDphoneConnectionWebrtc &) {
		});
	}
	return result;
}

void AppendEndpoint(
		std::vector<tgcalls::Endpoint> &list,
		const MTPPhoneConnection &connection) {
	connection.match([&](const MTPDphoneConnection &data) {
		if (data.vpeer_tag().v.length() != 16 || data.is_tcp()) {
			return;
		}
		tgcalls::Endpoint endpoint = {
			.endpointId = (int64_t)data.vid().v,
			.host = tgcalls::EndpointHost{
				.ipv4 = data.vip().v.toStdString(),
				.ipv6 = data.vipv6().v.toStdString() },
			.port = (uint16_t)data.vport().v,
			.type = tgcalls::EndpointType::UdpRelay,
		};
		const auto tag = data.vpeer_tag().v;
		if (tag.size() >= 16) {
			memcpy(endpoint.peerTag, tag.data(), 16);
		}
		list.push_back(std::move(endpoint));
	}, [&](const MTPDphoneConnectionWebrtc &data) {
	});
}

void AppendServer(
		std::vector<tgcalls::RtcServer> &list,
		const MTPPhoneConnection &connection,
		const base::flat_set<int64> &ids) {
	connection.match([&](const MTPDphoneConnection &data) {
		const auto hex = [](const QByteArray &value) {
			const auto digit = [](uchar c) {
				return char((c < 10) ? ('0' + c) : ('a' + c - 10));
			};
			auto result = std::string();
			result.reserve(value.size() * 2);
			for (const auto ch : value) {
				result += digit(uchar(ch) / 16);
				result += digit(uchar(ch) % 16);
			}
			return result;
		};
		const auto host = data.vip().v;
		const auto hostv6 = data.vipv6().v;
		const auto port = uint16_t(data.vport().v);
		const auto username = std::string("reflector");
		const auto password = hex(data.vpeer_tag().v);
		const auto i = ids.find(int64(data.vid().v));
		Assert(i != end(ids));
		const auto id = uint8_t((i - begin(ids)) + 1);
		const auto pushTurn = [&](const QString &host) {
			list.push_back(tgcalls::RtcServer{
				.id = id,
				.host = host.toStdString(),
				.port = port,
				.login = username,
				.password = password,
				.isTurn = true,
				.isTcp = data.is_tcp(),
			});
		};
		pushTurn(host);
		pushTurn(hostv6);
	}, [&](const MTPDphoneConnectionWebrtc &data) {
		const auto host = qs(data.vip());
		const auto hostv6 = qs(data.vipv6());
		const auto port = uint16_t(data.vport().v);
		if (data.is_stun()) {
			const auto pushStun = [&](const QString &host) {
				if (host.isEmpty()) {
					return;
				}
				list.push_back(tgcalls::RtcServer{
					.host = host.toStdString(),
					.port = port,
					.isTurn = false
				});
			};
			pushStun(host);
			pushStun(hostv6);
		}
		const auto username = qs(data.vusername());
		const auto password = qs(data.vpassword());
		if (data.is_turn() && !username.isEmpty() && !password.isEmpty()) {
			const auto pushTurn = [&](const QString &host) {
				list.push_back(tgcalls::RtcServer{
					.host = host.toStdString(),
					.port = port,
					.login = username.toStdString(),
					.password = password.toStdString(),
					.isTurn = true,
				});
			};
			pushTurn(host);
			pushTurn(hostv6);
		}
	});
}

constexpr auto kFingerprintDataSize = 256;
uint64 ComputeFingerprint(bytes::const_span authKey) {
	Expects(authKey.size() == kFingerprintDataSize);

	auto hash = openssl::Sha1(authKey);
	return (gsl::to_integer<uint64>(hash[19]) << 56)
		| (gsl::to_integer<uint64>(hash[18]) << 48)
		| (gsl::to_integer<uint64>(hash[17]) << 40)
		| (gsl::to_integer<uint64>(hash[16]) << 32)
		| (gsl::to_integer<uint64>(hash[15]) << 24)
		| (gsl::to_integer<uint64>(hash[14]) << 16)
		| (gsl::to_integer<uint64>(hash[13]) << 8)
		| (gsl::to_integer<uint64>(hash[12]));
}

[[nodiscard]] QVector<MTPstring> WrapVersions(
		const std::vector<std::string> &data) {
	return ranges::views::all(
		data
	) | ranges::views::transform([=](const std::string &string) {
		return MTP_string(string);
	}) | ranges::to<QVector<MTPstring>>;
}

[[nodiscard]] QVector<MTPstring> CollectVersionsForApi() {
	return WrapVersions(tgcalls::Meta::Versions() | ranges::actions::reverse);
}

[[nodiscard]] Webrtc::VideoState StartVideoState(bool enabled) {
	using State = Webrtc::VideoState;
	return enabled ? State::Active : State::Inactive;
}

} // namespace

Call::Call(
	not_null<Delegate*> delegate,
	not_null<UserData*> user,
	Type type,
	bool video)
: _delegate(delegate)
, _user(user)
, _api(&_user->session().mtp())
, _type(type)
, _discardByTimeoutTimer([=] { hangup(); })
, _playbackDeviceId(
	&Core::App().mediaDevices(),
	Webrtc::DeviceType::Playback,
	Webrtc::DeviceIdValueWithFallback(
		Core::App().settings().callPlaybackDeviceIdValue(),
		Core::App().settings().playbackDeviceIdValue()))
, _captureDeviceId(
	&Core::App().mediaDevices(),
	Webrtc::DeviceType::Capture,
	Webrtc::DeviceIdValueWithFallback(
		Core::App().settings().callCaptureDeviceIdValue(),
		Core::App().settings().captureDeviceIdValue()))
, _cameraDeviceId(
	&Core::App().mediaDevices(),
	Webrtc::DeviceType::Camera,
	Core::App().settings().cameraDeviceIdValue())
, _videoIncoming(
	std::make_unique<Webrtc::VideoTrack>(
		StartVideoState(video)))
, _videoOutgoing(
	std::make_unique<Webrtc::VideoTrack>(
		StartVideoState(video))) {
	if (_type == Type::Outgoing) {
		setState(State::WaitingUserConfirmation);
	} else {
		const auto &config = _user->session().serverConfig();
		_discardByTimeoutTimer.callOnce(config.callRingTimeoutMs);
		startWaitingTrack();
	}
	setupMediaDevices();
	setupOutgoingVideo();
}

Call::Call(
	not_null<Delegate*> delegate,
	not_null<UserData*> user,
	CallId conferenceId,
	MsgId conferenceInviteMsgId,
	std::vector<not_null<PeerData*>> conferenceParticipants,
	bool video)
: _delegate(delegate)
, _user(user)
, _api(&_user->session().mtp())
, _type(Type::Incoming)
, _state(State::WaitingIncoming)
, _discardByTimeoutTimer([=] { hangup(); })
, _playbackDeviceId(
	&Core::App().mediaDevices(),
	Webrtc::DeviceType::Playback,
	Webrtc::DeviceIdValueWithFallback(
		Core::App().settings().callPlaybackDeviceIdValue(),
		Core::App().settings().playbackDeviceIdValue()))
, _captureDeviceId(
	&Core::App().mediaDevices(),
	Webrtc::DeviceType::Capture,
	Webrtc::DeviceIdValueWithFallback(
		Core::App().settings().callCaptureDeviceIdValue(),
		Core::App().settings().captureDeviceIdValue()))
, _cameraDeviceId(
	&Core::App().mediaDevices(),
	Webrtc::DeviceType::Camera,
	Core::App().settings().cameraDeviceIdValue())
, _id(base::RandomValue<CallId>())
, _conferenceId(conferenceId)
, _conferenceInviteMsgId(conferenceInviteMsgId)
, _conferenceParticipants(std::move(conferenceParticipants))
, _videoIncoming(
	std::make_unique<Webrtc::VideoTrack>(
		StartVideoState(video)))
, _videoOutgoing(
	std::make_unique<Webrtc::VideoTrack>(
		StartVideoState(video))) {
	startWaitingTrack();
	setupOutgoingVideo();
}

void Call::generateModExpFirst(bytes::const_span randomSeed) {
	Expects(!conferenceInvite());

	auto first = MTP::CreateModExp(_dhConfig.g, _dhConfig.p, randomSeed);
	if (first.modexp.empty()) {
		LOG(("Call Error: Could not compute mod-exp first."));
		finish(FinishType::Failed);
		return;
	}

	_randomPower = std::move(first.randomPower);
	if (_type == Type::Incoming) {
		_gb = std::move(first.modexp);
	} else {
		_ga = std::move(first.modexp);
		_gaHash = openssl::Sha256(_ga);
	}
}

bool Call::isIncomingWaiting() const {
	if (type() != Call::Type::Incoming) {
		return false;
	}
	return (state() == State::Starting)
		|| (state() == State::WaitingIncoming);
}

void Call::start(bytes::const_span random) {
	Expects(!conferenceInvite());

	// Save config here, because it is possible that it changes between
	// different usages inside the same call.
	_dhConfig = _delegate->getDhConfig();
	Assert(_dhConfig.g != 0);
	Assert(!_dhConfig.p.empty());

	generateModExpFirst(random);
	const auto state = _state.current();
	if (state == State::Starting || state == State::Requesting) {
		if (_type == Type::Outgoing) {
			startOutgoing();
		} else {
			startIncoming();
		}
	} else if (state == State::ExchangingKeys
		&& _answerAfterDhConfigReceived) {
		answer();
	}
}

void Call::startOutgoing() {
	Expects(_type == Type::Outgoing);
	Expects(_state.current() == State::Requesting);
	Expects(_gaHash.size() == kSha256Size);
	Expects(!conferenceInvite());

	const auto flags = _videoCapture
		? MTPphone_RequestCall::Flag::f_video
		: MTPphone_RequestCall::Flag(0);
	_api.request(MTPphone_RequestCall(
		MTP_flags(flags),
		_user->inputUser,
		MTP_int(base::RandomValue<int32>()),
		MTP_bytes(_gaHash),
		MTP_phoneCallProtocol(
			MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p
				| MTPDphoneCallProtocol::Flag::f_udp_reflector),
			MTP_int(kMinLayer),
			MTP_int(tgcalls::Meta::MaxLayer()),
			MTP_vector(CollectVersionsForApi()))
	)).done([=](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);

		setState(State::Waiting);

		const auto &call = result.c_phone_phoneCall();
		_user->session().data().processUsers(call.vusers());
		if (call.vphone_call().type() != mtpc_phoneCallWaiting) {
			LOG(("Call Error: Expected phoneCallWaiting in response to "
				"phone.requestCall()"));
			finish(FinishType::Failed);
			return;
		}

		const auto &phoneCall = call.vphone_call();
		const auto &waitingCall = phoneCall.c_phoneCallWaiting();
		_id = waitingCall.vid().v;
		_accessHash = waitingCall.vaccess_hash().v;
		if (_finishAfterRequestingCall != FinishType::None) {
			if (_finishAfterRequestingCall == FinishType::Failed) {
				finish(_finishAfterRequestingCall);
			} else {
				hangup();
			}
			return;
		}

		const auto &config = _user->session().serverConfig();
		_discardByTimeoutTimer.callOnce(config.callReceiveTimeoutMs);
		handleUpdate(phoneCall);
	}).fail([this](const MTP::Error &error) {
		handleRequestError(error.type());
	}).send();
}

void Call::startIncoming() {
	Expects(_type == Type::Incoming);
	Expects(_state.current() == State::Starting);
	Expects(!conferenceInvite());

	_api.request(MTPphone_ReceivedCall(
		MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash))
	)).done([=] {
		if (_state.current() == State::Starting) {
			setState(State::WaitingIncoming);
		}
	}).fail([=](const MTP::Error &error) {
		handleRequestError(error.type());
	}).send();
}

void Call::applyUserConfirmation() {
	Expects(!conferenceInvite());

	if (_state.current() == State::WaitingUserConfirmation) {
		setState(State::Requesting);
	}
}

void Call::answer() {
	const auto video = isSharingVideo();
	_delegate->callRequestPermissionsOrFail(crl::guard(this, [=] {
		actuallyAnswer();
	}), video);
}

StartConferenceInfo Call::migrateConferenceInfo(StartConferenceInfo extend) {
	extend.migrating = true;
	extend.muted = muted();
	extend.videoCapture = isSharingVideo() ? _videoCapture : nullptr;
	extend.videoCaptureScreenId = screenSharingDeviceId();
	return extend;
}

void Call::acceptConferenceInvite() {
	Expects(conferenceInvite());

	if (_state.current() != State::WaitingIncoming) {
		return;
	}
	setState(State::ExchangingKeys);
	const auto limit = 5;
	const auto messageId = _conferenceInviteMsgId;
	_api.request(MTPphone_GetGroupCall(
		MTP_inputGroupCallInviteMessage(MTP_int(messageId.bare)),
		MTP_int(limit)
	)).done([=](const MTPphone_GroupCall &result) {
		result.data().vcall().match([&](const auto &data) {
			auto call = _user->owner().sharedConferenceCall(
				data.vid().v,
				data.vaccess_hash().v);
			call->processFullCall(result);
			Core::App().calls().startOrJoinConferenceCall(
				migrateConferenceInfo({
					.call = std::move(call),
					.joinMessageId = messageId,
				}));
		});
	}).fail([=](const MTP::Error &error) {
		handleRequestError(error.type());
	}).send();
}

void Call::actuallyAnswer() {
	Expects(_type == Type::Incoming);

	if (conferenceInvite()) {
		acceptConferenceInvite();
		return;
	}

	const auto state = _state.current();
	if (state != State::Starting && state != State::WaitingIncoming) {
		if (state != State::ExchangingKeys
			|| !_answerAfterDhConfigReceived) {
			return;
		}
	}
	setState(State::ExchangingKeys);
	if (_gb.empty()) {
		_answerAfterDhConfigReceived = true;
		return;
	} else {
		_answerAfterDhConfigReceived = false;
	}
	_api.request(MTPphone_AcceptCall(
		MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)),
		MTP_bytes(_gb),
		MTP_phoneCallProtocol(
			MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p
				| MTPDphoneCallProtocol::Flag::f_udp_reflector),
			MTP_int(kMinLayer),
			MTP_int(tgcalls::Meta::MaxLayer()),
			MTP_vector(CollectVersionsForApi()))
	)).done([=](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);

		const auto &call = result.c_phone_phoneCall();
		_user->session().data().processUsers(call.vusers());
		if (call.vphone_call().type() != mtpc_phoneCallWaiting) {
			LOG(("Call Error: "
				"Not phoneCallWaiting in response to phone.acceptCall."));
			finish(FinishType::Failed);
			return;
		}

		handleUpdate(call.vphone_call());
	}).fail([=](const MTP::Error &error) {
		handleRequestError(error.type());
	}).send();
}

void Call::captureMuteChanged(bool mute) {
	setMuted(mute);
}

rpl::producer<Webrtc::DeviceResolvedId> Call::captureMuteDeviceId() {
	return _captureDeviceId.value();
}

void Call::setMuted(bool mute) {
	_muted = mute;
	if (_instance) {
		_instance->setMuteMicrophone(mute);
	}
}

void Call::setupMediaDevices() {
	Expects(!conferenceInvite());

	_playbackDeviceId.changes() | rpl::filter([=] {
		return _instance && _setDeviceIdCallback;
	}) | rpl::start_with_next([=](const Webrtc::DeviceResolvedId &deviceId) {
		_setDeviceIdCallback(deviceId);

		// Value doesn't matter here, just trigger reading of the new value.
		_instance->setAudioOutputDevice(deviceId.value.toStdString());
	}, _lifetime);

	_captureDeviceId.changes() | rpl::filter([=] {
		return _instance && _setDeviceIdCallback;
	}) | rpl::start_with_next([=](const Webrtc::DeviceResolvedId &deviceId) {
		_setDeviceIdCallback(deviceId);

		// Value doesn't matter here, just trigger reading of the new value.
		_instance->setAudioInputDevice(deviceId.value.toStdString());
	}, _lifetime);
}

void Call::setupOutgoingVideo() {
	const auto cameraId = [] {
		return Core::App().mediaDevices().defaultId(
			Webrtc::DeviceType::Camera);
	};
	const auto started = _videoOutgoing->state();
	if (cameraId().isEmpty()) {
		_videoOutgoing->setState(Webrtc::VideoState::Inactive);
	}
	_videoOutgoing->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		if (state != Webrtc::VideoState::Inactive
			&& cameraId().isEmpty()
			&& !_videoCaptureIsScreencast) {
			_errors.fire({ ErrorType::NoCamera });
			_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		} else if (_state.current() != State::Established
			&& (state != Webrtc::VideoState::Inactive)
			&& (started == Webrtc::VideoState::Inactive)
			&& !conferenceInvite()) {
			_errors.fire({ ErrorType::NotStartedCall });
			_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		} else if (state != Webrtc::VideoState::Inactive
			&& _instance
			&& !_instance->supportsVideo()) {
			_errors.fire({ ErrorType::NotVideoCall });
			_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		} else if (state != Webrtc::VideoState::Inactive) {
			// Paused not supported right now.
			Assert(state == Webrtc::VideoState::Active);
			if (!_videoCapture) {
				_videoCapture = _delegate->callGetVideoCapture(
					_videoCaptureDeviceId,
					_videoCaptureIsScreencast);
				_videoCapture->setOutput(_videoOutgoing->sink());
			}
			_videoCapture->setState(tgcalls::VideoState::Active);
			if (_instance) {
				_instance->setVideoCapture(_videoCapture);
			}
		} else if (_videoCapture) {
			_videoCapture->setState(tgcalls::VideoState::Inactive);
			if (_instance) {
				_instance->setVideoCapture(nullptr);
			}
		}
	}, _lifetime);

	_cameraDeviceId.changes(
	) | rpl::filter([=] {
		return !_videoCaptureIsScreencast;
	}) | rpl::start_with_next([=](Webrtc::DeviceResolvedId deviceId) {
		const auto &id = deviceId.value;
		_videoCaptureDeviceId = id;
		if (_videoCapture) {
			_videoCapture->switchToDevice(id.toStdString(), false);
			if (_instance) {
				_instance->sendVideoDeviceUpdated();
			}
		}
	}, _lifetime);
}

not_null<Webrtc::VideoTrack*> Call::videoIncoming() const {
	return _videoIncoming.get();
}

not_null<Webrtc::VideoTrack*> Call::videoOutgoing() const {
	return _videoOutgoing.get();
}

crl::time Call::getDurationMs() const {
	return _startTime ? (crl::now() - _startTime) : 0;
}

void Call::hangup(Data::GroupCall *migrateCall, const QString &migrateSlug) {
	const auto state = _state.current();
	if (state == State::Busy
		|| state == State::MigrationHangingUp) {
		_delegate->callFinished(this);
	} else {
		const auto missed = (state == State::Ringing
			|| (state == State::Waiting && _type == Type::Outgoing));
		const auto declined = isIncomingWaiting();
		const auto reason = !migrateSlug.isEmpty()
			? MTP_phoneCallDiscardReasonMigrateConferenceCall(
				MTP_string(migrateSlug))
			: missed
			? MTP_phoneCallDiscardReasonMissed()
			: declined
			? MTP_phoneCallDiscardReasonBusy()
			: MTP_phoneCallDiscardReasonHangup();
		finish(FinishType::Ended, reason, migrateCall);
	}
}

void Call::redial() {
	Expects(!conferenceInvite());

	if (_state.current() != State::Busy) {
		return;
	}
	Assert(_instance == nullptr);
	_type = Type::Outgoing;
	setState(State::Requesting);
	_answerAfterDhConfigReceived = false;
	startWaitingTrack();
	_delegate->callRedial(this);
}

QString Call::getDebugLog() const {
	return _instance
		? QString::fromStdString(_instance->getDebugInfo())
		: QString();
}

void Call::startWaitingTrack() {
	_waitingTrack = Media::Audio::Current().createTrack();
	const auto trackFileName = Core::App().settings().getSoundPath(
		(_type == Type::Outgoing)
		? u"call_outgoing"_q
		: u"call_incoming"_q);
	_waitingTrack->samplePeakEach(kSoundSampleMs);
	_waitingTrack->fillFromFile(trackFileName);
	_waitingTrack->playInLoop();
}

void Call::sendSignalingData(const QByteArray &data) {
	Expects(!conferenceInvite());

	_api.request(MTPphone_SendSignalingData(
		MTP_inputPhoneCall(
			MTP_long(_id),
			MTP_long(_accessHash)),
		MTP_bytes(data)
	)).done([=](const MTPBool &result) {
		if (!mtpIsTrue(result)) {
			finish(FinishType::Failed);
		}
	}).fail([=](const MTP::Error &error) {
		handleRequestError(error.type());
	}).send();
}

float64 Call::getWaitingSoundPeakValue() const {
	if (_waitingTrack) {
		const auto when = crl::now() + kSoundSampleMs / 4;
		return _waitingTrack->getPeakValue(when);
	}
	return 0.;
}

bool Call::isKeyShaForFingerprintReady() const {
	return (_keyFingerprint != 0);
}

bytes::vector Call::getKeyShaForFingerprint() const {
	Expects(isKeyShaForFingerprintReady());
	Expects(!_ga.empty());

	auto encryptedChatAuthKey = bytes::vector(
		_authKey.size() + _ga.size(),
		gsl::byte{});
	bytes::copy(
		gsl::make_span(encryptedChatAuthKey).subspan(0, _authKey.size()),
		_authKey);
	bytes::copy(
		gsl::make_span(encryptedChatAuthKey).subspan(
			_authKey.size(),
			_ga.size()),
		_ga);
	return openssl::Sha256(encryptedChatAuthKey);
}

bool Call::handleUpdate(const MTPPhoneCall &call) {
	switch (call.type()) {
	case mtpc_phoneCallRequested: {
		const auto &data = call.c_phoneCallRequested();
		if (_type != Type::Incoming
			|| _id != 0
			|| peerToUser(_user->id) != UserId(data.vadmin_id())) {
			Unexpected("phoneCallRequested call inside an existing call "
				"handleUpdate()");
		}
		if (_user->session().userId() != UserId(data.vparticipant_id())) {
			LOG(("Call Error: Wrong call participant_id %1, expected %2."
				).arg(data.vparticipant_id().v
				).arg(_user->session().userId().bare));
			finish(FinishType::Failed);
			return true;
		}

		_id = data.vid().v;
		_accessHash = data.vaccess_hash().v;
		const auto gaHashBytes = bytes::make_span(data.vg_a_hash().v);
		if (gaHashBytes.size() != kSha256Size) {
			LOG(("Call Error: Wrong g_a_hash size %1, expected %2."
				).arg(gaHashBytes.size()
				).arg(kSha256Size));
			finish(FinishType::Failed);
			return true;
		}
		_gaHash = bytes::make_vector(gaHashBytes);
	} return true;

	case mtpc_phoneCallEmpty: {
		const auto &data = call.c_phoneCallEmpty();
		if (data.vid().v != _id) {
			return false;
		}
		LOG(("Call Error: phoneCallEmpty received."));
		finish(FinishType::Failed);
	} return true;

	case mtpc_phoneCallWaiting: {
		const auto &data = call.c_phoneCallWaiting();
		if (data.vid().v != _id) {
			return false;
		}
		if (_type == Type::Outgoing
			&& _state.current() == State::Waiting
			&& data.vreceive_date().value_or_empty() != 0) {
			const auto &config = _user->session().serverConfig();
			_discardByTimeoutTimer.callOnce(config.callRingTimeoutMs);
			setState(State::Ringing);
			startWaitingTrack();
		}
	} return true;

	case mtpc_phoneCall: {
		const auto &data = call.c_phoneCall();
		if (data.vid().v != _id) {
			return false;
		}
		if (_type == Type::Incoming
			&& _state.current() == State::ExchangingKeys
			&& !_instance) {
			startConfirmedCall(data);
		}
	} return true;

	case mtpc_phoneCallDiscarded: {
		const auto &data = call.c_phoneCallDiscarded();
		if (data.vid().v != _id) {
			return false;
		}
		if (data.is_need_debug()) {
			const auto debugLog = _instance
				? _instance->getDebugInfo()
				: std::string();
			if (!debugLog.empty()) {
				user()->session().api().request(MTPphone_SaveCallDebug(
					MTP_inputPhoneCall(
						MTP_long(_id),
						MTP_long(_accessHash)),
					MTP_dataJSON(MTP_string(debugLog))
				)).send();
			}
		}
		if (data.is_need_rating() && _id && _accessHash) {
			const auto window = Core::App().windowFor(
				::Window::SeparateId(_user));
			const auto session = &_user->session();
			const auto callId = _id;
			const auto callAccessHash = _accessHash;
			auto owned = Box<Ui::RateCallBox>(
				Core::App().settings().sendSubmitWay());
			const auto box = window
				? window->show(std::move(owned))
				: Ui::show(std::move(owned));
			const auto sender = box->lifetime().make_state<MTP::Sender>(
				&session->mtp());
			box->sends(
			) | rpl::take(
				1 // Instead of keeping requestId.
			) | rpl::start_with_next([=](const Ui::RateCallBox::Result &r) {
				sender->request(MTPphone_SetCallRating(
					MTP_flags(0),
					MTP_inputPhoneCall(
						MTP_long(callId),
						MTP_long(callAccessHash)),
					MTP_int(r.rating),
					MTP_string(r.comment)
				)).done([=](const MTPUpdates &updates) {
					session->api().applyUpdates(updates);
					box->closeBox();
				}).fail([=] {
					box->closeBox();
				}).send();
			}, box->lifetime());
		}
		const auto reason = data.vreason();
		if (reason
			&& reason->type() == mtpc_phoneCallDiscardReasonDisconnect) {
			LOG(("Call Info: Discarded with DISCONNECT reason."));
		}
		if (reason && reason->type() == mtpc_phoneCallDiscardReasonMigrateConferenceCall) {
			const auto slug = qs(reason->c_phoneCallDiscardReasonMigrateConferenceCall().vslug());
			finishByMigration(slug);
		} else if (reason && reason->type() == mtpc_phoneCallDiscardReasonBusy) {
			setState(State::Busy);
		} else if (_type == Type::Outgoing
			|| _state.current() == State::HangingUp) {
			setState(State::Ended);
		} else {
			setState(State::EndedByOtherDevice);
		}
	} return true;

	case mtpc_phoneCallAccepted: {
		const auto &data = call.c_phoneCallAccepted();
		if (data.vid().v != _id) {
			return false;
		}
		if (_type != Type::Outgoing) {
			LOG(("Call Error: "
				"Unexpected phoneCallAccepted for an incoming call."));
			finish(FinishType::Failed);
		} else if (checkCallFields(data)) {
			confirmAcceptedCall(data);
		}
	} return true;
	}

	Unexpected("phoneCall type inside an existing call handleUpdate()");
}

void Call::finishByMigration(const QString &slug) {
	Expects(!conferenceInvite());

	if (_state.current() == State::MigrationHangingUp) {
		return;
	}
	setState(State::MigrationHangingUp);
	const auto limit = 5;
	const auto session = &_user->session();
	session->api().request(MTPphone_GetGroupCall(
		MTP_inputGroupCallSlug(MTP_string(slug)),
		MTP_int(limit)
	)).done([=](const MTPphone_GroupCall &result) {
		result.data().vcall().match([&](const auto &data) {
			const auto call = session->data().sharedConferenceCall(
				data.vid().v,
				data.vaccess_hash().v);
			call->processFullCall(result);
			Core::App().calls().startOrJoinConferenceCall(
				migrateConferenceInfo({
					.call = call,
					.linkSlug = slug,
				}));
		});
	}).fail(crl::guard(this, [=] {
		setState(State::Failed);
	})).send();
}

void Call::updateRemoteMediaState(
		tgcalls::AudioState audio,
		tgcalls::VideoState video) {
	_remoteAudioState = [&] {
		using From = tgcalls::AudioState;
		using To = RemoteAudioState;
		switch (audio) {
		case From::Active: return To::Active;
		case From::Muted: return To::Muted;
		}
		Unexpected("Audio state in remoteMediaStateUpdated.");
	}();
	_videoIncoming->setState([&] {
		using From = tgcalls::VideoState;
		using To = Webrtc::VideoState;
		switch (video) {
		case From::Inactive: return To::Inactive;
		case From::Paused: return To::Paused;
		case From::Active: return To::Active;
		}
		Unexpected("Video state in remoteMediaStateUpdated.");
	}());
}

bool Call::handleSignalingData(
		const MTPDupdatePhoneCallSignalingData &data) {
	if (data.vphone_call_id().v != _id || !_instance) {
		return false;
	}
	auto prepared = ranges::views::all(
		data.vdata().v
	) | ranges::views::transform([](char byte) {
		return static_cast<uint8_t>(byte);
	}) | ranges::to_vector;
	_instance->receiveSignalingData(std::move(prepared));
	return true;
}

void Call::confirmAcceptedCall(const MTPDphoneCallAccepted &call) {
	Expects(_type == Type::Outgoing);
	Expects(!conferenceInvite());

	if (_state.current() == State::ExchangingKeys
		|| _instance) {
		LOG(("Call Warning: Unexpected confirmAcceptedCall."));
		return;
	}

	const auto firstBytes = bytes::make_span(call.vg_b().v);
	const auto computedAuthKey = MTP::CreateAuthKey(
		firstBytes,
		_randomPower,
		_dhConfig.p);
	if (computedAuthKey.empty()) {
		LOG(("Call Error: Could not compute mod-exp final."));
		finish(FinishType::Failed);
		return;
	}

	MTP::AuthKey::FillData(_authKey, computedAuthKey);
	_keyFingerprint = ComputeFingerprint(_authKey);

	setState(State::ExchangingKeys);
	_api.request(MTPphone_ConfirmCall(
		MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)),
		MTP_bytes(_ga),
		MTP_long(_keyFingerprint),
		MTP_phoneCallProtocol(
			MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p
				| MTPDphoneCallProtocol::Flag::f_udp_reflector),
			MTP_int(kMinLayer),
			MTP_int(tgcalls::Meta::MaxLayer()),
			MTP_vector(CollectVersionsForApi()))
	)).done([=](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);

		const auto &call = result.c_phone_phoneCall();
		_user->session().data().processUsers(call.vusers());
		if (call.vphone_call().type() != mtpc_phoneCall) {
			LOG(("Call Error: Expected phoneCall in response to "
				"phone.confirmCall()"));
			finish(FinishType::Failed);
			return;
		}

		createAndStartController(call.vphone_call().c_phoneCall());
	}).fail([=](const MTP::Error &error) {
		handleRequestError(error.type());
	}).send();
}

void Call::startConfirmedCall(const MTPDphoneCall &call) {
	Expects(_type == Type::Incoming);
	Expects(!conferenceInvite());

	const auto firstBytes = bytes::make_span(call.vg_a_or_b().v);
	if (_gaHash != openssl::Sha256(firstBytes)) {
		LOG(("Call Error: Wrong g_a hash received."));
		finish(FinishType::Failed);
		return;
	}
	_ga = bytes::vector(firstBytes.begin(), firstBytes.end());

	const auto computedAuthKey = MTP::CreateAuthKey(
		firstBytes,
		_randomPower,
		_dhConfig.p);
	if (computedAuthKey.empty()) {
		LOG(("Call Error: Could not compute mod-exp final."));
		finish(FinishType::Failed);
		return;
	}

	MTP::AuthKey::FillData(_authKey, computedAuthKey);
	_keyFingerprint = ComputeFingerprint(_authKey);

	createAndStartController(call);
}

void Call::createAndStartController(const MTPDphoneCall &call) {
	Expects(!conferenceInvite());

	_discardByTimeoutTimer.cancel();
	if (!checkCallFields(call) || _authKey.size() != kAuthKeySize) {
		return;
	}

	_conferenceSupported = call.is_conference_supported();

	const auto &protocol = call.vprotocol().c_phoneCallProtocol();
	const auto &serverConfig = _user->session().serverConfig();

	auto encryptionKeyValue = std::make_shared<std::array<
		uint8_t,
		kAuthKeySize>>();
	memcpy(encryptionKeyValue->data(), _authKey.data(), kAuthKeySize);

	const auto version = call.vprotocol().match([&](
			const MTPDphoneCallProtocol &data) {
		return data.vlibrary_versions().v;
	}).value(0, MTP_bytes(kDefaultVersion)).v;

	LOG(("Call Info: Creating instance with version '%1', allowP2P: %2").arg(
		QString::fromUtf8(version),
		Logs::b(call.is_p2p_allowed())));

	const auto versionString = version.toStdString();
	const auto &settings = Core::App().settings();
	const auto weak = base::make_weak(this);

	_setDeviceIdCallback = nullptr;
	const auto playbackDeviceIdInitial = _playbackDeviceId.current();
	const auto captureDeviceIdInitial = _captureDeviceId.current();
	const auto saveSetDeviceIdCallback = [=](
			Fn<void(Webrtc::DeviceResolvedId)> setDeviceIdCallback) {
		setDeviceIdCallback(playbackDeviceIdInitial);
		setDeviceIdCallback(captureDeviceIdInitial);
		crl::on_main(weak, [=] {
			_setDeviceIdCallback = std::move(setDeviceIdCallback);
			const auto playback = _playbackDeviceId.current();
			if (_instance && playback != playbackDeviceIdInitial) {
				_setDeviceIdCallback(playback);

				// Value doesn't matter here, just trigger reading of the...
				_instance->setAudioOutputDevice(
					playback.value.toStdString());
			}
			const auto capture = _captureDeviceId.current();
			if (_instance && capture != captureDeviceIdInitial) {
				_setDeviceIdCallback(capture);

				// Value doesn't matter here, just trigger reading of the...
				_instance->setAudioInputDevice(capture.value.toStdString());
			}
		});
	};

	tgcalls::Descriptor descriptor = {
		.version = versionString,
		.config = tgcalls::Config{
			.initializationTimeout
				= serverConfig.callConnectTimeoutMs / 1000.,
			.receiveTimeout = serverConfig.callPacketTimeoutMs / 1000.,
			.dataSaving = tgcalls::DataSaving::Never,
			.enableP2P = call.is_p2p_allowed(),
			.enableAEC = false,
			.enableNS = true,
			.enableAGC = true,
			.enableVolumeControl = true,
			.maxApiLayer = protocol.vmax_layer().v,
		},
		.encryptionKey = tgcalls::EncryptionKey(
			std::move(encryptionKeyValue),
			(_type == Type::Outgoing)),
		.mediaDevicesConfig = tgcalls::MediaDevicesConfig{
			.audioInputId = captureDeviceIdInitial.value.toStdString(),
			.audioOutputId = playbackDeviceIdInitial.value.toStdString(),
			.inputVolume = 1.f,//settings.callInputVolume() / 100.f,
			.outputVolume = 1.f,//settings.callOutputVolume() / 100.f,
		},
		.videoCapture = _videoCapture,
		.stateUpdated = [=](tgcalls::State state) {
			crl::on_main(weak, [=] {
				handleControllerStateChange(state);
			});
		},
		.signalBarsUpdated = [=](int count) {
			crl::on_main(weak, [=] {
				handleControllerBarCountChange(count);
			});
		},
		.remoteBatteryLevelIsLowUpdated = [=](bool isLow) {
#ifdef _DEBUG
//			isLow = true;
#endif
			crl::on_main(weak, [=] {
				_remoteBatteryState = isLow
					? RemoteBatteryState::Low
					: RemoteBatteryState::Normal;
			});
		},
		.remoteMediaStateUpdated = [=](
				tgcalls::AudioState audio,
				tgcalls::VideoState video) {
			crl::on_main(weak, [=] {
				updateRemoteMediaState(audio, video);
			});
		},
		.signalingDataEmitted = [=](const std::vector<uint8_t> &data) {
			const auto bytes = QByteArray(
				reinterpret_cast<const char*>(data.data()),
				data.size());
			crl::on_main(weak, [=] {
				sendSignalingData(bytes);
			});
		},
		.createAudioDeviceModule = Webrtc::AudioDeviceModuleCreator(
			saveSetDeviceIdCallback),
	};
	if (Logs::DebugEnabled()) {
		const auto callLogFolder = cWorkingDir() + u"DebugLogs"_q;
		const auto callLogPath = callLogFolder + u"/last_call_log.txt"_q;
		const auto callLogNative = QDir::toNativeSeparators(callLogPath);
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

	const auto ids = CollectEndpointIds(call.vconnections().v);
	for (const auto &connection : call.vconnections().v) {
		AppendEndpoint(descriptor.endpoints, connection);
	}
	for (const auto &connection : call.vconnections().v) {
		AppendServer(descriptor.rtcServers, connection, ids);
	}

	{
		const auto &settingsProxy = Core::App().settings().proxy();
		using ProxyData = MTP::ProxyData;
		if (settingsProxy.useProxyForCalls() && settingsProxy.isEnabled()) {
			const auto &selected = settingsProxy.selected();
			if (selected.supportsCalls() && !selected.host.isEmpty()) {
				Assert(selected.type == ProxyData::Type::Socks5);
				descriptor.proxy = std::make_unique<tgcalls::Proxy>();
				descriptor.proxy->host = selected.host.toStdString();
				descriptor.proxy->port = selected.port;
				descriptor.proxy->login = selected.user.toStdString();
				descriptor.proxy->password = selected.password.toStdString();
			}
		}
	}
	_instance = tgcalls::Meta::Create(versionString, std::move(descriptor));
	if (!_instance) {
		LOG(("Call Error: Wrong library version: %1."
			).arg(QString::fromUtf8(version)));
		finish(FinishType::Failed);
		return;
	}

	const auto raw = _instance.get();
	if (_muted.current()) {
		raw->setMuteMicrophone(_muted.current());
	}

	raw->setIncomingVideoOutput(_videoIncoming->sink());
	raw->setAudioOutputDuckingEnabled(settings.callAudioDuckingEnabled());

	_state.value() | rpl::start_with_next([=](State state) {
		const auto track = (state != State::FailedHangingUp)
			&& (state != State::Failed)
			&& (state != State::HangingUp)
			&& (state != State::MigrationHangingUp)
			&& (state != State::Ended)
			&& (state != State::EndedByOtherDevice)
			&& (state != State::Busy);
		Core::App().mediaDevices().setCaptureMuteTracker(this, track);
	}, _instanceLifetime);

	_muted.value() | rpl::start_with_next([=](bool muted) {
		Core::App().mediaDevices().setCaptureMuted(muted);
	}, _instanceLifetime);

#if 0
	Core::App().batterySaving().value(
	) | rpl::start_with_next([=](bool isSaving) {
		crl::on_main(weak, [=] {
			if (_instance) {
				_instance->setIsLowBatteryLevel(isSaving);
			}
		});
	}, _instanceLifetime);
#endif
}

void Call::handleControllerStateChange(tgcalls::State state) {
	Expects(!conferenceInvite());

	switch (state) {
	case tgcalls::State::WaitInit: {
		DEBUG_LOG(("Call Info: State changed to WaitingInit."));
		setState(State::WaitingInit);
	} break;

	case tgcalls::State::WaitInitAck: {
		DEBUG_LOG(("Call Info: State changed to WaitingInitAck."));
		setState(State::WaitingInitAck);
	} break;

	case tgcalls::State::Established: {
		DEBUG_LOG(("Call Info: State changed to Established."));
		setState(State::Established);
	} break;

	case tgcalls::State::Failed: {
		const auto error = _instance
			? QString::fromStdString(_instance->getLastError())
			: QString();
		LOG(("Call Info: State changed to Failed, error: %1.").arg(error));
		handleControllerError(error);
	} break;

	default: LOG(("Call Error: Unexpected state in handleStateChange: %1"
		).arg(int(state)));
	}
}

void Call::handleControllerBarCountChange(int count) {
	setSignalBarCount(count);
}

void Call::setSignalBarCount(int count) {
	_signalBarCount = count;
}

template <typename T>
bool Call::checkCallCommonFields(const T &call) {
	const auto checkFailed = [this] {
		finish(FinishType::Failed);
		return false;
	};
	if (call.vaccess_hash().v != _accessHash) {
		LOG(("Call Error: Wrong call access_hash."));
		return checkFailed();
	}
	const auto adminId = (_type == Type::Outgoing)
		? _user->session().userId()
		: peerToUser(_user->id);
	const auto participantId = (_type == Type::Outgoing)
		? peerToUser(_user->id)
		: _user->session().userId();
	if (UserId(call.vadmin_id()) != adminId) {
		LOG(("Call Error: Wrong call admin_id %1, expected %2.")
			.arg(call.vadmin_id().v)
			.arg(adminId.bare));
		return checkFailed();
	}
	if (UserId(call.vparticipant_id()) != participantId) {
		LOG(("Call Error: Wrong call participant_id %1, expected %2.")
			.arg(call.vparticipant_id().v)
			.arg(participantId.bare));
		return checkFailed();
	}
	return true;
}

bool Call::checkCallFields(const MTPDphoneCall &call) {
	if (!checkCallCommonFields(call)) {
		return false;
	}
	if (call.vkey_fingerprint().v != _keyFingerprint) {
		LOG(("Call Error: Wrong call fingerprint."));
		finish(FinishType::Failed);
		return false;
	}
	return true;
}

bool Call::checkCallFields(const MTPDphoneCallAccepted &call) {
	return checkCallCommonFields(call);
}

void Call::setState(State state) {
	const auto was = _state.current();
	if (was == State::Failed) {
		return;
	}
	if (was == State::FailedHangingUp
		&& state != State::Failed) {
		return;
	}
	if (was == State::MigrationHangingUp
		&& state != State::Ended
		&& state != State::Failed) {
		return;
	}
	if (was != state) {
		_state = state;

		if (true
			&& state != State::Starting
			&& state != State::Requesting
			&& state != State::Waiting
			&& state != State::WaitingIncoming
			&& state != State::Ringing) {
			_waitingTrack.reset();
		}
		if (false
			|| state == State::Ended
			|| state == State::EndedByOtherDevice
			|| state == State::Failed
			|| state == State::Busy) {
			// Destroy controller before destroying Call Panel,
			// so that the panel hide animation is smooth.
			destroyController();
		}
		switch (state) {
		case State::Established:
			_startTime = crl::now();
			break;
		case State::ExchangingKeys:
			_delegate->callPlaySound(Delegate::CallSound::Connecting);
			break;
		case State::Ended:
			if (was != State::WaitingUserConfirmation) {
				_delegate->callPlaySound(Delegate::CallSound::Ended);
			}
			[[fallthrough]];
		case State::EndedByOtherDevice:
			_delegate->callFinished(this);
			break;
		case State::Failed:
			_delegate->callPlaySound(Delegate::CallSound::Ended);
			_delegate->callFailed(this);
			break;
		case State::Busy:
			_delegate->callPlaySound(Delegate::CallSound::Busy);
			_discardByTimeoutTimer.cancel();
			break;
		}
	}
}

//void Call::setAudioVolume(bool input, float level) {
//	if (_instance) {
//		if (input) {
//			_instance->setInputVolume(level);
//		} else {
//			_instance->setOutputVolume(level);
//		}
//	}
//}

void Call::setAudioDuckingEnabled(bool enabled) {
	if (_instance) {
		_instance->setAudioOutputDuckingEnabled(enabled);
	}
}

bool Call::isSharingVideo() const {
	return (_videoOutgoing->state() != Webrtc::VideoState::Inactive);
}

bool Call::isSharingCamera() const {
	return !_videoCaptureIsScreencast && isSharingVideo();
}

bool Call::isSharingScreen() const {
	return _videoCaptureIsScreencast && isSharingVideo();
}

QString Call::cameraSharingDeviceId() const {
	return isSharingCamera() ? _videoCaptureDeviceId : QString();
}

QString Call::screenSharingDeviceId() const {
	return isSharingScreen() ? _videoCaptureDeviceId : QString();
}

void Call::toggleCameraSharing(bool enabled) {
	if (isSharingCamera() == enabled) {
		return;
	} else if (!enabled) {
		if (_videoCapture) {
			_videoCapture->setState(tgcalls::VideoState::Inactive);
		}
		_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		_videoCaptureDeviceId = QString();
		return;
	}
	_delegate->callRequestPermissionsOrFail(crl::guard(this, [=] {
		toggleScreenSharing(std::nullopt);
		_videoCaptureDeviceId = _cameraDeviceId.current().value;
		if (_videoCapture) {
			_videoCapture->switchToDevice(
				_videoCaptureDeviceId.toStdString(),
				false);
			if (_instance) {
				_instance->sendVideoDeviceUpdated();
			}
		}
		_videoOutgoing->setState(Webrtc::VideoState::Active);
	}), true);
}

void Call::toggleScreenSharing(std::optional<QString> uniqueId) {
	if (!uniqueId) {
		if (isSharingScreen()) {
			if (_videoCapture) {
				_videoCapture->setState(tgcalls::VideoState::Inactive);
			}
			_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		}
		_videoCaptureDeviceId = QString();
		_videoCaptureIsScreencast = false;
		return;
	} else if (screenSharingDeviceId() == *uniqueId) {
		return;
	}
	toggleCameraSharing(false);
	_videoCaptureIsScreencast = true;
	_videoCaptureDeviceId = *uniqueId;
	if (_videoCapture) {
		_videoCapture->switchToDevice(uniqueId->toStdString(), true);
		if (_instance) {
			_instance->sendVideoDeviceUpdated();
		}
	}
	_videoOutgoing->setState(Webrtc::VideoState::Active);
}

auto Call::peekVideoCapture() const
-> std::shared_ptr<tgcalls::VideoCaptureInterface> {
	return _videoCapture;
}

auto Call::playbackDeviceIdValue() const
-> rpl::producer<Webrtc::DeviceResolvedId> {
	return _playbackDeviceId.value();
}

rpl::producer<Webrtc::DeviceResolvedId> Call::captureDeviceIdValue() const {
	return _captureDeviceId.value();
}

rpl::producer<Webrtc::DeviceResolvedId> Call::cameraDeviceIdValue() const {
	return _cameraDeviceId.value();
}

void Call::finish(
		FinishType type,
		const MTPPhoneCallDiscardReason &reason,
		Data::GroupCall *migrateCall) {
	Expects(type != FinishType::None);

	setSignalBarCount(kSignalBarFinished);

	const auto finalState = (type == FinishType::Ended)
		? State::Ended
		: State::Failed;
	const auto hangupState = (type == FinishType::Ended)
		? State::HangingUp
		: State::FailedHangingUp;
	const auto state = _state.current();
	if (state == State::Requesting) {
		_finishByTimeoutTimer.call(kHangupTimeoutMs, [this, finalState] {
			setState(finalState);
		});
		_finishAfterRequestingCall = type;
		return;
	}
	if (state == State::HangingUp
		|| state == State::FailedHangingUp
		|| state == State::EndedByOtherDevice
		|| state == State::Ended
		|| state == State::Failed) {
		return;
	} else if (conferenceInvite()) {
		if (migrateCall) {
			_delegate->callFinished(this);
		} else {
			Core::App().calls().declineIncomingConferenceInvites(_conferenceId);
			setState(finalState);
		}
		return;
	} else if (!_id) {
		setState(finalState);
		return;
	}

	setState(hangupState);
	const auto duration = getDurationMs() / 1000;
	const auto connectionId = _instance
		? _instance->getPreferredRelayId()
		: 0;
	_finishByTimeoutTimer.call(kHangupTimeoutMs, [this, finalState] {
		setState(finalState);
	});

	using Video = Webrtc::VideoState;
	const auto flags = ((_videoIncoming->state() != Video::Inactive)
		|| (_videoOutgoing->state() != Video::Inactive))
		? MTPphone_DiscardCall::Flag::f_video
		: MTPphone_DiscardCall::Flag(0);

	// We want to discard request still being sent and processed even if
	// the call is already destroyed.
	if (migrateCall) {
		_user->owner().registerInvitedToCallUser(
			migrateCall->id(),
			migrateCall,
			_user,
			true);
	}
	const auto session = &_user->session();
	const auto weak = base::make_weak(this);
	session->api().request(MTPphone_DiscardCall( // We send 'discard' here.
		MTP_flags(flags),
		MTP_inputPhoneCall(
			MTP_long(_id),
			MTP_long(_accessHash)),
		MTP_int(duration),
		reason,
		MTP_long(connectionId)
	)).done([=](const MTPUpdates &result) {
		// Here 'this' could be destroyed by updates, so we set Ended after
		// updates being handled, but in a guarded way.
		crl::on_main(weak, [=] { setState(finalState); });
		session->api().applyUpdates(result);
	}).fail(crl::guard(weak, [this, finalState] {
		setState(finalState);
	})).send();
}

void Call::setStateQueued(State state) {
	crl::on_main(this, [=] {
		setState(state);
	});
}

void Call::setFailedQueued(const QString &error) {
	crl::on_main(this, [=] {
		handleControllerError(error);
	});
}

void Call::handleRequestError(const QString &error) {
	const auto inform = (error == u"USER_PRIVACY_RESTRICTED"_q)
		? tr::lng_call_error_not_available(tr::now, lt_user, _user->name())
		: (error == u"PARTICIPANT_VERSION_OUTDATED"_q)
		? tr::lng_call_error_outdated(tr::now, lt_user, _user->name())
		: (error == u"CALL_PROTOCOL_LAYER_INVALID"_q)
		? Lang::Hard::CallErrorIncompatible().replace(
			"{user}",
			_user->name())
		: error;
	if (!inform.isEmpty()) {
		if (const auto window = Core::App().windowFor(
				::Window::SeparateId(_user))) {
			window->show(Ui::MakeInformBox(inform));
		} else {
			Ui::show(Ui::MakeInformBox(inform));
		}
	}
	finish(FinishType::Failed);
}

void Call::handleControllerError(const QString &error) {
	const auto inform = (error == u"ERROR_INCOMPATIBLE"_q)
		? Lang::Hard::CallErrorIncompatible().replace(
			"{user}",
			_user->name())
		: (error == u"ERROR_AUDIO_IO"_q)
		? tr::lng_call_error_audio_io(tr::now)
		: QString();
	if (!inform.isEmpty()) {
		if (const auto window = Core::App().windowFor(
				::Window::SeparateId(_user))) {
			window->show(Ui::MakeInformBox(inform));
		} else {
			Ui::show(Ui::MakeInformBox(inform));
		}
	}
	finish(FinishType::Failed);
}

void Call::destroyController() {
	_instanceLifetime.destroy();
	Core::App().mediaDevices().setCaptureMuteTracker(this, false);

	if (_instance) {
		_instance->stop([](tgcalls::FinalState) {
		});

		DEBUG_LOG(("Call Info: Destroying call controller.."));
		_instance.reset();
		DEBUG_LOG(("Call Info: Call controller destroyed."));
	}
	setSignalBarCount(kSignalBarFinished);
}

Call::~Call() {
	destroyController();
}

void UpdateConfig(const std::string &data) {
}

} // namespace Calls
