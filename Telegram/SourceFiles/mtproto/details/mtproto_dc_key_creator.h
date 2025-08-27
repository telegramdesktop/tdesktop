/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/connection_abstract.h"
#include "base/basic_types.h"
#include "base/expected.h"

namespace MTP {
class DcOptions;
} // namespace MTP

namespace MTP::details {

struct DcKeyRequest {
	TimeId temporaryExpiresIn = 0;
	bool persistentNeeded = false;
};

enum class DcKeyError {
	UnknownPublicKey,
	Other,
};

struct DcKeyResult {
	AuthKeyPtr persistentKey;
	AuthKeyPtr temporaryKey;
	uint64 temporaryServerSalt = 0;
	uint64 persistentServerSalt = 0;
};

class DcKeyCreator final {
public:
	struct Delegate {
		Fn<void(base::expected<DcKeyResult, DcKeyError>)> done;
		Fn<void(uint64)> sentSome;
		Fn<void()> receivedSome;
	};

	DcKeyCreator(
		DcId dcId,
		int16 protocolDcId,
		not_null<AbstractConnection*> connection,
		not_null<DcOptions*> dcOptions,
		Delegate delegate,
		DcKeyRequest request);
	~DcKeyCreator();

private:
	enum class Stage {
		None,
		WaitingPQ,
		WaitingDH,
		WaitingDone,
		Ready,
	};
	struct Data {
		Data()
		: new_nonce(*(MTPint256*)((uchar*)new_nonce_buf.data()))
		, auth_key_aux_hash(*(MTPlong*)((uchar*)new_nonce_buf.data() + 33)) {
		}
		MTPint128 nonce, server_nonce;

		// 32 bytes new_nonce + 1 check byte + 8 bytes of auth_key_aux_hash.
		bytes::array<41> new_nonce_buf{};

		MTPint256 &new_nonce;
		MTPlong &auth_key_aux_hash;

		MTPlong retry_id;

		int32 g = 0;

		bytes::array<32> aesKey;
		bytes::array<32> aesIV;
		MTPlong auth_key_hash;
		uint64 doneSalt = 0;
	};
	struct Attempt {
		~Attempt();

		Data data;
		bytes::vector dhPrime;
		bytes::vector g_a;
		AuthKey::Data authKey = { { gsl::byte{} } };
		TimeId expiresIn = 0;
		uint32 retries = 0;
		Stage stage = Stage::None;
	};

	template <typename RequestType>
	void sendNotSecureRequest(const RequestType &request);

	template <
		typename RequestType,
		typename Response = typename RequestType::ResponseType>
	[[nodiscard]] std::optional<Response> readNotSecureResponse(
			gsl::span<const mtpPrime> answer);

	Attempt *attemptByNonce(const MTPint128 &nonce);

	void answered();
	void handleAnswer(gsl::span<const mtpPrime> answer);
	void pqSend(not_null<Attempt*> attempt, TimeId expiresIn);
	void pqAnswered(
		not_null<Attempt*> attempt,
		const MTPresPQ &data);
	void dhParamsAnswered(
		not_null<Attempt*> attempt,
		const MTPserver_DH_Params &data);
	void dhClientParamsSend(not_null<Attempt*> attempt);
	void dhClientParamsAnswered(
		not_null<Attempt*> attempt,
		const MTPset_client_DH_params_answer &data);

	void stopReceiving();
	void failed(DcKeyError error = DcKeyError::Other);
	void done();

	const not_null<AbstractConnection*> _connection;
	const not_null<DcOptions*> _dcOptions;
	const DcId _dcId = 0;
	const int16 _protocolDcId = 0;
	const DcKeyRequest _request;
	Delegate _delegate;

	Attempt _temporary;
	Attempt _persistent;

};

} // namespace MTP::details
