/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_call.h"

#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/rate_call_box.h"
#include "calls/calls_instance.h"
#include "base/openssl_help.h"
#include "mtproto/mtproto_dh_utils.h"
#include "mtproto/mtproto_config.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "media/audio/media_audio_track.h"
#include "base/platform/base_platform_info.h"
#include "calls/calls_panel.h"
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_media_devices.h"
#include "webrtc/webrtc_create_adm.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "facades.h"

#include <tgcalls/Instance.h>
#include <tgcalls/VideoCaptureInterface.h>

namespace tgcalls {
class InstanceImpl;
class InstanceImplLegacy;
class InstanceImplReference;
void SetLegacyGlobalServerConfig(const std::string &serverConfig);
} // namespace tgcalls

namespace Calls {
namespace {

constexpr auto kMinLayer = 65;
constexpr auto kHangupTimeoutMs = 5000;
constexpr auto kSha256Size = 32;
const auto kDefaultVersion = "2.4.4"_q;

#ifndef DESKTOP_APP_DISABLE_WEBRTC_INTEGRATION
const auto RegisterTag = tgcalls::Register<tgcalls::InstanceImpl>();
//const auto RegisterTagReference = tgcalls::Register<tgcalls::InstanceImplReference>();
#endif // DESKTOP_APP_DISABLE_WEBRTC_INTEGRATION
const auto RegisterTagLegacy = tgcalls::Register<tgcalls::InstanceImplLegacy>();

void AppendEndpoint(
		std::vector<tgcalls::Endpoint> &list,
		const MTPPhoneConnection &connection) {
	connection.match([&](const MTPDphoneConnection &data) {
		if (data.vpeer_tag().v.length() != 16) {
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
		const MTPPhoneConnection &connection) {
	connection.match([&](const MTPDphoneConnection &data) {
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
	auto result = QVector<MTPstring>();
	result.reserve(data.size());
	for (const auto &version : data) {
		result.push_back(MTP_string(version));
	}
	return result;
}

[[nodiscard]] QVector<MTPstring> CollectVersionsForApi() {
	return WrapVersions(tgcalls::Meta::Versions() | ranges::action::reverse);
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
, _videoIncoming(std::make_unique<Webrtc::VideoTrack>(StartVideoState(video)))
, _videoOutgoing(std::make_unique<Webrtc::VideoTrack>(StartVideoState(video))) {
	_discardByTimeoutTimer.setCallback([=] { hangup(); });

	if (_type == Type::Outgoing) {
		setState(State::Requesting);
	} else {
		startWaitingTrack();
	}
	setupOutgoingVideo();
}

void Call::generateModExpFirst(bytes::const_span randomSeed) {
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

	const auto flags = _videoCapture
		? MTPphone_RequestCall::Flag::f_video
		: MTPphone_RequestCall::Flag(0);
	_api.request(MTPphone_RequestCall(
		MTP_flags(flags),
		_user->inputUser,
		MTP_int(rand_value<int32>()),
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

		auto &call = result.c_phone_phoneCall();
		_user->session().data().processUsers(call.vusers());
		if (call.vphone_call().type() != mtpc_phoneCallWaiting) {
			LOG(("Call Error: Expected phoneCallWaiting in response to phone.requestCall()"));
			finish(FinishType::Failed);
			return;
		}

		auto &phoneCall = call.vphone_call();
		auto &waitingCall = phoneCall.c_phoneCallWaiting();
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
	}).fail([this](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::startIncoming() {
	Expects(_type == Type::Incoming);
	Expects(_state.current() == State::Starting);

	_api.request(MTPphone_ReceivedCall(
		MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash))
	)).done([=](const MTPBool &result) {
		if (_state.current() == State::Starting) {
			setState(State::WaitingIncoming);
		}
	}).fail([=](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::switchVideoOutgoing() {
	const auto video = _videoOutgoing->state() == Webrtc::VideoState::Active;
	_delegate->callRequestPermissionsOrFail(crl::guard(this, [=] {
		videoOutgoing()->setState(StartVideoState(!video));
	}), true);

}

void Call::answer() {
	const auto video = _videoOutgoing->state() == Webrtc::VideoState::Active;
	_delegate->callRequestPermissionsOrFail(crl::guard(this, [=] {
		actuallyAnswer();
	}), video);
}

void Call::actuallyAnswer() {
	Expects(_type == Type::Incoming);

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

		auto &call = result.c_phone_phoneCall();
		_user->session().data().processUsers(call.vusers());
		if (call.vphone_call().type() != mtpc_phoneCallWaiting) {
			LOG(("Call Error: "
				"Not phoneCallWaiting in response to phone.acceptCall."));
			finish(FinishType::Failed);
			return;
		}

		handleUpdate(call.vphone_call());
	}).fail([=](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::setMuted(bool mute) {
	_muted = mute;
	if (_instance) {
		_instance->setMuteMicrophone(mute);
	}
}

void Call::setupOutgoingVideo() {
	static const auto hasDevices = [] {
		return !Webrtc::GetVideoInputList().empty();
	};
	const auto started = _videoOutgoing->state();
	if (!hasDevices()) {
		_videoOutgoing->setState(Webrtc::VideoState::Inactive);
	}
	_videoOutgoing->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		if (state != Webrtc::VideoState::Inactive && !hasDevices()) {
			_errors.fire({ ErrorType::NoCamera });
			_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		} else if (_state.current() != State::Established
			&& (state != Webrtc::VideoState::Inactive)
			&& (started == Webrtc::VideoState::Inactive)) {
			_errors.fire({ ErrorType::NotStartedCall });
			_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		} else if (state != Webrtc::VideoState::Inactive
			&& _instance
			&& !_instance->supportsVideo()) {
			_errors.fire({ ErrorType::NotVideoCall });
			_videoOutgoing->setState(Webrtc::VideoState::Inactive);
		} else if (state != Webrtc::VideoState::Inactive) {
			// Paused not supported right now.
#ifndef DESKTOP_APP_DISABLE_WEBRTC_INTEGRATION
			Assert(state == Webrtc::VideoState::Active);
			if (!_videoCapture) {
				_videoCapture = _delegate->getVideoCapture();
				_videoCapture->setOutput(_videoOutgoing->sink());
			}
			if (_instance) {
				_instance->setVideoCapture(_videoCapture);
			}
			_videoCapture->setState(tgcalls::VideoState::Active);
#endif // DESKTOP_APP_DISABLE_WEBRTC_INTEGRATION
		} else if (_videoCapture) {
			_videoCapture->setState(tgcalls::VideoState::Inactive);
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

void Call::hangup() {
	const auto state = _state.current();
	if (state == State::Busy) {
		_delegate->callFinished(this);
	} else {
		auto missed = (state == State::Ringing || (state == State::Waiting && _type == Type::Outgoing));
		auto declined = isIncomingWaiting();
		auto reason = missed ? MTP_phoneCallDiscardReasonMissed() :
			declined ? MTP_phoneCallDiscardReasonBusy() : MTP_phoneCallDiscardReasonHangup();
		finish(FinishType::Ended, reason);
	}
}

void Call::redial() {
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
	auto trackFileName = Core::App().settings().getSoundPath(
		(_type == Type::Outgoing)
		? qsl("call_outgoing")
		: qsl("call_incoming"));
	_waitingTrack->samplePeakEach(kSoundSampleMs);
	_waitingTrack->fillFromFile(trackFileName);
	_waitingTrack->playInLoop();
}

void Call::sendSignalingData(const QByteArray &data) {
	_api.request(MTPphone_SendSignalingData(
		MTP_inputPhoneCall(
			MTP_long(_id),
			MTP_long(_accessHash)),
		MTP_bytes(data)
	)).done([=](const MTPBool &result) {
		if (!mtpIsTrue(result)) {
			finish(FinishType::Failed);
		}
	}).fail([=](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

float64 Call::getWaitingSoundPeakValue() const {
	if (_waitingTrack) {
		auto when = crl::now() + kSoundSampleMs / 4;
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

	auto encryptedChatAuthKey = bytes::vector(_authKey.size() + _ga.size(), gsl::byte {});
	bytes::copy(gsl::make_span(encryptedChatAuthKey).subspan(0, _authKey.size()), _authKey);
	bytes::copy(gsl::make_span(encryptedChatAuthKey).subspan(_authKey.size(), _ga.size()), _ga);
	return openssl::Sha256(encryptedChatAuthKey);
}

bool Call::handleUpdate(const MTPPhoneCall &call) {
	switch (call.type()) {
	case mtpc_phoneCallRequested: {
		auto &data = call.c_phoneCallRequested();
		if (_type != Type::Incoming
			|| _id != 0
			|| peerToUser(_user->id) != data.vadmin_id().v) {
			Unexpected("phoneCallRequested call inside an existing call handleUpdate()");
		}
		if (_user->session().userId() != data.vparticipant_id().v) {
			LOG(("Call Error: Wrong call participant_id %1, expected %2."
				).arg(data.vparticipant_id().v
				).arg(_user->session().userId()));
			finish(FinishType::Failed);
			return true;
		}

		_id = data.vid().v;
		_accessHash = data.vaccess_hash().v;
		auto gaHashBytes = bytes::make_span(data.vg_a_hash().v);
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
		auto &data = call.c_phoneCallEmpty();
		if (data.vid().v != _id) {
			return false;
		}
		LOG(("Call Error: phoneCallEmpty received."));
		finish(FinishType::Failed);
	} return true;

	case mtpc_phoneCallWaiting: {
		auto &data = call.c_phoneCallWaiting();
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
		auto &data = call.c_phoneCall();
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
		auto &data = call.c_phoneCallDiscarded();
		if (data.vid().v != _id) {
			return false;
		}
		if (data.is_need_debug()) {
			auto debugLog = _instance
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
			Ui::show(Box<RateCallBox>(&_user->session(), _id, _accessHash));
		}
		const auto reason = data.vreason();
		if (reason && reason->type() == mtpc_phoneCallDiscardReasonDisconnect) {
			LOG(("Call Info: Discarded with DISCONNECT reason."));
		}
		if (reason && reason->type() == mtpc_phoneCallDiscardReasonBusy) {
			setState(State::Busy);
		} else if (_type == Type::Outgoing
			|| _state.current() == State::HangingUp) {
			setState(State::Ended);
		} else {
			setState(State::EndedByOtherDevice);
		}
	} return true;

	case mtpc_phoneCallAccepted: {
		auto &data = call.c_phoneCallAccepted();
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
	auto prepared = ranges::view::all(
		data.vdata().v
	) | ranges::view::transform([](char byte) {
		return static_cast<uint8_t>(byte);
	}) | ranges::to_vector;
	_instance->receiveSignalingData(std::move(prepared));
	return true;
}

void Call::confirmAcceptedCall(const MTPDphoneCallAccepted &call) {
	Expects(_type == Type::Outgoing);

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

		auto &call = result.c_phone_phoneCall();
		_user->session().data().processUsers(call.vusers());
		if (call.vphone_call().type() != mtpc_phoneCall) {
			LOG(("Call Error: Expected phoneCall in response to phone.confirmCall()"));
			finish(FinishType::Failed);
			return;
		}

		createAndStartController(call.vphone_call().c_phoneCall());
	}).fail([=](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::startConfirmedCall(const MTPDphoneCall &call) {
	Expects(_type == Type::Incoming);

	auto firstBytes = bytes::make_span(call.vg_a_or_b().v);
	if (_gaHash != openssl::Sha256(firstBytes)) {
		LOG(("Call Error: Wrong g_a hash received."));
		finish(FinishType::Failed);
		return;
	}
	_ga = bytes::vector(firstBytes.begin(), firstBytes.end());

	auto computedAuthKey = MTP::CreateAuthKey(firstBytes, _randomPower, _dhConfig.p);
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
	_discardByTimeoutTimer.cancel();
	if (!checkCallFields(call) || _authKey.size() != 256) {
		return;
	}

	const auto &protocol = call.vprotocol().c_phoneCallProtocol();
	const auto &serverConfig = _user->session().serverConfig();

	auto encryptionKeyValue = std::make_shared<std::array<uint8_t, 256>>();
	memcpy(encryptionKeyValue->data(), _authKey.data(), 256);

	const auto &settings = Core::App().settings();

	const auto weak = base::make_weak(this);
	tgcalls::Descriptor descriptor = {
		.config = tgcalls::Config{
			.initializationTimeout = serverConfig.callConnectTimeoutMs / 1000.,
			.receiveTimeout = serverConfig.callPacketTimeoutMs / 1000.,
			.dataSaving = tgcalls::DataSaving::Never,
			.enableP2P = call.is_p2p_allowed(),
			.enableAEC = !Platform::IsMac10_7OrGreater(),
			.enableNS = true,
			.enableAGC = true,
			.enableVolumeControl = true,
			.maxApiLayer = protocol.vmax_layer().v,
		},
		.encryptionKey = tgcalls::EncryptionKey(
			std::move(encryptionKeyValue),
			(_type == Type::Outgoing)),
		.mediaDevicesConfig = tgcalls::MediaDevicesConfig{
			.audioInputId = settings.callInputDeviceId().toStdString(),
			.audioOutputId = settings.callOutputDeviceId().toStdString(),
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
		.remoteMediaStateUpdated = [=](tgcalls::AudioState audio, tgcalls::VideoState video) {
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
			settings.callAudioBackend()),
	};
	if (Logs::DebugEnabled()) {
		auto callLogFolder = cWorkingDir() + qsl("DebugLogs");
		auto callLogPath = callLogFolder + qsl("/last_call_log.txt");
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

	for (const auto &connection : call.vconnections().v) {
		AppendEndpoint(descriptor.endpoints, connection);
	}
	for (const auto &connection : call.vconnections().v) {
		AppendServer(descriptor.rtcServers, connection);
	}

	if (Global::UseProxyForCalls()
		&& (Global::ProxySettings() == MTP::ProxyData::Settings::Enabled)) {
		const auto &selected = Global::SelectedProxy();
		if (selected.supportsCalls() && !selected.host.isEmpty()) {
			Assert(selected.type == MTP::ProxyData::Type::Socks5);
			descriptor.proxy = std::make_unique<tgcalls::Proxy>();
			descriptor.proxy->host = selected.host.toStdString();
			descriptor.proxy->port = selected.port;
			descriptor.proxy->login = selected.user.toStdString();
			descriptor.proxy->password = selected.password.toStdString();
		}
	}

	const auto version = call.vprotocol().match([&](
			const MTPDphoneCallProtocol &data) {
		return data.vlibrary_versions().v;
	}).value(0, MTP_bytes(kDefaultVersion)).v;

	LOG(("Call Info: Creating instance with version '%1', allowP2P: %2"
		).arg(QString::fromUtf8(version)
		).arg(Logs::b(descriptor.config.enableP2P)));
	_instance = tgcalls::Meta::Create(
		version.toStdString(),
		std::move(descriptor));
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
}

void Call::handleControllerStateChange(tgcalls::State state) {
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
		auto error = _instance
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
	auto checkFailed = [this] {
		finish(FinishType::Failed);
		return false;
	};
	if (call.vaccess_hash().v != _accessHash) {
		LOG(("Call Error: Wrong call access_hash."));
		return checkFailed();
	}
	auto adminId = (_type == Type::Outgoing) ? _user->session().userId() : peerToUser(_user->id);
	auto participantId = (_type == Type::Outgoing) ? peerToUser(_user->id) : _user->session().userId();
	if (call.vadmin_id().v != adminId) {
		LOG(("Call Error: Wrong call admin_id %1, expected %2.").arg(call.vadmin_id().v).arg(adminId));
		return checkFailed();
	}
	if (call.vparticipant_id().v != participantId) {
		LOG(("Call Error: Wrong call participant_id %1, expected %2.").arg(call.vparticipant_id().v).arg(participantId));
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
	if (_state.current() == State::Failed) {
		return;
	}
	if (_state.current() == State::FailedHangingUp && state != State::Failed) {
		return;
	}
	if (_state.current() != state) {
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
			_delegate->callPlaySound(Delegate::CallSound::Ended);
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
			break;
		}
	}
}

void Call::setCurrentAudioDevice(bool input, const QString &deviceId) {
	if (_instance) {
		const auto id = deviceId.toStdString();
		if (input) {
			_instance->setAudioInputDevice(id);
		} else {
			_instance->setAudioOutputDevice(id);
		}
	}
}

void Call::setCurrentVideoDevice(const QString &deviceId) {
	if (_videoCapture) {
		_videoCapture->switchToDevice(deviceId.toStdString());
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

void Call::finish(FinishType type, const MTPPhoneCallDiscardReason &reason) {
	Expects(type != FinishType::None);

	setSignalBarCount(kSignalBarFinished);

	auto finalState = (type == FinishType::Ended) ? State::Ended : State::Failed;
	auto hangupState = (type == FinishType::Ended) ? State::HangingUp : State::FailedHangingUp;
	const auto state = _state.current();
	if (state == State::Requesting) {
		_finishByTimeoutTimer.call(kHangupTimeoutMs, [this, finalState] { setState(finalState); });
		_finishAfterRequestingCall = type;
		return;
	}
	if (state == State::HangingUp
		|| state == State::FailedHangingUp
		|| state == State::EndedByOtherDevice
		|| state == State::Ended
		|| state == State::Failed) {
		return;
	}
	if (!_id) {
		setState(finalState);
		return;
	}

	setState(hangupState);
	auto duration = getDurationMs() / 1000;
	auto connectionId = _instance ? _instance->getPreferredRelayId() : 0;
	_finishByTimeoutTimer.call(kHangupTimeoutMs, [this, finalState] { setState(finalState); });
	const auto flags = ((_videoIncoming->state() != Webrtc::VideoState::Inactive)
		|| (_videoOutgoing->state() != Webrtc::VideoState::Inactive))
		? MTPphone_DiscardCall::Flag::f_video
		: MTPphone_DiscardCall::Flag(0);

	// We want to discard request still being sent and processed even if
	// the call is already destroyed.
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
	}).fail(crl::guard(weak, [this, finalState](const RPCError &error) {
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

void Call::handleRequestError(const RPCError &error) {
	if (error.type() == qstr("USER_PRIVACY_RESTRICTED")) {
		Ui::show(Box<InformBox>(tr::lng_call_error_not_available(tr::now, lt_user, _user->name)));
	} else if (error.type() == qstr("PARTICIPANT_VERSION_OUTDATED")) {
		Ui::show(Box<InformBox>(tr::lng_call_error_outdated(tr::now, lt_user, _user->name)));
	} else if (error.type() == qstr("CALL_PROTOCOL_LAYER_INVALID")) {
		Ui::show(Box<InformBox>(Lang::Hard::CallErrorIncompatible().replace("{user}", _user->name)));
	}
	finish(FinishType::Failed);
}

void Call::handleControllerError(const QString &error) {
	if (error == u"ERROR_INCOMPATIBLE"_q) {
		Ui::show(Box<InformBox>(
			Lang::Hard::CallErrorIncompatible().replace(
				"{user}",
				_user->name)));
	} else if (error == u"ERROR_AUDIO_IO"_q) {
		Ui::show(Box<InformBox>(tr::lng_call_error_audio_io(tr::now)));
	}
	finish(FinishType::Failed);
}

void Call::destroyController() {
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
	tgcalls::SetLegacyGlobalServerConfig(data);
}

} // namespace Calls
