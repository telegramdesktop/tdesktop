/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_dc_key_binder.h"

#include "mtproto/details/mtproto_serialized_request.h"
#include "mtproto/mtp_instance.h"
#include "base/unixtime.h"
#include "base/openssl_help.h"
#include "scheme.h"

#include <QtCore/QPointer>

namespace MTP::details {
namespace {

[[nodiscard]] QByteArray EncryptBindAuthKeyInner(
		const AuthKeyPtr &persistentKey,
		mtpMsgId realMsgId,
		const MTPBindAuthKeyInner &data) {
	auto serialized = SerializedRequest::Serialize(data);
	serialized.setMsgId(realMsgId);
	serialized.setSeqNo(0);
	serialized.addPadding(true);

	constexpr auto kMsgIdPosition = SerializedRequest::kMessageIdPosition;
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

DcKeyBinder::DcKeyBinder(AuthKeyPtr &&persistentKey)
: _persistentKey(std::move(persistentKey)) {
	Expects(_persistentKey != nullptr);
}

SerializedRequest DcKeyBinder::prepareRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId) {
	Expects(temporaryKey != nullptr);
	Expects(temporaryKey->expiresAt() != 0);

	const auto nonce = openssl::RandomValue<uint64>();
	const auto msgId = base::unixtime::mtproto_msg_id();
	auto result = SerializedRequest::Serialize(MTPauth_BindTempAuthKey(
		MTP_long(_persistentKey->keyId()),
		MTP_long(nonce),
		MTP_int(temporaryKey->expiresAt()),
		MTP_bytes(EncryptBindAuthKeyInner(
			_persistentKey,
			msgId,
			MTP_bind_auth_key_inner(
				MTP_long(nonce),
				MTP_long(temporaryKey->keyId()),
				MTP_long(_persistentKey->keyId()),
				MTP_long(sessionId),
				MTP_int(temporaryKey->expiresAt()))))));
	result.setMsgId(msgId);
	return result;
}

DcKeyBindState DcKeyBinder::handleResponse(const mtpBuffer &response) {
	Expects(!response.isEmpty());

	auto from = response.begin();
	const auto end = from + response.size();
	auto error = MTPRpcError();
	if (response[0] == mtpc_boolTrue) {
		return DcKeyBindState::Success;
	} else if (response[0] == mtpc_rpc_error && error.read(from, end)) {
		const auto destroyed = error.match([&](const MTPDrpc_error &data) {
			return (data.verror_code().v == 400)
				&& (data.verror_message().v == "ENCRYPTED_MESSAGE_INVALID");
		});
		return destroyed
			? DcKeyBindState::DefinitelyDestroyed
			: DcKeyBindState::Failed;
	} else {
		return DcKeyBindState::Failed;
	}
}

AuthKeyPtr DcKeyBinder::persistentKey() const {
	return _persistentKey;
}

} // namespace MTP::details
