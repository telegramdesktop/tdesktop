/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_dc_key_creator.h"

#include "mtproto/connection_abstract.h"
#include "mtproto/mtproto_dh_utils.h"
#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "scheme.h"
#include "logs.h"

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

DcKeyCreator::DcKeyCreator(
	DcId dcId,
	int16 protocolDcId,
	not_null<AbstractConnection*> connection,
	not_null<DcOptions*> dcOptions,
	Delegate delegate,
	TimeId expireIn)
: _connection(connection)
, _dcOptions(dcOptions)
, _dcId(dcId)
, _protocolDcId(protocolDcId)
, _expireIn(expireIn)
, _delegate(std::move(delegate)) {
	Expects(_expireIn >= 0);
	Expects(_delegate.done != nullptr);

	_data.nonce = openssl::RandomValue<MTPint128>();
	pqSend();
}

DcKeyCreator::~DcKeyCreator() {
	if (_delegate.done) {
		stopReceiving();
	}
	const auto clearBytes = [](bytes::span bytes) {
		OPENSSL_cleanse(bytes.data(), bytes.size());
	};
	OPENSSL_cleanse(&_data, sizeof(_data));
	clearBytes(_dhPrime);
	clearBytes(_g_a);
	clearBytes(_authKey);
}

void DcKeyCreator::pqSend() {
	QObject::connect(_connection, &AbstractConnection::receivedData, [=] {
		pqAnswered();
	});

	DEBUG_LOG(("AuthKey Info: sending Req_pq..."));
	sendNotSecureRequest(MTPReq_pq_multi(_data.nonce));
}

void DcKeyCreator::pqAnswered() {
	stopReceiving();
	DEBUG_LOG(("AuthKey Info: receiving Req_pq answer..."));

	MTPReq_pq::ResponseType res_pq;
	if (!readNotSecureResponse(res_pq)) {
		return failed();
	}

	auto &res_pq_data = res_pq.c_resPQ();
	if (res_pq_data.vnonce() != _data.nonce) {
		LOG(("AuthKey Error: received nonce <> sent nonce (in res_pq)!"));
		DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&res_pq_data.vnonce(), 16).str()).arg(Logs::mb(&_data.nonce, 16).str()));
		return failed();
	}

	const auto rsaKey = _dcOptions->getDcRSAKey(
		_dcId,
		res_pq.c_resPQ().vserver_public_key_fingerprints().v);
	if (!rsaKey.valid()) {
		return failed(Error::UnknownPublicKey);
	}

	_data.server_nonce = res_pq_data.vserver_nonce();
	_data.new_nonce = openssl::RandomValue<MTPint256>();

	const auto &pq = res_pq_data.vpq().v;
	const auto parsed = ParsePQ(res_pq_data.vpq().v);
	if (parsed.p.isEmpty() || parsed.q.isEmpty()) {
		LOG(("AuthKey Error: could not factor pq!"));
		DEBUG_LOG(("AuthKey Error: problematic pq: %1").arg(Logs::mb(pq.constData(), pq.length()).str()));
		return failed();
	}

	const auto dhEncString = [&] {
		return (_expireIn == 0)
			? EncryptPQInnerRSA(
				MTP_p_q_inner_data_dc(
					res_pq_data.vpq(),
					MTP_bytes(parsed.p),
					MTP_bytes(parsed.q),
					_data.nonce,
					_data.server_nonce,
					_data.new_nonce,
					MTP_int(_protocolDcId)),
				rsaKey)
			: EncryptPQInnerRSA(
				MTP_p_q_inner_data_temp_dc(
					res_pq_data.vpq(),
					MTP_bytes(parsed.p),
					MTP_bytes(parsed.q),
					_data.nonce,
					_data.server_nonce,
					_data.new_nonce,
					MTP_int(_protocolDcId),
					MTP_int(_expireIn)),
				rsaKey);
	}();
	if (dhEncString.empty()) {
		return failed();
	}

	QObject::connect(_connection, &AbstractConnection::receivedData, [=] {
		dhParamsAnswered();
	});

	DEBUG_LOG(("AuthKey Info: sending Req_DH_params..."));

	sendNotSecureRequest(MTPReq_DH_params(
		_data.nonce,
		_data.server_nonce,
		MTP_bytes(parsed.p),
		MTP_bytes(parsed.q),
		MTP_long(rsaKey.fingerprint()),
		MTP_bytes(dhEncString)));
}

void DcKeyCreator::dhParamsAnswered() {
	stopReceiving();
	DEBUG_LOG(("AuthKey Info: receiving Req_DH_params answer..."));

	MTPReq_DH_params::ResponseType res_DH_params;
	if (!readNotSecureResponse(res_DH_params)) {
		return failed();
	}

	switch (res_DH_params.type()) {
	case mtpc_server_DH_params_ok: {
		const auto &encDH(res_DH_params.c_server_DH_params_ok());
		if (encDH.vnonce() != _data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in server_DH_params_ok)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&encDH.vnonce(), 16).str()).arg(Logs::mb(&_data.nonce, 16).str()));
			return failed();
		}
		if (encDH.vserver_nonce() != _data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_params_ok)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&encDH.vserver_nonce(), 16).str()).arg(Logs::mb(&_data.server_nonce, 16).str()));
			return failed();
		}

		auto &encDHStr = encDH.vencrypted_answer().v;
		uint32 encDHLen = encDHStr.length(), encDHBufLen = encDHLen >> 2;
		if ((encDHLen & 0x03) || encDHBufLen < 6) {
			LOG(("AuthKey Error: bad encrypted data length %1 (in server_DH_params_ok)!").arg(encDHLen));
			DEBUG_LOG(("AuthKey Error: received encrypted data %1").arg(Logs::mb(encDHStr.constData(), encDHLen).str()));
			return failed();
		}

		const auto nlen = sizeof(_data.new_nonce);
		const auto slen = sizeof(_data.server_nonce);
		auto tmp_aes_buffer = bytes::array<1024>();
		const auto tmp_aes = bytes::make_span(tmp_aes_buffer);
		bytes::copy(tmp_aes, bytes::object_as_span(&_data.new_nonce));
		bytes::copy(tmp_aes.subspan(nlen), bytes::object_as_span(&_data.server_nonce));
		bytes::copy(tmp_aes.subspan(nlen + slen), bytes::object_as_span(&_data.new_nonce));
		bytes::copy(tmp_aes.subspan(nlen + slen + nlen), bytes::object_as_span(&_data.new_nonce));
		const auto sha1ns = openssl::Sha1(tmp_aes.subspan(0, nlen + slen));
		const auto sha1sn = openssl::Sha1(tmp_aes.subspan(nlen, nlen + slen));
		const auto sha1nn = openssl::Sha1(tmp_aes.subspan(nlen + slen, nlen + nlen));

		mtpBuffer decBuffer;
		decBuffer.resize(encDHBufLen);

		const auto aesKey = bytes::make_span(_data.aesKey);
		const auto aesIV = bytes::make_span(_data.aesIV);
		bytes::copy(aesKey, bytes::make_span(sha1ns).subspan(0, 20));
		bytes::copy(aesKey.subspan(20), bytes::make_span(sha1sn).subspan(0, 12));
		bytes::copy(aesIV, bytes::make_span(sha1sn).subspan(12, 8));
		bytes::copy(aesIV.subspan(8), bytes::make_span(sha1nn).subspan(0, 20));
		bytes::copy(aesIV.subspan(28), bytes::object_as_span(&_data.new_nonce).subspan(0, 4));

		aesIgeDecryptRaw(encDHStr.constData(), &decBuffer[0], encDHLen, aesKey.data(), aesIV.data());

		const mtpPrime *from(&decBuffer[5]), *to(from), *end(from + (encDHBufLen - 5));
		MTPServer_DH_inner_data dh_inner;
		if (!dh_inner.read(to, end)) {
			LOG(("AuthKey Error: could not decrypt server_DH_inner_data!"));
			return failed();
		}
		const auto &dh_inner_data(dh_inner.c_server_DH_inner_data());
		if (dh_inner_data.vnonce() != _data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in server_DH_inner_data)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&dh_inner_data.vnonce(), 16).str()).arg(Logs::mb(&_data.nonce, 16).str()));
			return failed();
		}
		if (dh_inner_data.vserver_nonce() != _data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_inner_data)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&dh_inner_data.vserver_nonce(), 16).str()).arg(Logs::mb(&_data.server_nonce, 16).str()));
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
			DEBUG_LOG(("AuthKey Error: sha1 did not match, server_nonce: %1, new_nonce %2, encrypted data %3").arg(Logs::mb(&_data.server_nonce, 16).str()).arg(Logs::mb(&_data.new_nonce, 16).str()).arg(Logs::mb(encDHStr.constData(), encDHLen).str()));
			return failed();
		}
		base::unixtime::update(dh_inner_data.vserver_time().v);

		// check that dhPrime and (dhPrime - 1) / 2 are really prime
		if (!IsPrimeAndGood(bytes::make_span(dh_inner_data.vdh_prime().v), dh_inner_data.vg().v)) {
			LOG(("AuthKey Error: bad dh_prime primality!"));
			return failed();
		}

		_dhPrime = bytes::make_vector(
			dh_inner_data.vdh_prime().v);
		_data.g = dh_inner_data.vg().v;
		_g_a = bytes::make_vector(dh_inner_data.vg_a().v);
		_data.retry_id = MTP_long(0);
		_data.retries = 0;
	} return dhClientParamsSend();

	case mtpc_server_DH_params_fail: {
		const auto &encDH(res_DH_params.c_server_DH_params_fail());
		if (encDH.vnonce() != _data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in server_DH_params_fail)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&encDH.vnonce(), 16).str()).arg(Logs::mb(&_data.nonce, 16).str()));
			return failed();
		}
		if (encDH.vserver_nonce() != _data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_params_fail)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&encDH.vserver_nonce(), 16).str()).arg(Logs::mb(&_data.server_nonce, 16).str()));
			return failed();
		}
		if (encDH.vnew_nonce_hash() != NonceDigest(bytes::object_as_span(&_data.new_nonce))) {
			LOG(("AuthKey Error: received new_nonce_hash did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash: %1, new_nonce: %2").arg(Logs::mb(&encDH.vnew_nonce_hash(), 16).str()).arg(Logs::mb(&_data.new_nonce, 32).str()));
			return failed();
		}
		LOG(("AuthKey Error: server_DH_params_fail received!"));
	} return failed();

	}
	LOG(("AuthKey Error: unknown server_DH_params received, typeId = %1").arg(res_DH_params.type()));
	return failed();
}

void DcKeyCreator::dhClientParamsSend() {
	if (++_data.retries > 5) {
		LOG(("AuthKey Error: could not create auth_key for %1 retries").arg(_data.retries - 1));
		return failed();
	}

	// gen rand 'b'
	auto randomSeed = bytes::vector(ModExpFirst::kRandomPowerSize);
	bytes::set_random(randomSeed);
	auto g_b_data = CreateModExp(_data.g, _dhPrime, randomSeed);
	if (g_b_data.modexp.empty()) {
		LOG(("AuthKey Error: could not generate good g_b."));
		return failed();
	}

	auto computedAuthKey = CreateAuthKey(_g_a, g_b_data.randomPower, _dhPrime);
	if (computedAuthKey.empty()) {
		LOG(("AuthKey Error: could not generate auth_key."));
		return failed();
	}
	AuthKey::FillData(_authKey, computedAuthKey);

	auto auth_key_sha = openssl::Sha1(_authKey);
	memcpy(&_data.auth_key_aux_hash, auth_key_sha.data(), 8);
	memcpy(&_data.auth_key_hash, auth_key_sha.data() + 12, 8);

	const auto client_dh_inner = MTP_client_DH_inner_data(
		_data.nonce,
		_data.server_nonce,
		_data.retry_id,
		MTP_bytes(g_b_data.modexp));

	auto sdhEncString = EncryptClientDHInner(
		client_dh_inner,
		_data.aesKey.data(),
		_data.aesIV.data());

	QObject::connect(_connection, &AbstractConnection::receivedData, [=] {
		dhClientParamsAnswered();
	});

	DEBUG_LOG(("AuthKey Info: sending Req_client_DH_params..."));
	sendNotSecureRequest(MTPSet_client_DH_params(
		_data.nonce,
		_data.server_nonce,
		MTP_string(std::move(sdhEncString))));
}

void DcKeyCreator::dhClientParamsAnswered() {
	stopReceiving();
	DEBUG_LOG(("AuthKey Info: receiving Req_client_DH_params answer..."));

	MTPSet_client_DH_params::ResponseType res_client_DH_params;
	if (!readNotSecureResponse(res_client_DH_params)) {
		return failed();
	}

	switch (res_client_DH_params.type()) {
	case mtpc_dh_gen_ok: {
		const auto &resDH(res_client_DH_params.c_dh_gen_ok());
		if (resDH.vnonce() != _data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_ok)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&resDH.vnonce(), 16).str()).arg(Logs::mb(&_data.nonce, 16).str()));
			return failed();
		}
		if (resDH.vserver_nonce() != _data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_ok)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&resDH.vserver_nonce(), 16).str()).arg(Logs::mb(&_data.server_nonce, 16).str()));
			return failed();
		}
		_data.new_nonce_buf[32] = bytes::type(1);
		if (resDH.vnew_nonce_hash1() != NonceDigest(_data.new_nonce_buf)) {
			LOG(("AuthKey Error: received new_nonce_hash1 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash1: %1, new_nonce_buf: %2").arg(Logs::mb(&resDH.vnew_nonce_hash1(), 16).str()).arg(Logs::mb(_data.new_nonce_buf.data(), 41).str()));
			return failed();
		}

		uint64 salt1 = _data.new_nonce.l.l, salt2 = _data.server_nonce.l;
		done(salt1 ^ salt2);
	} return;

	case mtpc_dh_gen_retry: {
		const auto &resDH(res_client_DH_params.c_dh_gen_retry());
		if (resDH.vnonce() != _data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_retry)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&resDH.vnonce(), 16).str()).arg(Logs::mb(&_data.nonce, 16).str()));
			return failed();
		}
		if (resDH.vserver_nonce() != _data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_retry)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&resDH.vserver_nonce(), 16).str()).arg(Logs::mb(&_data.server_nonce, 16).str()));
			return failed();
		}
		_data.new_nonce_buf[32] = bytes::type(2);
		uchar sha1Buffer[20];
		if (resDH.vnew_nonce_hash2() != NonceDigest(_data.new_nonce_buf)) {
			LOG(("AuthKey Error: received new_nonce_hash2 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash2: %1, new_nonce_buf: %2").arg(Logs::mb(&resDH.vnew_nonce_hash2(), 16).str()).arg(Logs::mb(_data.new_nonce_buf.data(), 41).str()));
			return failed();
		}
		_data.retry_id = _data.auth_key_aux_hash;
	} return dhClientParamsSend();

	case mtpc_dh_gen_fail: {
		const auto &resDH(res_client_DH_params.c_dh_gen_fail());
		if (resDH.vnonce() != _data.nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_fail)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&resDH.vnonce(), 16).str()).arg(Logs::mb(&_data.nonce, 16).str()));
			return failed();
		}
		if (resDH.vserver_nonce() != _data.server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_fail)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&resDH.vserver_nonce(), 16).str()).arg(Logs::mb(&_data.server_nonce, 16).str()));
			return failed();
		}
		_data.new_nonce_buf[32] = bytes::type(3);
		uchar sha1Buffer[20];
		if (resDH.vnew_nonce_hash3() != NonceDigest(_data.new_nonce_buf)) {
			LOG(("AuthKey Error: received new_nonce_hash3 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash3: %1, new_nonce_buf: %2").arg(Logs::mb(&resDH.vnew_nonce_hash3(), 16).str()).arg(Logs::mb(_data.new_nonce_buf.data(), 41).str()));
			return failed();
		}
		LOG(("AuthKey Error: dh_gen_fail received!"));
	} return failed();
	}

	LOG(("AuthKey Error: unknown set_client_DH_params_answer received, typeId = %1").arg(res_client_DH_params.type()));
	return failed();
}

template <typename Request>
void DcKeyCreator::sendNotSecureRequest(const Request &request) {
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

template <typename Response>
bool DcKeyCreator::readNotSecureResponse(Response &response) {
	if (_delegate.receivedSome) {
		_delegate.receivedSome();
	}

	if (_connection->received().empty()) {
		LOG(("AuthKey Error: "
			"trying to read response from empty received list"));
		return false;
	}

	const auto buffer = std::move(_connection->received().front());
	_connection->received().pop_front();

	const auto answer = _connection->parseNotSecureResponse(buffer);
	if (answer.empty()) {
		return false;
	}
	auto from = answer.data();
	return response.read(from, from + answer.size());
}

void DcKeyCreator::failed(Error error) {
	stopReceiving();
	auto onstack = base::take(_delegate.done);
	onstack(tl::unexpected(error));
}

void DcKeyCreator::done(uint64 serverSalt) {
	auto result = Result();
	result.key = std::make_shared<AuthKey>(
		AuthKey::Type::Generated,
		_dcId,
		_authKey);
	result.serverSalt = serverSalt;

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
