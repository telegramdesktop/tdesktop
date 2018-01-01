/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "calls/calls_call.h"

#include "auth_session.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/rate_call_box.h"
#include "calls/calls_instance.h"
#include "base/openssl_help.h"
#include "mtproto/connection.h"
#include "media/media_audio_track.h"
#include "calls/calls_panel.h"

#ifdef slots
#undef slots
#define NEED_TO_RESTORE_SLOTS
#endif // slots

#include <VoIPController.h>
#include <VoIPServerConfig.h>

#ifdef NEED_TO_RESTORE_SLOTS
#define slots Q_SLOTS
#undef NEED_TO_RESTORE_SLOTS
#endif // NEED_TO_RESTORE_SLOTS

namespace Calls {
namespace {

constexpr auto kMinLayer = 65;
constexpr auto kMaxLayer = 65; // MTP::CurrentLayer?
constexpr auto kHangupTimeoutMs = 5000;

using tgvoip::Endpoint;

void ConvertEndpoint(std::vector<tgvoip::Endpoint> &ep, const MTPDphoneConnection &mtc) {
	if (mtc.vpeer_tag.v.length() != 16) {
		return;
	}
	auto ipv4 = tgvoip::IPv4Address(std::string(mtc.vip.v.constData(), mtc.vip.v.size()));
	auto ipv6 = tgvoip::IPv6Address(std::string(mtc.vipv6.v.constData(), mtc.vipv6.v.size()));
	ep.push_back(Endpoint((int64_t)mtc.vid.v, (uint16_t)mtc.vport.v, ipv4, ipv6, EP_TYPE_UDP_RELAY, (unsigned char*)mtc.vpeer_tag.v.data()));
}

constexpr auto kFingerprintDataSize = 256;
uint64 ComputeFingerprint(const std::array<gsl::byte, kFingerprintDataSize> &authKey) {
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

} // namespace

Call::Call(not_null<Delegate*> delegate, not_null<UserData*> user, Type type)
: _delegate(delegate)
, _user(user)
, _type(type) {
	_discardByTimeoutTimer.setCallback([this] { hangup(); });

	if (_type == Type::Outgoing) {
		setState(State::Requesting);
	} else {
		startWaitingTrack();
	}
}

void Call::generateModExpFirst(base::const_byte_span randomSeed) {
	auto first = MTP::CreateModExp(_dhConfig.g, _dhConfig.p, randomSeed);
	if (first.modexp.empty()) {
		LOG(("Call Error: Could not compute mod-exp first."));
		finish(FinishType::Failed);
		return;
	}

	_randomPower = first.randomPower;
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
	return (_state == State::Starting) || (_state == State::WaitingIncoming);
}

void Call::start(base::const_byte_span random) {
	// Save config here, because it is possible that it changes between
	// different usages inside the same call.
	_dhConfig = _delegate->getDhConfig();
	Assert(_dhConfig.g != 0);
	Assert(!_dhConfig.p.empty());

	generateModExpFirst(random);
	if (_state == State::Starting || _state == State::Requesting) {
		if (_type == Type::Outgoing) {
			startOutgoing();
		} else {
			startIncoming();
		}
	} else if (_state == State::ExchangingKeys && _answerAfterDhConfigReceived) {
		answer();
	}
}

void Call::startOutgoing() {
	Expects(_type == Type::Outgoing);
	Expects(_state == State::Requesting);

	request(MTPphone_RequestCall(_user->inputUser, MTP_int(rand_value<int32>()), MTP_bytes(_gaHash), MTP_phoneCallProtocol(MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p | MTPDphoneCallProtocol::Flag::f_udp_reflector), MTP_int(kMinLayer), MTP_int(kMaxLayer)))).done([this](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);

		setState(State::Waiting);

		auto &call = result.c_phone_phoneCall();
		App::feedUsers(call.vusers);
		if (call.vphone_call.type() != mtpc_phoneCallWaiting) {
			LOG(("Call Error: Expected phoneCallWaiting in response to phone.requestCall()"));
			finish(FinishType::Failed);
			return;
		}

		auto &phoneCall = call.vphone_call;
		auto &waitingCall = phoneCall.c_phoneCallWaiting();
		_id = waitingCall.vid.v;
		_accessHash = waitingCall.vaccess_hash.v;
		if (_finishAfterRequestingCall != FinishType::None) {
			if (_finishAfterRequestingCall == FinishType::Failed) {
				finish(_finishAfterRequestingCall);
			} else {
				hangup();
			}
			return;
		}

		_discardByTimeoutTimer.callOnce(Global::CallReceiveTimeoutMs());
		handleUpdate(phoneCall);
	}).fail([this](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::startIncoming() {
	Expects(_type == Type::Incoming);
	Expects(_state == State::Starting);

	request(MTPphone_ReceivedCall(MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)))).done([this](const MTPBool &result) {
		if (_state == State::Starting) {
			setState(State::WaitingIncoming);
		}
	}).fail([this](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::answer() {
	Expects(_type == Type::Incoming);

	if (_state != State::Starting && _state != State::WaitingIncoming) {
		if (_state != State::ExchangingKeys || !_answerAfterDhConfigReceived) {
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
	request(MTPphone_AcceptCall(MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)), MTP_bytes(_gb), _protocol)).done([this](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);
		auto &call = result.c_phone_phoneCall();
		App::feedUsers(call.vusers);
		if (call.vphone_call.type() != mtpc_phoneCallWaiting) {
			LOG(("Call Error: Expected phoneCallWaiting in response to phone.acceptCall()"));
			finish(FinishType::Failed);
			return;
		}

		handleUpdate(call.vphone_call);
	}).fail([this](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::setMute(bool mute) {
	_mute = mute;
	if (_controller) {
		_controller->SetMicMute(_mute);
	}
	_muteChanged.notify(_mute);
}

TimeMs Call::getDurationMs() const {
	return _startTime ? (getms(true) - _startTime) : 0;
}

void Call::hangup() {
	if (_state == State::Busy) {
		_delegate->callFinished(this);
	} else {
		auto missed = (_state == State::Ringing || (_state == State::Waiting && _type == Type::Outgoing));
		auto declined = isIncomingWaiting();
		auto reason = missed ? MTP_phoneCallDiscardReasonMissed() :
			declined ? MTP_phoneCallDiscardReasonBusy() : MTP_phoneCallDiscardReasonHangup();
		finish(FinishType::Ended, reason);
	}
}

void Call::redial() {
	if (_state != State::Busy) {
		return;
	}
	Assert(_controller == nullptr);
	_type = Type::Outgoing;
	setState(State::Requesting);
	_answerAfterDhConfigReceived = false;
	startWaitingTrack();
	_delegate->callRedial(this);
}

QString Call::getDebugLog() const {
	constexpr auto kDebugLimit = 4096;
	auto bytes = base::byte_vector(kDebugLimit, gsl::byte {});
	_controller->GetDebugString(reinterpret_cast<char*>(bytes.data()), bytes.size());
	auto end = std::find(bytes.begin(), bytes.end(), gsl::byte {});
	auto size = (end - bytes.begin());
	return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()), size);
}

void Call::startWaitingTrack() {
	_waitingTrack = Media::Audio::Current().createTrack();
	auto trackFileName = Auth().data().getSoundPath((_type == Type::Outgoing) ? qsl("call_outgoing") : qsl("call_incoming"));
	_waitingTrack->samplePeakEach(kSoundSampleMs);
	_waitingTrack->fillFromFile(trackFileName);
	_waitingTrack->playInLoop();
}

float64 Call::getWaitingSoundPeakValue() const {
	if (_waitingTrack) {
		auto when = getms() + kSoundSampleMs / 4;
		return _waitingTrack->getPeakValue(when);
	}
	return 0.;
}

bool Call::isKeyShaForFingerprintReady() const {
	return (_keyFingerprint != 0);
}

base::byte_array<Call::kSha256Size> Call::getKeyShaForFingerprint() const {
	Expects(isKeyShaForFingerprintReady());
	Expects(!_ga.empty());
	auto encryptedChatAuthKey = base::byte_vector(_authKey.size() + _ga.size(), gsl::byte {});
	base::copy_bytes(gsl::make_span(encryptedChatAuthKey).subspan(0, _authKey.size()), _authKey);
	base::copy_bytes(gsl::make_span(encryptedChatAuthKey).subspan(_authKey.size(), _ga.size()), _ga);
	return openssl::Sha256(encryptedChatAuthKey);
}

bool Call::handleUpdate(const MTPPhoneCall &call) {
	switch (call.type()) {
	case mtpc_phoneCallRequested: {
		auto &data = call.c_phoneCallRequested();
		if (_type != Type::Incoming
			|| _id != 0
			|| peerToUser(_user->id) != data.vadmin_id.v) {
			Unexpected("phoneCallRequested call inside an existing call handleUpdate()");
		}
		if (Auth().userId() != data.vparticipant_id.v) {
			LOG(("Call Error: Wrong call participant_id %1, expected %2.").arg(data.vparticipant_id.v).arg(Auth().userId()));
			finish(FinishType::Failed);
			return true;
		}
		_id = data.vid.v;
		_accessHash = data.vaccess_hash.v;
		_protocol = data.vprotocol;
		auto gaHashBytes = bytesFromMTP(data.vg_a_hash);
		if (gaHashBytes.size() != _gaHash.size()) {
			LOG(("Call Error: Wrong g_a_hash size %1, expected %2.").arg(gaHashBytes.size()).arg(_gaHash.size()));
			finish(FinishType::Failed);
			return true;
		}
		base::copy_bytes(gsl::make_span(_gaHash), gaHashBytes);
	} return true;

	case mtpc_phoneCallEmpty: {
		auto &data = call.c_phoneCallEmpty();
		if (data.vid.v != _id) {
			return false;
		}
		LOG(("Call Error: phoneCallEmpty received."));
		finish(FinishType::Failed);
	} return true;

	case mtpc_phoneCallWaiting: {
		auto &data = call.c_phoneCallWaiting();
		if (data.vid.v != _id) {
			return false;
		}
		if (_type == Type::Outgoing && _state == State::Waiting && data.vreceive_date.v != 0) {
			_discardByTimeoutTimer.callOnce(Global::CallRingTimeoutMs());
			setState(State::Ringing);
			startWaitingTrack();
		}
	} return true;

	case mtpc_phoneCall: {
		auto &data = call.c_phoneCall();
		if (data.vid.v != _id) {
			return false;
		}
		if (_type == Type::Incoming && _state == State::ExchangingKeys) {
			startConfirmedCall(data);
		}
	} return true;

	case mtpc_phoneCallDiscarded: {
		auto &data = call.c_phoneCallDiscarded();
		if (data.vid.v != _id) {
			return false;
		}
		if (data.is_need_debug()) {
			auto debugLog = _controller ? _controller->GetDebugLog() : std::string();
			if (!debugLog.empty()) {
				MTP::send(MTPphone_SaveCallDebug(MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)), MTP_dataJSON(MTP_string(debugLog))));
			}
		}
		if (data.is_need_rating() && _id && _accessHash) {
			Ui::show(Box<RateCallBox>(_id, _accessHash));
		}
		if (data.has_reason() && data.vreason.type() == mtpc_phoneCallDiscardReasonDisconnect) {
			LOG(("Call Info: Discarded with DISCONNECT reason."));
		}
		if (data.has_reason() && data.vreason.type() == mtpc_phoneCallDiscardReasonBusy) {
			setState(State::Busy);
		} else if (_type == Type::Outgoing || _state == State::HangingUp) {
			setState(State::Ended);
		} else {
			setState(State::EndedByOtherDevice);
		}
	} return true;

	case mtpc_phoneCallAccepted: {
		auto &data = call.c_phoneCallAccepted();
		if (data.vid.v != _id) {
			return false;
		}
		if (_type != Type::Outgoing) {
			LOG(("Call Error: Unexpected phoneCallAccepted for an incoming call."));
			finish(FinishType::Failed);
		} else if (checkCallFields(data)) {
			confirmAcceptedCall(data);
		}
	} return true;
	}

	Unexpected("phoneCall type inside an existing call handleUpdate()");
}

void Call::confirmAcceptedCall(const MTPDphoneCallAccepted &call) {
	Expects(_type == Type::Outgoing);

	auto firstBytes = bytesFromMTP(call.vg_b);
	auto computedAuthKey = MTP::CreateAuthKey(firstBytes, _randomPower, _dhConfig.p);
	if (computedAuthKey.empty()) {
		LOG(("Call Error: Could not compute mod-exp final."));
		finish(FinishType::Failed);
		return;
	}

	MTP::AuthKey::FillData(_authKey, computedAuthKey);
	_keyFingerprint = ComputeFingerprint(_authKey);

	setState(State::ExchangingKeys);
	request(MTPphone_ConfirmCall(MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)), MTP_bytes(_ga), MTP_long(_keyFingerprint), MTP_phoneCallProtocol(MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p | MTPDphoneCallProtocol::Flag::f_udp_reflector), MTP_int(kMinLayer), MTP_int(kMaxLayer)))).done([this](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);
		auto &call = result.c_phone_phoneCall();
		App::feedUsers(call.vusers);
		if (call.vphone_call.type() != mtpc_phoneCall) {
			LOG(("Call Error: Expected phoneCall in response to phone.confirmCall()"));
			finish(FinishType::Failed);
			return;
		}

		createAndStartController(call.vphone_call.c_phoneCall());
	}).fail([this](const RPCError &error) {
		handleRequestError(error);
	}).send();
}

void Call::startConfirmedCall(const MTPDphoneCall &call) {
	Expects(_type == Type::Incoming);

	auto firstBytes = bytesFromMTP(call.vg_a_or_b);
	if (_gaHash != openssl::Sha256(firstBytes)) {
		LOG(("Call Error: Wrong g_a hash received."));
		finish(FinishType::Failed);
		return;
	}
	_ga = base::byte_vector(firstBytes.begin(), firstBytes.end());

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
	if (!checkCallFields(call)) {
		return;
	}

	voip_config_t config = { 0 };
	config.data_saving = DATA_SAVING_NEVER;
#ifdef Q_OS_MAC
	config.enableAEC = (QSysInfo::macVersion() < QSysInfo::MV_10_7);
#else // Q_OS_MAC
	config.enableAEC = true;
#endif // Q_OS_MAC
	config.enableNS = true;
	config.enableAGC = true;
	config.init_timeout = Global::CallConnectTimeoutMs() / 1000;
	config.recv_timeout = Global::CallPacketTimeoutMs() / 1000;
	if (cDebug()) {
		auto callLogFolder = cWorkingDir() + qsl("DebugLogs");
		auto callLogPath = callLogFolder + qsl("/last_call_log.txt");
		auto callLogNative = QFile::encodeName(QDir::toNativeSeparators(callLogPath));
		auto callLogBytesSrc = gsl::as_bytes(gsl::make_span(callLogNative));
		auto callLogBytesDst = gsl::as_writeable_bytes(gsl::make_span(config.logFilePath));
		if (callLogBytesSrc.size() + 1 <= callLogBytesDst.size()) { // +1 - zero-terminator
			QFile(callLogPath).remove();
			QDir().mkpath(callLogFolder);
			base::copy_bytes(callLogBytesDst, callLogBytesSrc);
		}
	}

	std::vector<Endpoint> endpoints;
	ConvertEndpoint(endpoints, call.vconnection.c_phoneConnection());
	for (int i = 0; i < call.valternative_connections.v.length(); i++) {
		ConvertEndpoint(endpoints, call.valternative_connections.v[i].c_phoneConnection());
	}

	_controller = std::make_unique<tgvoip::VoIPController>();
	if (_mute) {
		_controller->SetMicMute(_mute);
	}
	_controller->implData = static_cast<void*>(this);
	_controller->SetRemoteEndpoints(endpoints, true);
	_controller->SetConfig(&config);
	_controller->SetEncryptionKey(reinterpret_cast<char*>(_authKey.data()), (_type == Type::Outgoing));
	_controller->SetStateCallback([](tgvoip::VoIPController *controller, int state) {
		static_cast<Call*>(controller->implData)->handleControllerStateChange(controller, state);
	});
	_controller->Start();
	_controller->Connect();
}

void Call::handleControllerStateChange(tgvoip::VoIPController *controller, int state) {
	// NB! Can be called from an arbitrary thread!
	// Expects(controller == _controller.get()); This can be called from ~VoIPController()!
	Expects(controller->implData == static_cast<void*>(this));

	switch (state) {
	case STATE_WAIT_INIT: {
		DEBUG_LOG(("Call Info: State changed to WaitingInit."));
		setStateQueued(State::WaitingInit);
	} break;

	case STATE_WAIT_INIT_ACK: {
		DEBUG_LOG(("Call Info: State changed to WaitingInitAck."));
		setStateQueued(State::WaitingInitAck);
	} break;

	case STATE_ESTABLISHED: {
		DEBUG_LOG(("Call Info: State changed to Established."));
		setStateQueued(State::Established);
	} break;

	case STATE_FAILED: {
		auto error = controller->GetLastError();
		LOG(("Call Info: State changed to Failed, error: %1.").arg(error));
		setFailedQueued(error);
	} break;

	default: LOG(("Call Error: Unexpected state in handleStateChange: %1").arg(state));
	}
}

template <typename T>
bool Call::checkCallCommonFields(const T &call) {
	auto checkFailed = [this] {
		finish(FinishType::Failed);
		return false;
	};
	if (call.vaccess_hash.v != _accessHash) {
		LOG(("Call Error: Wrong call access_hash."));
		return checkFailed();
	}
	auto adminId = (_type == Type::Outgoing) ? Auth().userId() : peerToUser(_user->id);
	auto participantId = (_type == Type::Outgoing) ? peerToUser(_user->id) : Auth().userId();
	if (call.vadmin_id.v != adminId) {
		LOG(("Call Error: Wrong call admin_id %1, expected %2.").arg(call.vadmin_id.v).arg(adminId));
		return checkFailed();
	}
	if (call.vparticipant_id.v != participantId) {
		LOG(("Call Error: Wrong call participant_id %1, expected %2.").arg(call.vparticipant_id.v).arg(participantId));
		return checkFailed();
	}
	return true;
}

bool Call::checkCallFields(const MTPDphoneCall &call) {
	if (!checkCallCommonFields(call)) {
		return false;
	}
	if (call.vkey_fingerprint.v != _keyFingerprint) {
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
	if (_state == State::Failed) {
		return;
	}
	if (_state == State::FailedHangingUp && state != State::Failed) {
		return;
	}
	if (_state != state) {
		_state = state;
		_stateChanged.notify(state, true);

		if (true
			&& _state != State::Starting
			&& _state != State::Requesting
			&& _state != State::Waiting
			&& _state != State::WaitingIncoming
			&& _state != State::Ringing) {
			_waitingTrack.reset();
		}
		if (false
			|| _state == State::Ended
			|| _state == State::EndedByOtherDevice
			|| _state == State::Failed
			|| _state == State::Busy) {
			// Destroy controller before destroying Call Panel,
			// so that the panel hide animation is smooth.
			destroyController();
		}
		switch (_state) {
		case State::Established:
			_startTime = getms(true);
			break;
		case State::ExchangingKeys:
			_delegate->playSound(Delegate::Sound::Connecting);
			break;
		case State::Ended:
			_delegate->playSound(Delegate::Sound::Ended);
			[[fallthrough]];
		case State::EndedByOtherDevice:
			_delegate->callFinished(this);
			break;
		case State::Failed:
			_delegate->playSound(Delegate::Sound::Ended);
			_delegate->callFailed(this);
			break;
		case State::Busy:
			_delegate->playSound(Delegate::Sound::Busy);
			break;
		}
	}
}

void Call::finish(FinishType type, const MTPPhoneCallDiscardReason &reason) {
	Expects(type != FinishType::None);
	auto finalState = (type == FinishType::Ended) ? State::Ended : State::Failed;
	auto hangupState = (type == FinishType::Ended) ? State::HangingUp : State::FailedHangingUp;
	if (_state == State::Requesting) {
		_finishByTimeoutTimer.call(kHangupTimeoutMs, [this, finalState] { setState(finalState); });
		_finishAfterRequestingCall = type;
		return;
	}
	if (_state == State::HangingUp
		|| _state == State::FailedHangingUp
		|| _state == State::EndedByOtherDevice
		|| _state == State::Ended
		|| _state == State::Failed) {
		return;
	}
	if (!_id) {
		setState(finalState);
		return;
	}

	setState(hangupState);
	auto duration = getDurationMs() / 1000;
	auto connectionId = _controller ? _controller->GetPreferredRelayID() : 0;
	_finishByTimeoutTimer.call(kHangupTimeoutMs, [this, finalState] { setState(finalState); });
	request(MTPphone_DiscardCall(MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)), MTP_int(duration), reason, MTP_long(connectionId))).done([this, finalState](const MTPUpdates &result) {
		// This could be destroyed by updates, so we set Ended after
		// updates being handled, but in a guarded way.
		InvokeQueued(this, [this, finalState] { setState(finalState); });
		App::main()->sentUpdatesReceived(result);
	}).fail([this, finalState](const RPCError &error) {
		setState(finalState);
	}).send();
}

void Call::setStateQueued(State state) {
	InvokeQueued(this, [this, state] { setState(state); });
}

void Call::setFailedQueued(int error) {
	InvokeQueued(this, [this, error] { handleControllerError(error); });
}

void Call::handleRequestError(const RPCError &error) {
	if (error.type() == qstr("USER_PRIVACY_RESTRICTED")) {
		Ui::show(Box<InformBox>(lng_call_error_not_available(lt_user, App::peerName(_user))));
	} else if (error.type() == qstr("PARTICIPANT_VERSION_OUTDATED")) {
		Ui::show(Box<InformBox>(lng_call_error_outdated(lt_user, App::peerName(_user))));
	} else if (error.type() == qstr("CALL_PROTOCOL_LAYER_INVALID")) {
		Ui::show(Box<InformBox>(Lang::Hard::CallErrorIncompatible().replace("{user}", App::peerName(_user))));
	}
	finish(FinishType::Failed);
}

void Call::handleControllerError(int error) {
	if (error == TGVOIP_ERROR_INCOMPATIBLE) {
		Ui::show(Box<InformBox>(Lang::Hard::CallErrorIncompatible().replace("{user}", App::peerName(_user))));
	} else if (error == TGVOIP_ERROR_AUDIO_IO) {
		Ui::show(Box<InformBox>(lang(lng_call_error_audio_io)));
	}
	finish(FinishType::Failed);
}

void Call::destroyController() {
	if (_controller) {
		DEBUG_LOG(("Call Info: Destroying call controller.."));
		_controller.reset();
		DEBUG_LOG(("Call Info: Call controller destroyed."));
	}
}

Call::~Call() {
	destroyController();
}

void UpdateConfig(const std::map<std::string, std::string> &data) {
	tgvoip::ServerConfig::GetSharedInstance()->Update(data);
}

} // namespace Calls
