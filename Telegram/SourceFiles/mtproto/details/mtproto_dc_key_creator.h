/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/auth_key.h"
#include "mtproto/connection_abstract.h"
#include "base/basic_types.h"
#include "base/expected.h"

namespace MTP {
class DcOptions;
} // namespace MTP

namespace MTP::details {

using namespace ::MTP::internal;

class DcKeyCreator final {
public:
	enum class Error {
		UnknownPublicKey,
		Other,
	};
	struct Result {
		AuthKeyPtr key;
		uint64 serverSalt = 0;
	};
	struct Delegate {
		FnMut<void(base::expected<Result, Error>)> done;
		Fn<void(uint64)> sentSome;
		Fn<void()> receivedSome;
	};

	DcKeyCreator(
		DcId dcId,
		int16 protocolDcId,
		not_null<AbstractConnection*> connection,
		not_null<DcOptions*> dcOptions,
		Delegate delegate);
	~DcKeyCreator();

private:
	// Auth key creation fields and methods
	struct Data {
		Data()
		: new_nonce(*(MTPint256*)((uchar*)new_nonce_buf.data()))
		, auth_key_aux_hash(*(MTPlong*)((uchar*)new_nonce_buf.data() + 33)) {
		}
		MTPint128 nonce, server_nonce;

		// 32 bytes new_nonce + 1 check byte + 8 bytes of auth_key_aux_hash.
		bytes::array<41> new_nonce_buf;

		MTPint256 &new_nonce;
		MTPlong &auth_key_aux_hash;

		uint32 retries = 0;
		MTPlong retry_id;

		int32 g = 0;

		bytes::array<32> aesKey;
		bytes::array<32> aesIV;
		MTPlong auth_key_hash;
	};

	template <typename Request>
	void sendNotSecureRequest(const Request &request);

	template <typename Response>
	[[nodiscard]] bool readNotSecureResponse(Response &response);

	void pqSend();
	void pqAnswered();
	void dhParamsAnswered();
	void dhClientParamsSend();
	void dhClientParamsAnswered();

	void failed(Error error = Error::Other);
	void done(uint64 serverSalt);

	const not_null<AbstractConnection*> _connection;
	const not_null<DcOptions*> _dcOptions;
	const DcId _dcId;
	const int16 _protocolDcId = 0;
	Delegate _delegate;

	Data _data;
	bytes::vector _dhPrime;
	bytes::vector _g_a;
	AuthKey::Data _authKey = { { gsl::byte{} } };
	FnMut<void(base::expected<Result, Error>)> _done;

};

} // namespace MTP::details
