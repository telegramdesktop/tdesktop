/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_dc_key_creator.h"

#include "mtproto/details/mtproto_rsa_public_key.h"
#include "mtproto/connection_abstract.h"
#include "mtproto/mtproto_dh_utils.h"
#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "scheme.h"
#include "logs.h"

#include <cmath>

namespace MTP::details {
namespace {

struct ParsedPQ {
	QByteArray p;
	QByteArray q;
};

[[nodiscard]] ParsedPQ ParsePQ(const QByteArray &pqStr) {
	if (pqStr.length() > 8) {
		// More than 64 bit pq.
		return ParsedPQ();
	}

	uint64 pq = 0, p, q;
	const uchar *pqChars = (const uchar*)pqStr.constData();
	for (uint32 i = 0, l = pqStr.length(); i < l; ++i) {
		pq <<= 8;
		pq |= (uint64)pqChars[i];
	}
	uint64 pqSqrt = (uint64)sqrtl((long double)pq), ySqr, y;
	while (pqSqrt * pqSqrt > pq) --pqSqrt;
	while (pqSqrt * pqSqrt < pq) ++pqSqrt;
	for (ySqr = pqSqrt * pqSqrt - pq; ; ++pqSqrt, ySqr = pqSqrt * pqSqrt - pq) {
		y = (uint64)sqrtl((long double)ySqr);
		while (y * y > ySqr) --y;
		while (y * y < ySqr) ++y;
		if (!ySqr || y + pqSqrt >= pq) {
			return ParsedPQ();
		}
		if (y * y == ySqr) {
			p = pqSqrt + y;
			q = (pqSqrt > y) ? (pqSqrt - y) : (y - pqSqrt);
			break;
		}
	}
	if (p > q) std::swap(p, q);

	auto pStr = QByteArray(4, Qt::Uninitialized);
	uchar *pChars = (uchar*)pStr.data();
	for (uint32 i = 0; i < 4; ++i) {
		*(pChars + 3 - i) = (uchar)(p & 0xFF);
		p >>= 8;
	}

	auto qStr = QByteArray(4, Qt::Uninitialized);
	uchar *qChars = (uchar*)qStr.data();
	for (uint32 i = 0; i < 4; ++i) {
		*(qChars + 3 - i) = (uchar)(q & 0xFF);
		q >>= 8;
	}

	return { pStr, qStr };
}

template <typename PQInnerData>
[[nodiscard]] bytes::vector EncryptPQInnerRSA(
		const PQInnerData &data,
		const RSAPublicKey &key) {
	constexpr auto kSkipPrimes = 6;
	constexpr auto kMaxPrimes = 65; // 260 bytes

	using BoxedPQInnerData = std::conditional_t<
		tl::is_boxed_v<PQInnerData>,
		PQInnerData,
		tl::boxed<PQInnerData>>;
	const auto boxed = BoxedPQInnerData(data);
	const auto p_q_inner_size = tl::count_length(boxed);
	const auto sizeInPrimes = (p_q_inner_size >> 2) + kSkipPrimes;
	if (sizeInPrimes >= kMaxPrimes) {
		auto tmp = mtpBuffer();
		tmp.reserve(sizeInPrimes);
		boxed.write(tmp);
		LOG(("AuthKey Error: too large data for RSA encrypt, size %1").arg(sizeInPrimes * sizeof(mtpPrime)));
		DEBUG_LOG(("AuthKey Error: bad data for RSA encrypt %1").arg(Logs::mb(&tmp[0], tmp.size() * 4).str()));
		return {}; // can't be 255-byte string
	}

	auto encBuffer = mtpBuffer();
	encBuffer.reserve(kMaxPrimes);
	encBuffer.resize(kSkipPrimes);
	boxed.write(encBuffer);
	encBuffer.resize(kMaxPrimes);
	const auto bytes = bytes::make_span(encBuffer);

	const auto hashSrc = bytes.subspan(
		kSkipPrimes * sizeof(mtpPrime),
		p_q_inner_size);
	bytes::copy(bytes.subspan(sizeof(mtpPrime)), openssl::Sha1(hashSrc));
	bytes::set_random(bytes.subspan(sizeInPrimes * sizeof(mtpPrime)));

	const auto bytesToEncrypt = bytes.subspan(3, 256);
	return key.encrypt(bytesToEncrypt);
}

[[nodiscard]] std::string EncryptClientDHInner(
		const MTPClient_DH_Inner_Data &data,
		const void *aesKey,
		const void *aesIV) {
	constexpr auto kSkipPrimes = openssl::kSha1Size / sizeof(mtpPrime);

	auto client_dh_inner_size = tl::count_length(data);
	auto encSize = (client_dh_inner_size >> 2) + kSkipPrimes;
	auto encFullSize = encSize;
	if (encSize & 0x03) {
		encFullSize += 4 - (encSize & 0x03);
	}

	auto encBuffer = mtpBuffer();
	encBuffer.reserve(encFullSize);
	encBuffer.resize(kSkipPrimes);
	data.write(encBuffer);
	encBuffer.resize(encFullSize);

	const auto bytes = bytes::make_span(encBuffer);

	const auto hash = openssl::Sha1(bytes.subspan(
		kSkipPrimes * sizeof(mtpPrime),
		client_dh_inner_size));
	bytes::copy(bytes, hash);
	bytes::set_random(bytes.subspan(encSize * sizeof(mtpPrime)));

	auto sdhEncString = std::string(encFullSize * 4, ' ');

	aesIgeEncryptRaw(&encBuffer[0], &sdhEncString[0], encFullSize * sizeof(mtpPrime), aesKey, aesIV);

	return sdhEncString;
}

// 128 lower-order bits of SHA1.
MTPint128 NonceDigest(bytes::const_span data) {
	const auto hash = openssl::Sha1(data);
	return *(MTPint128*)(hash.data() + 4);
}

} // namespace

DcKeyCreator::Attempt::~Attempt() {
	const auto clearBytes = [](bytes::span bytes) {
		OPENSSL_cleanse(bytes.data(), bytes.size());
	};
	OPENSSL_cleanse(&data, sizeof(data));
	clearBytes(dhPrime);
	clearBytes(g_a);
	clearBytes(authKey);
}

DcKeyCreator::DcKeyCreator(
	DcId dcId,
	int16 protocolDcId,
	not_null<AbstractConnection*> connection,
	not_null<DcOptions*> dcOptions,
	Delegate delegate,
	DcKeyRequest request)
: _connection(connection)
, _dcOptions(dcOptions)
, _dcId(dcId)
, _protocolDcId(protocolDcId)
, _request(request)
, _delegate(std::move(delegate)) {
	Expects(_request.temporaryExpiresIn > 0);
	Expects(_delegate.done != nullptr);

	QObject::connect(_connection, &AbstractConnection::receivedData, [=] {
		answered();
	});

	if (_request.persistentNeeded) {
		pqSend(&_persistent, 0);
	} else {
		pqSend(&_temporary, _request.temporaryExpiresIn);
	}
}

DcKeyCreator::~DcKeyCreator() {
	if (_delegate.done) {
		stopReceiving();
	}
}

template <typename RequestType>
void DcKeyCreator::sendNotSecureRequest(const RequestType &request) {
	auto packet = _connection->prepareNotSecurePacket(
		request,
		base::unixtime::mtproto_msg_id());

	DEBUG_LOG(("AuthKey Info: sending request, size: %1, time: %3"
		).arg(packet.size() - 8
		).arg(packet[5]));

	const auto bytesSize = packet.size() * sizeof(mtpPrime);

	_connection->sendData(std::move(packet));

	if (_delegate.sentSome) {
		_delegate.sentSome(bytesSize);
	}
}

template <typename RequestType, typename Response>
std::optional<Response> DcKeyCreator::readNotSecureResponse(
		gsl::span<const mtpPrime> answer) {
	auto from = answer.data();
	auto result = Response();
	if (result.read(from, from + answer.size())) {
		return result;
	}
	return std::nullopt;
}

void DcKeyCreator::answered() {
	if (_delegate.receivedSome) {
		_delegate.receivedSome();
	}

	if (_connection->received().empty()) {
		LOG(("AuthKey Error: "
			"trying to read response from empty received list"));
		return failed();
	}

	const auto buffer = std::move(_connection->received().front());
	_connection->received().pop_front();

	const auto answer = _connection->parseNotSecureResponse(buffer);
	if (answer.empty()) {
		return failed();
	}

	handleAnswer(answer);
}

DcKeyCreator::Attempt *DcKeyCreator::attemptByNonce(const MTPint128 &nonce) {
	if (_temporary.data.nonce == nonce) {
		DEBUG_LOG(("AuthKey Info: receiving answer for temporary..."));
		return &_temporary;
	} else if (_persistent.data.nonce == nonce) {
		DEBUG_LOG(("AuthKey Info: receiving answer for persistent..."));
		return &_persistent;
	}
	LOG(("AuthKey Error: attempt by nonce not found."));
	return nullptr;
}

void DcKeyCreator::handleAnswer(gsl::span<const mtpPrime> answer) {
	if (const auto resPQ = readNotSecureResponse<MTPReq_pq>(answer)) {
		const auto nonce = resPQ->match([](const auto &data) {
			return data.vnonce();
		});
		if (const auto attempt = attemptByNonce(nonce)) {
			DEBUG_LOG(("AuthKey Info: receiving Req_pq answer..."));
			return pqAnswered(attempt, *resPQ);
		}
	} else if (const auto resDH = readNotSecureResponse<MTPReq_DH_params>(answer)) {
		const auto nonce = resDH->match([](const auto &data) {
			return data.vnonce();
		});
		if (const auto attempt = attemptByNonce(nonce)) {
			DEBUG_LOG(("AuthKey Info: receiving Req_DH_params answer..."));
			return dhParamsAnswered(attempt, *resDH);
		}
	} else if (const auto result = readNotSecureResponse<MTPSet_client_DH_params>(answer)) {
		const auto nonce = result->match([](const auto &data) {
			return data.vnonce();
		});
		if (const auto attempt = attemptByNonce(nonce)) {
			DEBUG_LOG(("AuthKey Info: receiving Req_client_DH_params answer..."));
			return dhClientParamsAnswered(attempt, *result);
		}
	}
	LOG(("AuthKey Error: Unknown answer received."));
	failed();
}

void DcKeyCreator::pqSend(not_null<Attempt*> attempt, TimeId expiresIn) {
	DEBUG_LOG(("AuthKey Info: sending Req_pq for %1..."
		).arg(expiresIn ? "temporary" : "persistent"));
	attempt->stage = Stage::WaitingPQ;
	attempt->expiresIn = expiresIn;
	attempt->data.nonce = openssl::RandomValue<MTPint128>();
	sendNotSecureRequest(MTPReq_pq_multi(attempt->data.nonce));
}

void DcKeyCreator::pqAnswered(
		not_null<Attempt*> attempt,
		const MTPresPQ &data) {
	data.match([&](const MTPDresPQ &data) {
		Expects(data.vnonce() == attempt->data.nonce);

		if (attempt->stage != Stage::WaitingPQ) {
			LOG(("AuthKey Error: Unexpected stage %1").arg(int(attempt->stage)));
			return failed();
		}
		const auto rsaKey = _dcOptions->getDcRSAKey(
			_dcId,
			data.vserver_public_key_fingerprints().v);
		if (!rsaKey.valid()) {
			return failed(DcKeyError::UnknownPublicKey);
		}

		attempt->data.server_nonce = data.vserver_nonce();
		attempt->data.new_nonce = openssl::RandomValue<MTPint256>();

		const auto &pq = data.vpq().v;
		const auto parsed = ParsePQ(data.vpq().v);
		if (parsed.p.isEmpty() || parsed.q.isEmpty()) {
			LOG(("AuthKey Error: could not factor pq!"));
			DEBUG_LOG(("AuthKey Error: problematic pq: %1").arg(Logs::mb(pq.constData(), pq.length()).str()));
			return failed();
		}

		const auto dhEncString = [&] {
			return (attempt->expiresIn == 0)
				? EncryptPQInnerRSA(
					MTP_p_q_inner_data_dc(
						data.vpq(),
						MTP_bytes(parsed.p),
						MTP_bytes(parsed.q),
						attempt->data.nonce,
						attempt->data.server_nonce,
						attempt->data.new_nonce,
						MTP_int(_protocolDcId)),
					rsaKey)
				: EncryptPQInnerRSA(
					MTP_p_q_inner_data_temp_dc(
						data.vpq(),
						MTP_bytes(parsed.p),
						MTP_bytes(parsed.q),
						attempt->data.nonce,
						attempt->data.server_nonce,
						attempt->data.new_nonce,
						MTP_int(_protocolDcId),
						MTP_int(attempt->expiresIn)),
					rsaKey);
		}();
		if (dhEncString.empty()) {
			return failed();
		}

		attempt->stage = Stage::WaitingDH;
		DEBUG_LOG(("AuthKey Info: sending Req_DH_params..."));
		sendNotSecureRequest(MTPReq_DH_params(
			attempt->data.nonce,
			attempt->data.server_nonce,
			MTP_bytes(parsed.p),
			MTP_bytes(parsed.q),
			MTP_long(rsaKey.fingerprint()),
			MTP_bytes(dhEncString)));
	});
}

void DcKeyCreator::dhParamsAnswered(
		not_null<Attempt*> attempt,
		const MTPserver_DH_Params &data) {
	if (attempt->stage != Stage::WaitingDH) {
		LOG(("AuthKey Error: Unexpected stage %1").arg(int(attempt->stage)));
		return failed();
	}
	data.match([&](const MTPDserver_DH_params_ok &data) {
		Expects(data.vnonce() == attempt->data.nonce);

		if (data.vserver_nonce() != attempt->data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_params_ok)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&data.vserver_nonce(), 16).str(), Logs::mb(&attempt->data.server_nonce, 16).str()));
			return failed();
		}

		auto &encDHStr = data.vencrypted_answer().v;
		uint32 encDHLen = encDHStr.length(), encDHBufLen = encDHLen >> 2;
		if ((encDHLen & 0x03) || encDHBufLen < 6) {
			LOG(("AuthKey Error: bad encrypted data length %1 (in server_DH_params_ok)!").arg(encDHLen));
			DEBUG_LOG(("AuthKey Error: received encrypted data %1").arg(Logs::mb(encDHStr.constData(), encDHLen).str()));
			return failed();
		}

		const auto nlen = sizeof(attempt->data.new_nonce);
		const auto slen = sizeof(attempt->data.server_nonce);
		auto tmp_aes_buffer = bytes::array<1024>();
		const auto tmp_aes = bytes::make_span(tmp_aes_buffer);
		bytes::copy(tmp_aes, bytes::object_as_span(&attempt->data.new_nonce));
		bytes::copy(tmp_aes.subspan(nlen), bytes::object_as_span(&attempt->data.server_nonce));
		bytes::copy(tmp_aes.subspan(nlen + slen), bytes::object_as_span(&attempt->data.new_nonce));
		bytes::copy(tmp_aes.subspan(nlen + slen + nlen), bytes::object_as_span(&attempt->data.new_nonce));
		const auto sha1ns = openssl::Sha1(tmp_aes.subspan(0, nlen + slen));
		const auto sha1sn = openssl::Sha1(tmp_aes.subspan(nlen, nlen + slen));
		const auto sha1nn = openssl::Sha1(tmp_aes.subspan(nlen + slen, nlen + nlen));

		mtpBuffer decBuffer;
		decBuffer.resize(encDHBufLen);

		const auto aesKey = bytes::make_span(attempt->data.aesKey);
		const auto aesIV = bytes::make_span(attempt->data.aesIV);
		bytes::copy(aesKey, bytes::make_span(sha1ns).subspan(0, 20));
		bytes::copy(aesKey.subspan(20), bytes::make_span(sha1sn).subspan(0, 12));
		bytes::copy(aesIV, bytes::make_span(sha1sn).subspan(12, 8));
		bytes::copy(aesIV.subspan(8), bytes::make_span(sha1nn).subspan(0, 20));
		bytes::copy(aesIV.subspan(28), bytes::object_as_span(&attempt->data.new_nonce).subspan(0, 4));

		aesIgeDecryptRaw(encDHStr.constData(), &decBuffer[0], encDHLen, aesKey.data(), aesIV.data());

		const mtpPrime *from(&decBuffer[5]), *to(from), *end(from + (encDHBufLen - 5));
		MTPServer_DH_inner_data dh_inner;
		if (!dh_inner.read(to, end)) {
			LOG(("AuthKey Error: could not decrypt server_DH_inner_data!"));
			return failed();
		}
		const auto &dh_inner_data(dh_inner.c_server_DH_inner_data());
		if (dh_inner_data.vnonce() != attempt->data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in server_DH_inner_data)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&dh_inner_data.vnonce(), 16).str(), Logs::mb(&attempt->data.nonce, 16).str()));
			return failed();
		}
		if (dh_inner_data.vserver_nonce() != attempt->data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_inner_data)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&dh_inner_data.vserver_nonce(), 16).str(), Logs::mb(&attempt->data.server_nonce, 16).str()));
			return failed();
		}
		const auto sha1Buffer = openssl::Sha1(
			bytes::make_span(decBuffer).subspan(
				5 * sizeof(mtpPrime),
				(to - from) * sizeof(mtpPrime)));
		const auto sha1Dec = bytes::make_span(decBuffer).subspan(
			0,
			openssl::kSha1Size);
		if (bytes::compare(sha1Dec, sha1Buffer)) {
			LOG(("AuthKey Error: sha1 hash of encrypted part did not match!"));
			DEBUG_LOG(("AuthKey Error: sha1 did not match, server_nonce: %1, new_nonce %2, encrypted data %3").arg(Logs::mb(&attempt->data.server_nonce, 16).str(), Logs::mb(&attempt->data.new_nonce, 16).str(), Logs::mb(encDHStr.constData(), encDHLen).str()));
			return failed();
		}
		base::unixtime::update(dh_inner_data.vserver_time().v);

		// check that dhPrime and (dhPrime - 1) / 2 are really prime
		if (!IsPrimeAndGood(bytes::make_span(dh_inner_data.vdh_prime().v), dh_inner_data.vg().v)) {
			LOG(("AuthKey Error: bad dh_prime primality!"));
			return failed();
		}

		attempt->dhPrime = bytes::make_vector(
			dh_inner_data.vdh_prime().v);
		attempt->data.g = dh_inner_data.vg().v;
		attempt->g_a = bytes::make_vector(dh_inner_data.vg_a().v);
		attempt->data.retry_id = MTP_long(0);
		attempt->retries = 0;
		dhClientParamsSend(attempt);
	}, [&](const MTPDserver_DH_params_fail &data) {
		Expects(data.vnonce() == attempt->data.nonce);

		if (data.vserver_nonce() != attempt->data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_params_fail)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&data.vserver_nonce(), 16).str(), Logs::mb(&attempt->data.server_nonce, 16).str()));
			return failed();
		}
		if (data.vnew_nonce_hash() != NonceDigest(bytes::object_as_span(&attempt->data.new_nonce))) {
			LOG(("AuthKey Error: received new_nonce_hash did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash: %1, new_nonce: %2").arg(Logs::mb(&data.vnew_nonce_hash(), 16).str(), Logs::mb(&attempt->data.new_nonce, 32).str()));
			return failed();
		}
		LOG(("AuthKey Error: server_DH_params_fail received!"));
		failed();
	});
}

void DcKeyCreator::dhClientParamsSend(not_null<Attempt*> attempt) {
	if (++attempt->retries > 5) {
		LOG(("AuthKey Error: could not create auth_key for %1 retries").arg(attempt->retries - 1));
		return failed();
	}

	// gen rand 'b'
	auto randomSeed = bytes::vector(ModExpFirst::kRandomPowerSize);
	bytes::set_random(randomSeed);
	auto g_b_data = CreateModExp(attempt->data.g, attempt->dhPrime, randomSeed);
	if (g_b_data.modexp.empty()) {
		LOG(("AuthKey Error: could not generate good g_b."));
		return failed();
	}

	auto computedAuthKey = CreateAuthKey(attempt->g_a, g_b_data.randomPower, attempt->dhPrime);
	if (computedAuthKey.empty()) {
		LOG(("AuthKey Error: could not generate auth_key."));
		return failed();
	}
	AuthKey::FillData(attempt->authKey, computedAuthKey);

	auto auth_key_sha = openssl::Sha1(attempt->authKey);
	memcpy(&attempt->data.auth_key_aux_hash, auth_key_sha.data(), 8);
	memcpy(&attempt->data.auth_key_hash, auth_key_sha.data() + 12, 8);

	const auto client_dh_inner = MTP_client_DH_inner_data(
		attempt->data.nonce,
		attempt->data.server_nonce,
		attempt->data.retry_id,
		MTP_bytes(g_b_data.modexp));

	auto sdhEncString = EncryptClientDHInner(
		client_dh_inner,
		attempt->data.aesKey.data(),
		attempt->data.aesIV.data());

	attempt->stage = Stage::WaitingDone;
	DEBUG_LOG(("AuthKey Info: sending Req_client_DH_params..."));
	sendNotSecureRequest(MTPSet_client_DH_params(
		attempt->data.nonce,
		attempt->data.server_nonce,
		MTP_string(std::move(sdhEncString))));
}

void DcKeyCreator::dhClientParamsAnswered(
		not_null<Attempt*> attempt,
		const MTPset_client_DH_params_answer &data) {
	if (attempt->stage != Stage::WaitingDone) {
		LOG(("AuthKey Error: Unexpected stage %1").arg(int(attempt->stage)));
		return failed();
	}

	data.match([&](const MTPDdh_gen_ok &data) {
		if (data.vnonce() != attempt->data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_ok)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&data.vnonce(), 16).str(), Logs::mb(&attempt->data.nonce, 16).str()));
			return failed();
		}
		if (data.vserver_nonce() != attempt->data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_ok)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&data.vserver_nonce(), 16).str(), Logs::mb(&attempt->data.server_nonce, 16).str()));
			return failed();
		}
		attempt->data.new_nonce_buf[32] = bytes::type(1);
		if (data.vnew_nonce_hash1() != NonceDigest(attempt->data.new_nonce_buf)) {
			LOG(("AuthKey Error: received new_nonce_hash1 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash1: %1, new_nonce_buf: %2").arg(Logs::mb(&data.vnew_nonce_hash1(), 16).str(), Logs::mb(attempt->data.new_nonce_buf.data(), 41).str()));
			return failed();
		}

		uint64 salt1 = attempt->data.new_nonce.l.l, salt2 = attempt->data.server_nonce.l;
		attempt->data.doneSalt = salt1 ^ salt2;
		attempt->stage = Stage::Ready;
		done();
	}, [&](const MTPDdh_gen_retry &data) {
		if (data.vnonce() != attempt->data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_retry)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&data.vnonce(), 16).str(), Logs::mb(&attempt->data.nonce, 16).str()));
			return failed();
		}
		if (data.vserver_nonce() != attempt->data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_retry)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&data.vserver_nonce(), 16).str(), Logs::mb(&attempt->data.server_nonce, 16).str()));
			return failed();
		}
		attempt->data.new_nonce_buf[32] = bytes::type(2);
		uchar sha1Buffer[20];
		if (data.vnew_nonce_hash2() != NonceDigest(attempt->data.new_nonce_buf)) {
			LOG(("AuthKey Error: received new_nonce_hash2 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash2: %1, new_nonce_buf: %2").arg(Logs::mb(&data.vnew_nonce_hash2(), 16).str(), Logs::mb(attempt->data.new_nonce_buf.data(), 41).str()));
			return failed();
		}
		attempt->data.retry_id = attempt->data.auth_key_aux_hash;
		dhClientParamsSend(attempt);
	}, [&](const MTPDdh_gen_fail &data) {
		if (data.vnonce() != attempt->data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_fail)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&data.vnonce(), 16).str(), Logs::mb(&attempt->data.nonce, 16).str()));
			return failed();
		}
		if (data.vserver_nonce() != attempt->data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_fail)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&data.vserver_nonce(), 16).str(), Logs::mb(&attempt->data.server_nonce, 16).str()));
			return failed();
		}
		attempt->data.new_nonce_buf[32] = bytes::type(3);
		uchar sha1Buffer[20];
		if (data.vnew_nonce_hash3() != NonceDigest(attempt->data.new_nonce_buf)) {
			LOG(("AuthKey Error: received new_nonce_hash3 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash3: %1, new_nonce_buf: %2").arg(Logs::mb(&data.vnew_nonce_hash3(), 16).str(), Logs::mb(attempt->data.new_nonce_buf.data(), 41).str()));
			return failed();
		}
		LOG(("AuthKey Error: dh_gen_fail received!"));
		failed();
	});
}

void DcKeyCreator::failed(DcKeyError error) {
	stopReceiving();
	auto onstack = base::take(_delegate.done);
	onstack(tl::unexpected(error));
}

void DcKeyCreator::done() {
	if (_temporary.stage == Stage::None) {
		pqSend(&_temporary, _request.temporaryExpiresIn);
		return;
	}
	Assert(_temporary.stage == Stage::Ready);
	Assert(_persistent.stage == Stage::Ready || _persistent.stage == Stage::None);

	auto result = DcKeyResult();
	result.temporaryKey = std::make_shared<AuthKey>(
		AuthKey::Type::Temporary,
		_dcId,
		_temporary.authKey);
	result.temporaryServerSalt = _temporary.data.doneSalt;
	if (_persistent.stage == Stage::Ready) {
		result.persistentKey = std::make_shared<AuthKey>(
			AuthKey::Type::Generated,
			_dcId,
			_persistent.authKey);
		result.persistentServerSalt = _persistent.data.doneSalt;
	}

	stopReceiving();
	auto onstack = base::take(_delegate.done);
	onstack(std::move(result));
}

void DcKeyCreator::stopReceiving() {
	QObject::disconnect(
		_connection,
		&AbstractConnection::receivedData,
		nullptr,
		nullptr);
}

} // namespace MTP::details
