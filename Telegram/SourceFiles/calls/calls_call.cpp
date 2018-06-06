/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
constexpr auto kMaxLayer = 75;
constexpr auto kHangupTimeoutMs = 5000;
constexpr auto kSha256Size = 32;

using tgvoip::Endpoint;

void ConvertEndpoint(
		std::vector<tgvoip::Endpoint> &ep,
		const MTPDphoneConnection &mtc) {
	if (mtc.vpeer_tag.v.length() != 16) {
		return;
	}
	auto ipv4 = tgvoip::IPv4Address(std::string(
		mtc.vip.v.constData(),
		mtc.vip.v.size()));
	auto ipv6 = tgvoip::IPv6Address(std::string(
		mtc.vipv6.v.constData(),
		mtc.vipv6.v.size()));
	ep.push_back(Endpoint(
		(int64_t)mtc.vid.v,
		(uint16_t)mtc.vport.v,
		ipv4,
		ipv6,
		tgvoip::Endpoint::TYPE_UDP_RELAY,
		(unsigned char*)mtc.vpeer_tag.v.data()));
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

} // namespace

void Call::ControllerPointer::create() {
	Expects(_data == nullptr);

	_data = std::make_unique<tgvoip::VoIPController>();
}

void Call::ControllerPointer::reset() {
	if (const auto controller = base::take(_data)) {
		controller->Stop();
	}
}

bool Call::ControllerPointer::empty() const {
	return (_data == nullptr);
}

bool Call::ControllerPointer::operator==(std::nullptr_t) const {
	return empty();
}

Call::ControllerPointer::operator bool() const {
	return !empty();
}

tgvoip::VoIPController *Call::ControllerPointer::operator->() const {
	Expects(!empty());

	return _data.get();
}

tgvoip::VoIPController &Call::ControllerPointer::operator*() const {
	Expects(!empty());

	return *_data;
}

Call::ControllerPointer::~ControllerPointer() {
	reset();
}

Call::Call(
	not_null<Delegate*> delegate,
	not_null<UserData*> user,
	Type type)
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
	return (_state == State::Starting) || (_state == State::WaitingIncoming);
}

void Call::start(bytes::const_span random) {
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
	Expects(_gaHash.size() == kSha256Size);

	request(MTPphone_RequestCall(
		_user->inputUser,
		MTP_int(rand_value<int32>()),
		MTP_bytes(_gaHash),
		MTP_phoneCallProtocol(
			MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p
				| MTPDphoneCallProtocol::Flag::f_udp_reflector),
			MTP_int(kMinLayer),
			MTP_int(kMaxLayer))
	)).done([this](const MTPphone_PhoneCall &result) {
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
	request(MTPphone_AcceptCall(
		MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)),
		MTP_bytes(_gb),
		MTP_phoneCallProtocol(
			MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p
				| MTPDphoneCallProtocol::Flag::f_udp_reflector),
			MTP_int(kMinLayer),
			MTP_int(kMaxLayer))
	)).done([this](const MTPphone_PhoneCall &result) {
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
	const auto debug = _controller->GetDebugString();
	return QString::fromUtf8(debug.data(), debug.size());
}

void Call::startWaitingTrack() {
	_waitingTrack = Media::Audio::Current().createTrack();
	auto trackFileName = Auth().settings().getSoundPath(
		(_type == Type::Outgoing)
		? qsl("call_outgoing")
		: qsl("call_incoming"));
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
		auto gaHashBytes = bytes::make_span(data.vg_a_hash.v);
		if (gaHashBytes.size() != kSha256Size) {
			LOG(("Call Error: Wrong g_a_hash size %1, expected %2.").arg(gaHashBytes.size()).arg(kSha256Size));
			finish(FinishType::Failed);
			return true;
		}
		_gaHash = bytes::make_vector(gaHashBytes);
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

	auto firstBytes = bytes::make_span(call.vg_b.v);
	auto computedAuthKey = MTP::CreateAuthKey(firstBytes, _randomPower, _dhConfig.p);
	if (computedAuthKey.empty()) {
		LOG(("Call Error: Could not compute mod-exp final."));
		finish(FinishType::Failed);
		return;
	}

	MTP::AuthKey::FillData(_authKey, computedAuthKey);
	_keyFingerprint = ComputeFingerprint(_authKey);

	setState(State::ExchangingKeys);
	request(MTPphone_ConfirmCall(
		MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)),
		MTP_bytes(_ga),
		MTP_long(_keyFingerprint),
		MTP_phoneCallProtocol(
			MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p
				| MTPDphoneCallProtocol::Flag::f_udp_reflector),
			MTP_int(kMinLayer),
			MTP_int(kMaxLayer))
	)).done([this](const MTPphone_PhoneCall &result) {
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

	auto firstBytes = bytes::make_span(call.vg_a_or_b.v);
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
	if (!checkCallFields(call)) {
		return;
	}

	tgvoip::VoIPController::Config config;
	config.dataSaving = tgvoip::DATA_SAVING_NEVER;
#ifdef Q_OS_MAC
	config.enableAEC = (QSysInfo::macVersion() < QSysInfo::MV_10_7);
#else // Q_OS_MAC
	config.enableAEC = true;
#endif // Q_OS_MAC
	config.enableNS = true;
	config.enableAGC = true;
	config.initTimeout = Global::CallConnectTimeoutMs() / 1000;
	config.recvTimeout = Global::CallPacketTimeoutMs() / 1000;
	if (Logs::DebugEnabled()) {
		auto callLogFolder = cWorkingDir() + qsl("DebugLogs");
		auto callLogPath = callLogFolder + qsl("/last_call_log.txt");
		auto callLogNative = QFile::encodeName(QDir::toNativeSeparators(callLogPath));
		auto callLogBytesSrc = bytes::make_span(callLogNative);
		auto callLogBytesDst = bytes::make_span(config.logFilePath);
		if (callLogBytesSrc.size() + 1 <= callLogBytesDst.size()) { // +1 - zero-terminator
			QFile(callLogPath).remove();
			QDir().mkpath(callLogFolder);
			bytes::copy(callLogBytesDst, callLogBytesSrc);
		}
	}

	const auto &protocol = call.vprotocol.c_phoneCallProtocol();
	auto endpoints = std::vector<Endpoint>();
	ConvertEndpoint(endpoints, call.vconnection.c_phoneConnection());
	for (int i = 0; i < call.valternative_connections.v.length(); i++) {
		ConvertEndpoint(endpoints, call.valternative_connections.v[i].c_phoneConnection());
	}

	auto callbacks = tgvoip::VoIPController::Callbacks();
	callbacks.connectionStateChanged = [](
			tgvoip::VoIPController *controller,
			int state) {
		const auto call = static_cast<Call*>(controller->implData);
		call->handleControllerStateChange(controller, state);
	};
	callbacks.signalBarCountChanged = [](
			tgvoip::VoIPController *controller,
			int count) {
		const auto call = static_cast<Call*>(controller->implData);
		call->handleControllerBarCountChange(controller, count);
	};

	_controller.create();
	if (_mute) {
		_controller->SetMicMute(_mute);
	}
	_controller->implData = static_cast<void*>(this);
	_controller->SetRemoteEndpoints(endpoints, true, protocol.vmax_layer.v);
	_controller->SetConfig(config);
	_controller->SetEncryptionKey(reinterpret_cast<char*>(_authKey.data()), (_type == Type::Outgoing));
	_controller->SetCallbacks(callbacks);
	if (Global::UseProxy() && Global::UseProxyForCalls()) {
		const auto proxy = Global::SelectedProxy();
		if (proxy.supportsCalls()) {
			Assert(proxy.type == ProxyData::Type::Socks5);
			_controller->SetProxy(
				tgvoip::PROXY_SOCKS5,
				proxy.host.toStdString(),
				proxy.port,
				proxy.user.toStdString(),
				proxy.password.toStdString());
		}
	}
	_controller->Start();
	_controller->Connect();
}

void Call::handleControllerStateChange(
		tgvoip::VoIPController *controller,
		int state) {
	// NB! Can be called from an arbitrary thread!
	// This can be called from ~VoIPController()!
	// Expects(controller == _controller.get());
	Expects(controller->implData == static_cast<void*>(this));

	switch (state) {
	case tgvoip::STATE_WAIT_INIT: {
		DEBUG_LOG(("Call Info: State changed to WaitingInit."));
		setStateQueued(State::WaitingInit);
	} break;

	case tgvoip::STATE_WAIT_INIT_ACK: {
		DEBUG_LOG(("Call Info: State changed to WaitingInitAck."));
		setStateQueued(State::WaitingInitAck);
	} break;

	case tgvoip::STATE_ESTABLISHED: {
		DEBUG_LOG(("Call Info: State changed to Established."));
		setStateQueued(State::Established);
	} break;

	case tgvoip::STATE_FAILED: {
		auto error = controller->GetLastError();
		LOG(("Call Info: State changed to Failed, error: %1.").arg(error));
		setFailedQueued(error);
	} break;

	default: LOG(("Call Error: Unexpected state in handleStateChange: %1").arg(state));
	}
}

void Call::handleControllerBarCountChange(
		tgvoip::VoIPController *controller,
		int count) {
	// NB! Can be called from an arbitrary thread!
	// This can be called from ~VoIPController()!
	// Expects(controller == _controller.get());
	Expects(controller->implData == static_cast<void*>(this));

	InvokeQueued(this, [=] {
		setSignalBarCount(count);
	});
}

void Call::setSignalBarCount(int count) {
	if (_signalBarCount != count) {
		_signalBarCount = count;
		_signalBarCountChanged.notify(count);
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

	setSignalBarCount(kSignalBarFinished);

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
	InvokeQueued(this, [=] {
		setState(state);
	});
}

void Call::setFailedQueued(int error) {
	InvokeQueued(this, [=] {
		handleControllerError(error);
	});
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
	if (error == tgvoip::ERROR_INCOMPATIBLE) {
		Ui::show(Box<InformBox>(
			Lang::Hard::CallErrorIncompatible().replace(
				"{user}",
				App::peerName(_user))));
	} else if (error == tgvoip::ERROR_AUDIO_IO) {
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
	setSignalBarCount(kSignalBarFinished);
}

Call::~Call() {
	destroyController();
}

void UpdateConfig(const std::map<std::string, std::string> &data) {
	tgvoip::ServerConfig::GetSharedInstance()->Update(data);
}

} // namespace Calls
