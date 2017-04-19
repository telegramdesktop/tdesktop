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
#include "calls/calls_instance.h"

#include "mtproto/connection.h"
#include "auth_session.h"

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

Instance::Instance() = default;

void Instance::startOutgoingCall(gsl::not_null<UserData*> user) {
	if (_controller) {
		return; // Already in a call.
	}

	_controller = std::make_unique<tgvoip::VoIPController>();
	request(MTPmessages_GetDhConfig(MTP_int(_dhConfigVersion), MTP_int(256))).done([this, user](const MTPmessages_DhConfig &result) {
		auto random = QByteArray();
		switch (result.type()) {
		case mtpc_messages_dhConfig: {
			auto &config = result.c_messages_dhConfig();
			if (!MTP::IsPrimeAndGood(config.vp.v, config.vg.v)) {
				LOG(("API Error: bad p/g received in dhConfig."));
				callFailed();
				return;
			}
			_dhConfigG = config.vg.v;
			_dhConfigP = config.vp.v;
			random = qba(config.vrandom);
		} break;

		case mtpc_messages_dhConfigNotModified: {
			auto &config = result.c_messages_dhConfigNotModified();
			random = qba(config.vrandom);
			if (!_dhConfigG || _dhConfigP.isEmpty()) {
				LOG(("API Error: dhConfigNotModified on zero version."));
				callFailed();
				return;
			}
		} break;

		default: Unexpected("Type in messages.getDhConfig");
		}
		if (random.size() != kSaltSize) {
			LOG(("API Error: dhConfig random bytes wrong size: %1").arg(random.size()));
			callFailed();
			return;
		}

		auto randomBytes = reinterpret_cast<const unsigned char*>(random.constData());
		RAND_bytes(_salt.data(), kSaltSize);
		for (auto i = 0; i != kSaltSize; i++) {
			_salt[i] ^= randomBytes[i];
		}
		BN_CTX* ctx = BN_CTX_new();
		BN_CTX_init(ctx);
		BIGNUM i_g_a;
		BN_init(&i_g_a);
		BN_set_word(&i_g_a, _dhConfigG);
		BIGNUM tmp;
		BN_init(&tmp);
		BIGNUM saltBN;
		BN_init(&saltBN);
		BN_bin2bn(_salt.data(), kSaltSize, &saltBN);
		BIGNUM pbytesBN;
		BN_init(&pbytesBN);
		BN_bin2bn(reinterpret_cast<const unsigned char*>(_dhConfigP.constData()), _dhConfigP.size(), &pbytesBN);
		BN_mod_exp(&tmp, &i_g_a, &saltBN, &pbytesBN, ctx);
		auto g_a_length = BN_num_bytes(&tmp);
		_g_a = QByteArray(g_a_length, Qt::Uninitialized);
		BN_bn2bin(&tmp, reinterpret_cast<unsigned char*>(_g_a.data()));
		constexpr auto kMaxGASize = 256;
		if (g_a_length > kMaxGASize) {
			_g_a = _g_a.mid(1, kMaxGASize);
		}
		BN_CTX_free(ctx);

		auto randomID = rand_value<int32>();
		QByteArray g_a_hash;
		g_a_hash.resize(SHA256_DIGEST_LENGTH);
		SHA256(reinterpret_cast<const unsigned char*>(_g_a.constData()), _g_a.size(), reinterpret_cast<unsigned char*>(g_a_hash.data()));

		request(MTPphone_RequestCall(user->inputUser, MTP_int(randomID), MTP_bytes(g_a_hash), MTP_phoneCallProtocol(MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p | MTPDphoneCallProtocol::Flag::f_udp_reflector), MTP_int(kMinLayer), MTP_int(kMaxLayer)))).done([this](const MTPphone_PhoneCall &result) {
			Expects(result.type() == mtpc_phone_phoneCall);
			auto &call = result.c_phone_phoneCall();
			App::feedUsers(call.vusers);
			if (call.vphone_call.type() != mtpc_phoneCallWaiting) {
				LOG(("API Error: Expected phoneCallWaiting in response to phone.requestCall()"));
				callFailed();
				return;
			}
			auto &phoneCall = call.vphone_call.c_phoneCallWaiting();
			_callId = phoneCall.vid.v;
			_accessHash = phoneCall.vaccess_hash.v;
		}).fail([this](const RPCError &error) {
			callFailed();
		}).send();
	}).fail([this](const RPCError &error) {
		callFailed();
	}).send();
}

void Instance::handleUpdate(const MTPDupdatePhoneCall& update) {
	// TODO check call id
	switch (update.vphone_call.type()) {
	case mtpc_phoneCallAccepted: {
		// state changed STATE_EXCHANGING_KEYS
		auto &call = update.vphone_call.c_phoneCallAccepted();
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
		BN_bin2bn(reinterpret_cast<const unsigned char*>(_dhConfigP.constData()), _dhConfigP.size(), &p);
		BN_bin2bn((const unsigned char*)call.vg_b.v.constData(), call.vg_b.v.length(), &i_authKey);
		BN_bin2bn(_salt.data(), kSaltSize, &salt);

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
		SHA1(_authKey.data(), _authKey.size(), authKeyHash);

		_keyFingerprint = ((uint64)authKeyHash[19] << 56)
			| ((uint64)authKeyHash[18] << 48)
			| ((uint64)authKeyHash[17] << 40)
			| ((uint64)authKeyHash[16] << 32)
			| ((uint64)authKeyHash[15] << 24)
			| ((uint64)authKeyHash[14] << 16)
			| ((uint64)authKeyHash[13] << 8)
			| ((uint64)authKeyHash[12]);

		request(MTPphone_ConfirmCall(MTP_inputPhoneCall(MTP_long(_callId), MTP_long(_accessHash)), MTP_bytes(_g_a), MTP_long(_keyFingerprint), MTP_phoneCallProtocol(MTP_flags(MTPDphoneCallProtocol::Flag::f_udp_p2p | MTPDphoneCallProtocol::Flag::f_udp_reflector), MTP_int(kMinLayer), MTP_int(kMaxLayer)))).done([this](const MTPphone_PhoneCall &result) {
			auto &call = result.c_phone_phoneCall().vphone_call.c_phoneCall();

			std::vector<Endpoint> endpoints;
			ConvertEndpoint(endpoints, call.vconnection.c_phoneConnection());
			for (int i = 0; i < call.valternative_connections.v.length(); i++) {
				ConvertEndpoint(endpoints, call.valternative_connections.v[i].c_phoneConnection());
			}
			_controller->SetRemoteEndpoints(endpoints, true);

			initiateActualCall();
		}).fail([this](const RPCError &error) {
			callFailed();
		}).send();
	} break;
	}
}

void Instance::callFailed() {
	InvokeQueued(this, [this] {
		_controller.reset();
	});
}

void Instance::initiateActualCall() {
	voip_config_t config;
	config.data_saving = DATA_SAVING_NEVER;
	config.enableAEC = true;
	config.enableNS = true;
	config.enableAGC = true;
	config.init_timeout = 30;
	config.recv_timeout = 10;
	_controller->SetConfig(&config);
	_controller->SetEncryptionKey(reinterpret_cast<char*>(_authKey.data()), true);
	_controller->Start();
	_controller->Connect();
}

Instance::~Instance() = default;

Instance &Current() {
	return AuthSession::Current().calls();
}

} // namespace Calls
