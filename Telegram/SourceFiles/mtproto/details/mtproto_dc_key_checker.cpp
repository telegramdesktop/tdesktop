/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_dc_key_checker.h"

#include "mtproto/mtp_instance.h"
#include "base/unixtime.h"
#include "base/openssl_help.h"
#include "scheme.h"

#include <QtCore/QPointer>

namespace MTP::details {
namespace {

constexpr auto kBindKeyExpireTimeout = TimeId(3600);

[[nodiscard]] QByteArray EncryptBindAuthKeyInner(
		const AuthKeyPtr &persistentKey,
		mtpMsgId realMsgId,
		const MTPBindAuthKeyInner &data) {
	auto serialized = SecureRequest::Serialize(data);
	serialized.setMsgId(realMsgId);
	serialized.setSeqNo(0);
	serialized.addPadding(false, true);

	constexpr auto kMsgIdPosition = SecureRequest::kMessageIdPosition;
	constexpr auto kMinMessageSize = 5;

	const auto sizeInPrimes = serialized->size();
	const auto messageSize = serialized.messageSize();
	Assert(messageSize >= kMinMessageSize);
	Assert(sizeInPrimes >= kMsgIdPosition + messageSize);

	const auto sizeInBytes = sizeInPrimes * sizeof(mtpPrime);
	const auto padding = sizeInBytes
		- (kMsgIdPosition + messageSize) * sizeof(mtpPrime);

	// session_id, salt - just random here.
	bytes::set_random(bytes::make_span(*serialized).subspan(
		0,
		kMsgIdPosition * sizeof(mtpPrime)));

	const auto hash = openssl::Sha1(bytes::make_span(*serialized).subspan(
		0,
		sizeInBytes - padding));
	auto msgKey = MTPint128();
	bytes::copy(
		bytes::object_as_span(&msgKey),
		bytes::make_span(hash).subspan(4));

	constexpr auto kAuthKeyIdBytes = 2 * sizeof(mtpPrime);
	constexpr auto kMessageKeyPosition = kAuthKeyIdBytes;
	constexpr auto kMessageKeyBytes = 4 * sizeof(mtpPrime);
	constexpr auto kPrefix = (kAuthKeyIdBytes + kMessageKeyBytes);
	auto encrypted = QByteArray(kPrefix + sizeInBytes, Qt::Uninitialized);
	*reinterpret_cast<uint64*>(encrypted.data()) = persistentKey->keyId();
	*reinterpret_cast<MTPint128*>(encrypted.data() + kMessageKeyPosition)
		= msgKey;

	aesIgeEncrypt_oldmtp(
		serialized->constData(),
		encrypted.data() + kPrefix,
		sizeInBytes,
		persistentKey,
		msgKey);

	return encrypted;
}

} // namespace

DcKeyChecker::DcKeyChecker(
	not_null<Instance*> instance,
	ShiftedDcId shiftedDcId,
	const AuthKeyPtr &persistentKey)
: _instance(instance)
, _shiftedDcId(shiftedDcId)
, _persistentKey(persistentKey) {
}

SecureRequest DcKeyChecker::prepareRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId) {
	Expects(_requestMsgId == 0);

	const auto nonce = openssl::RandomValue<uint64>();
	_requestMsgId = base::unixtime::mtproto_msg_id();
	auto result = SecureRequest::Serialize(MTPauth_BindTempAuthKey(
		MTP_long(_persistentKey->keyId()),
		MTP_long(nonce),
		MTP_int(kBindKeyExpireTimeout),
		MTP_bytes(EncryptBindAuthKeyInner(
			_persistentKey,
			_requestMsgId,
			MTP_bind_auth_key_inner(
				MTP_long(nonce),
				MTP_long(temporaryKey->keyId()),
				MTP_long(_persistentKey->keyId()),
				MTP_long(sessionId),
				MTP_int(kBindKeyExpireTimeout))))));
	result.setMsgId(_requestMsgId);
	return result;
}

bool DcKeyChecker::handleResponse(
		MTPlong requestMsgId,
		const mtpBuffer &response) {
	Expects(!response.isEmpty());

	if (!_requestMsgId || requestMsgId.v != _requestMsgId) {
		return false;
	}

	const auto destroyed = [&] {
		if (response[0] != mtpc_rpc_error) {
			return false;
		}
		auto error = MTPRpcError();
		auto from = response.begin();
		const auto end = from + response.size();
		if (!error.read(from, end)) {
			return false;
		}
		return error.match([&](const MTPDrpc_error &data) {
			return (data.verror_code().v == 400)
				&& (data.verror_message().v == "ENCRYPTED_MESSAGE_INVALID");
		});
	}();

	const auto instance = _instance;
	const auto shiftedDcId = _shiftedDcId;
	const auto keyId = _persistentKey->keyId();
	_persistentKey->setLastCheckTime(crl::now());
	crl::on_main(instance, [=] {
		instance->killSession(shiftedDcId);
		if (destroyed) {
			instance->keyDestroyedOnServer(BareDcId(shiftedDcId), keyId);
		}
	});

	_requestMsgId = 0;
	return true;
}

} // namespace MTP::details
