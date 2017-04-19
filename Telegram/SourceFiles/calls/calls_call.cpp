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
#include "calls/calls_instance.h"

#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

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

using tgvoip::Endpoint;

void ConvertEndpoint(std::vector<tgvoip::Endpoint> &ep, const MTPDphoneConnection &mtc) {
	if (mtc.vpeer_tag.v.length() != 16)
		return;
	auto ipv4 = tgvoip::IPv4Address(std::string(mtc.vip.v.constData(), mtc.vip.v.size()));
	auto ipv6 = tgvoip::IPv6Address(std::string(mtc.vipv6.v.constData(), mtc.vipv6.v.size()));
	ep.push_back(Endpoint((int64_t)mtc.vid.v, (uint16_t)mtc.vport.v, ipv4, ipv6, EP_TYPE_UDP_RELAY, (unsigned char*)mtc.vpeer_tag.v.data()));
}

} // namespace

Call::Call(gsl::not_null<Delegate*> delegate, gsl::not_null<UserData*> user)
: _delegate(delegate)
, _user(user) {
	// Save config here, because it is possible that it changes between
	// different usages inside the same call.
	_dhConfig = _delegate->getDhConfig();
}

void Call::generateSalt(base::const_byte_span random) {
	Expects(random.size() == _salt.size());
	memset_rand(_salt.data(), _salt.size());
	for (auto i = 0, count = int(_salt.size()); i != count; i++) {
		_salt[i] ^= random[i];
	}
}

void Call::startOutgoing(base::const_byte_span random) {
	generateSalt(random);

	BN_CTX* ctx = BN_CTX_new();
	BN_CTX_init(ctx);
	BIGNUM i_g_a;
	BN_init(&i_g_a);
	BN_set_word(&i_g_a, _dhConfig.g);
	BIGNUM tmp;
	BN_init(&tmp);
	BIGNUM saltBN;
	BN_init(&saltBN);
	BN_bin2bn(reinterpret_cast<const unsigned char*>(_salt.data()), _salt.size(), &saltBN);
	BIGNUM pbytesBN;
	BN_init(&pbytesBN);
	BN_bin2bn(reinterpret_cast<const unsigned char*>(_dhConfig.p.data()), _dhConfig.p.size(), &pbytesBN);
	BN_mod_exp(&tmp, &i_g_a, &saltBN, &pbytesBN, ctx);
	auto g_a_length = BN_num_bytes(&tmp);
	_g_a = std::vector<gsl::byte>(g_a_length, gsl::byte());
	BN_bn2bin(&tmp, reinterpret_cast<unsigned char*>(_g_a.data()));
	constexpr auto kMaxGASize = 256;
	if (_g_a.size() > kMaxGASize) {
		auto slice = gsl::make_span(_g_a).subspan(1, kMaxGASize);
		_g_a = std::vector<gsl::byte>(slice.begin(), slice.end());
	}
	BN_CTX_free(ctx);

	auto randomID = rand_value<int32>();
	auto g_a_hash = std::array<gsl::byte, SHA256_DIGEST_LENGTH>();
	SHA256(reinterpret_cast<const unsigned char*>(_g_a.data()), _g_a.size(), reinterpret_cast<unsigned char*>(g_a_hash.data()));

	request(MTPphone_RequestCall(_user->inputUser, MTP_int(randomID), MTP_bytes(g_a_hash), MTP_phoneCallProtocol(MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p | MTPDphoneCallProtocol::Flag::f_udp_reflector), MTP_int(kMinLayer), MTP_int(kMaxLayer)))).done([this](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);
		auto &call = result.c_phone_phoneCall();
		App::feedUsers(call.vusers);
		if (call.vphone_call.type() != mtpc_phoneCallWaiting) {
			LOG(("API Error: Expected phoneCallWaiting in response to phone.requestCall()"));
			failed();
			return;
		}
		auto &phoneCall = call.vphone_call.c_phoneCallWaiting();
		_id = phoneCall.vid.v;
		_accessHash = phoneCall.vaccess_hash.v;
	}).fail([this](const RPCError &error) {
		failed();
	}).send();
}

bool Call::handleUpdate(const MTPPhoneCall &call) {
	switch (call.type()) {
	case mtpc_phoneCallRequested: Unexpected("phoneCallRequested call inside an existing call handleUpdate()");

	case mtpc_phoneCallEmpty: {
		auto &data = call.c_phoneCallEmpty();
		if (data.vid.v != _id) {
			return false;
		}
		LOG(("Call Error: phoneCallEmpty received."));
		failed();
	} return true;

	case mtpc_phoneCallWaiting: {
		auto &data = call.c_phoneCallWaiting();
		if (data.vid.v != _id) {
			return false;
		}
	} return true;

	case mtpc_phoneCall: {
		auto &data = call.c_phoneCall();
		if (data.vid.v != _id) {
			return false;
		}
	} return true;

	case mtpc_phoneCallDiscarded: {
		auto &data = call.c_phoneCallDiscarded();
		if (data.vid.v != _id) {
			return false;
		}
		_delegate->callFinished(this, data.vreason);
	} return true;

	case mtpc_phoneCallAccepted: {
		auto &data = call.c_phoneCallAccepted();
		if (data.vid.v != _id) {
			return false;
		}
		if (checkCallFields(data)) {
			confirmAcceptedCall(data);
		}
	} return true;
	}

	Unexpected("phoneCall type inside an existing call handleUpdate()");
}

void Call::confirmAcceptedCall(const MTPDphoneCallAccepted &call) {
	// TODO check isGoodGaAndGb

	BN_CTX *ctx = BN_CTX_new();
	BN_CTX_init(ctx);
	BIGNUM p;
	BIGNUM i_authKey;
	BIGNUM res;
	BIGNUM salt;
	BN_init(&p);
	BN_init(&i_authKey);
	BN_init(&res);
	BN_init(&salt);
	BN_bin2bn(reinterpret_cast<const unsigned char*>(_dhConfig.p.data()), _dhConfig.p.size(), &p);
	BN_bin2bn(reinterpret_cast<const unsigned char*>(call.vg_b.v.constData()), call.vg_b.v.length(), &i_authKey);
	BN_bin2bn(reinterpret_cast<const unsigned char*>(_salt.data()), _salt.size(), &salt);

	BN_mod_exp(&res, &i_authKey, &salt, &p, ctx);
	BN_CTX_free(ctx);
	auto realAuthKeyLength = BN_num_bytes(&res);
	auto realAuthKeyBytes = QByteArray(realAuthKeyLength, Qt::Uninitialized);
	BN_bn2bin(&res, reinterpret_cast<unsigned char*>(realAuthKeyBytes.data()));

	if (realAuthKeyLength > kAuthKeySize) {
		memcpy(_authKey.data(), realAuthKeyBytes.constData() + (realAuthKeyLength - kAuthKeySize), kAuthKeySize);
	} else if (realAuthKeyLength < kAuthKeySize) {
		memset(_authKey.data(), 0, kAuthKeySize - realAuthKeyLength);
		memcpy(_authKey.data() + (kAuthKeySize - realAuthKeyLength), realAuthKeyBytes.constData(), realAuthKeyLength);
	} else {
		memcpy(_authKey.data(), realAuthKeyBytes.constData(), kAuthKeySize);
	}

	unsigned char authKeyHash[SHA_DIGEST_LENGTH];
	SHA1(reinterpret_cast<const unsigned char*>(_authKey.data()), _authKey.size(), authKeyHash);

	_keyFingerprint = ((uint64)authKeyHash[19] << 56)
		| ((uint64)authKeyHash[18] << 48)
		| ((uint64)authKeyHash[17] << 40)
		| ((uint64)authKeyHash[16] << 32)
		| ((uint64)authKeyHash[15] << 24)
		| ((uint64)authKeyHash[14] << 16)
		| ((uint64)authKeyHash[13] << 8)
		| ((uint64)authKeyHash[12]);

	request(MTPphone_ConfirmCall(MTP_inputPhoneCall(MTP_long(_id), MTP_long(_accessHash)), MTP_bytes(_g_a), MTP_long(_keyFingerprint), MTP_phoneCallProtocol(MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p | MTPDphoneCallProtocol::Flag::f_udp_reflector), MTP_int(kMinLayer), MTP_int(kMaxLayer)))).done([this](const MTPphone_PhoneCall &result) {
		Expects(result.type() == mtpc_phone_phoneCall);
		auto &call = result.c_phone_phoneCall();
		App::feedUsers(call.vusers);
		if (call.vphone_call.type() != mtpc_phoneCall) {
			LOG(("API Error: Expected phoneCall in response to phone.confirmCall()"));
			failed();
			return;
		}
		createAndStartController(call.vphone_call.c_phoneCall());
	}).fail([this](const RPCError &error) {
		failed();
	}).send();
}

void Call::createAndStartController(const MTPDphoneCall &call) {
	if (!checkCallFields(call)) {
		return;
	}

	voip_config_t config;
	config.data_saving = DATA_SAVING_NEVER;
	config.enableAEC = true;
	config.enableNS = true;
	config.enableAGC = true;
	config.init_timeout = 30;
	config.recv_timeout = 10;

	std::vector<Endpoint> endpoints;
	ConvertEndpoint(endpoints, call.vconnection.c_phoneConnection());
	for (int i = 0; i < call.valternative_connections.v.length(); i++) {
		ConvertEndpoint(endpoints, call.valternative_connections.v[i].c_phoneConnection());
	}

	_controller = std::make_unique<tgvoip::VoIPController>();
	_controller->implData = static_cast<void*>(this);
	_controller->SetRemoteEndpoints(endpoints, true);
	_controller->SetConfig(&config);
	_controller->SetEncryptionKey(reinterpret_cast<char*>(_authKey.data()), true);
	_controller->SetStateCallback([](tgvoip::VoIPController *controller, int state) {
		static_cast<Call*>(controller->implData)->handleControllerStateChange(controller, state);
	});
	_controller->Start();
	_controller->Connect();
}

void Call::handleControllerStateChange(tgvoip::VoIPController *controller, int state) {
	// NB! Can be called from an arbitrary thread!
	Expects(controller == _controller.get());
	Expects(controller->implData == static_cast<void*>(this));

	switch (state) {
	case STATE_WAIT_INIT: {
		DEBUG_LOG(("Call Info: State changed to Established."));
	} break;

	case STATE_WAIT_INIT_ACK: {
		DEBUG_LOG(("Call Info: State changed to Established."));
	} break;

	case STATE_ESTABLISHED: {
		DEBUG_LOG(("Call Info: State changed to Established."));
	} break;

	case STATE_FAILED: {
		DEBUG_LOG(("Call Info: State changed to Failed."));
		failed();
	} break;

	default: LOG(("Call Error: Unexpected state in handleStateChange: %1").arg(state));
	}
}

template <typename Type>
bool Call::checkCallCommonFields(const Type &call) {
	auto checkFailed = [this] {
		failed();
		return false;
	};
	if (call.vaccess_hash.v != _accessHash) {
		LOG(("API Error: Wrong call access_hash."));
		return checkFailed();
	}
	if (call.vadmin_id.v != AuthSession::CurrentUserId()) {
		LOG(("API Error: Wrong call admin_id %1, expected %2.").arg(call.vadmin_id.v).arg(AuthSession::CurrentUserId()));
		return checkFailed();
	}
	if (call.vparticipant_id.v != peerToUser(_user->id)) {
		LOG(("API Error: Wrong call participant_id %1, expected %2.").arg(call.vparticipant_id.v).arg(peerToUser(_user->id)));
		return checkFailed();
	}
	return true;
}

bool Call::checkCallFields(const MTPDphoneCall &call) {
	if (!checkCallCommonFields(call)) {
		return false;
	}
	if (call.vkey_fingerprint.v != _keyFingerprint) {
		LOG(("API Error: Wrong call fingerprint."));
		failed();
		return false;
	}
	return true;
}

bool Call::checkCallFields(const MTPDphoneCallAccepted &call) {
	return checkCallCommonFields(call);
}

void Call::destroyController() {
	if (_controller) {
		DEBUG_LOG(("Call Info: Destroying call controller.."));
		_controller.reset();
		DEBUG_LOG(("Call Info: Call controller destroyed."));
	}
}

void Call::failed() {
	InvokeQueued(this, [this] { _delegate->callFailed(this); });
}

Call::~Call() {
	destroyController();
}

} // namespace Calls
