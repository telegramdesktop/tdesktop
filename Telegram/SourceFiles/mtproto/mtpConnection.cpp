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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include <openssl/rand.h>

namespace {
	bool parsePQ(const std::string &pqStr, std::string &pStr, std::string &qStr) {
		if (pqStr.length() > 8) return false; // more than 64 bit pq

		uint64 pq = 0, p, q;
		const uchar *pqChars = (const uchar*)&pqStr[0];
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
			if (!ySqr || y + pqSqrt >= pq) return false;
			if (y * y == ySqr) {
				p = pqSqrt + y;
				q = (pqSqrt > y) ? (pqSqrt - y) : (y - pqSqrt);
				break;
			}
		}
		if (p > q) swap(p, q);

		pStr.resize(4);
		uchar *pChars = (uchar*)&pStr[0];
		for (uint32 i = 0; i < 4; ++i) {
			*(pChars + 3 - i) = (uchar)(p & 0xFF);
			p >>= 8;
		}

		qStr.resize(4);
		uchar *qChars = (uchar*)&qStr[0];
		for (uint32 i = 0; i < 4; ++i) {
			*(qChars + 3 - i) = (uchar)(q & 0xFF);
			q >>= 8;
		}

		return true;
	}

	class _BigNumCounter {
	public:
		bool count(const void *power, const void *modul, uint32 g, void *gResult, const void *g_a, void *g_aResult) {
			DEBUG_LOG(("BigNum Info: counting g_b = g ^ b % dh_prime and auth_key = g_a ^ b % dh_prime"));
			uint32 g_be = qToBigEndian(g);
			if (
				!BN_bin2bn((const uchar*)power, 64 * sizeof(uint32), &bnPower) ||
				!BN_bin2bn((const uchar*)modul, 64 * sizeof(uint32), &bnModul) ||
				!BN_bin2bn((const uchar*)&g_be, sizeof(uint32), &bn_g) ||
				!BN_bin2bn((const uchar*)g_a, 64 * sizeof(uint32), &bn_g_a)
			) {
				ERR_load_crypto_strings();
				LOG(("BigNum Error: BN_bin2bn failed, error: %1").arg(ERR_error_string(ERR_get_error(), 0)));
				DEBUG_LOG(("BigNum Error: base %1, power %2, modul %3").arg(Logs::mb(&g_be, sizeof(uint32)).str()).arg(Logs::mb(power, 64 * sizeof(uint32)).str()).arg(Logs::mb(modul, 64 * sizeof(uint32)).str()));
				return false;
			}

			if (!BN_mod_exp(&bnResult, &bn_g, &bnPower, &bnModul, ctx)) {
				ERR_load_crypto_strings();
				LOG(("BigNum Error: BN_mod_exp failed, error: %1").arg(ERR_error_string(ERR_get_error(), 0)));
				DEBUG_LOG(("BigNum Error: base %1, power %2, modul %3").arg(Logs::mb(&g_be, sizeof(uint32)).str()).arg(Logs::mb(power, 64 * sizeof(uint32)).str()).arg(Logs::mb(modul, 64 * sizeof(uint32)).str()));
				return false;
			}

			uint32 resultLen = BN_num_bytes(&bnResult);
			if (resultLen != 64 * sizeof(uint32)) {
				DEBUG_LOG(("BigNum Error: bad gResult len (%1)").arg(resultLen));
				return false;
			}
			resultLen = BN_bn2bin(&bnResult, (uchar*)gResult);
			if (resultLen != 64 * sizeof(uint32)) {
				DEBUG_LOG(("BigNum Error: bad gResult export len (%1)").arg(resultLen));
				return false;
			}

			BN_add_word(&bnResult, 1); // check g_b < dh_prime - 1
			if (BN_cmp(&bnResult, &bnModul) >= 0) {
				DEBUG_LOG(("BigNum Error: bad g_b >= dh_prime - 1"));
				return false;
			}

			if (!BN_mod_exp(&bnResult, &bn_g_a, &bnPower, &bnModul, ctx)) {
				ERR_load_crypto_strings();
				LOG(("BigNum Error: BN_mod_exp failed, error: %1").arg(ERR_error_string(ERR_get_error(), 0)));
				DEBUG_LOG(("BigNum Error: base %1, power %2, modul %3").arg(Logs::mb(&g_be, sizeof(uint32)).str()).arg(Logs::mb(power, 64 * sizeof(uint32)).str()).arg(Logs::mb(modul, 64 * sizeof(uint32)).str()));
				return false;
			}

			resultLen = BN_num_bytes(&bnResult);
			if (resultLen != 64 * sizeof(uint32)) {
				DEBUG_LOG(("BigNum Error: bad g_aResult len (%1)").arg(resultLen));
				return false;
			}
			resultLen = BN_bn2bin(&bnResult, (uchar*)g_aResult);
			if (resultLen != 64 * sizeof(uint32)) {
				DEBUG_LOG(("BigNum Error: bad g_aResult export len (%1)").arg(resultLen));
				return false;
			}

			BN_add_word(&bn_g_a, 1); // check g_a < dh_prime - 1
			if (BN_cmp(&bn_g_a, &bnModul) >= 0) {
				DEBUG_LOG(("BigNum Error: bad g_a >= dh_prime - 1"));
				return false;
			}

			return true;
		}

		_BigNumCounter() : ctx(BN_CTX_new()) {
			BN_init(&bnPower);
			BN_init(&bnModul);
			BN_init(&bn_g);
			BN_init(&bn_g_a);
			BN_init(&bnResult);
		}
		~_BigNumCounter() {
			BN_CTX_free(ctx);
			BN_clear_free(&bnPower);
			BN_clear_free(&bnModul);
			BN_clear_free(&bn_g);
			BN_clear_free(&bn_g_a);
			BN_clear_free(&bnResult);
		}

	private:
		BIGNUM bnPower, bnModul, bn_g, bn_g_a, bnResult;
		BN_CTX *ctx;
	};

	// Miller-Rabin primality test
	class _BigNumPrimeTest {
	public:

		bool isPrimeAndGood(const void *pData, uint32 iterCount, int32 g) {
			if (!memcmp(pData, "\xC7\x1C\xAE\xB9\xC6\xB1\xC9\x04\x8E\x6C\x52\x2F\x70\xF1\x3F\x73\x98\x0D\x40\x23\x8E\x3E\x21\xC1\x49\x34\xD0\x37\x56\x3D\x93\x0F\x48\x19\x8A\x0A\xA7\xC1\x40\x58\x22\x94\x93\xD2\x25\x30\xF4\xDB\xFA\x33\x6F\x6E\x0A\xC9\x25\x13\x95\x43\xAE\xD4\x4C\xCE\x7C\x37\x20\xFD\x51\xF6\x94\x58\x70\x5A\xC6\x8C\xD4\xFE\x6B\x6B\x13\xAB\xDC\x97\x46\x51\x29\x69\x32\x84\x54\xF1\x8F\xAF\x8C\x59\x5F\x64\x24\x77\xFE\x96\xBB\x2A\x94\x1D\x5B\xCD\x1D\x4A\xC8\xCC\x49\x88\x07\x08\xFA\x9B\x37\x8E\x3C\x4F\x3A\x90\x60\xBE\xE6\x7C\xF9\xA4\xA4\xA6\x95\x81\x10\x51\x90\x7E\x16\x27\x53\xB5\x6B\x0F\x6B\x41\x0D\xBA\x74\xD8\xA8\x4B\x2A\x14\xB3\x14\x4E\x0E\xF1\x28\x47\x54\xFD\x17\xED\x95\x0D\x59\x65\xB4\xB9\xDD\x46\x58\x2D\xB1\x17\x8D\x16\x9C\x6B\xC4\x65\xB0\xD6\xFF\x9C\xA3\x92\x8F\xEF\x5B\x9A\xE4\xE4\x18\xFC\x15\xE8\x3E\xBE\xA0\xF8\x7F\xA9\xFF\x5E\xED\x70\x05\x0D\xED\x28\x49\xF4\x7B\xF9\x59\xD9\x56\x85\x0C\xE9\x29\x85\x1F\x0D\x81\x15\xF6\x35\xB1\x05\xEE\x2E\x4E\x15\xD0\x4B\x24\x54\xBF\x6F\x4F\xAD\xF0\x34\xB1\x04\x03\x11\x9C\xD8\xE3\xB9\x2F\xCC\x5B", 256)) {
				if (g == 3 || g == 4 || g == 5 || g == 7) {
					return true;
				}
			}
			if (
				!BN_bin2bn((const uchar*)pData, 64 * sizeof(uint32), &bnPrime)
			) {
				ERR_load_crypto_strings();
				LOG(("BigNum PT Error: BN_bin2bn failed, error: %1").arg(ERR_error_string(ERR_get_error(), 0)));
				DEBUG_LOG(("BigNum PT Error: prime %1").arg(Logs::mb(pData, 64 * sizeof(uint32)).str()));
				return false;
			}

			int32 numBits = BN_num_bits(&bnPrime);
			if (numBits != 2048) {
				LOG(("BigNum PT Error: BN_bin2bn failed, bad dh_prime num bits: %1").arg(numBits));
				return false;
			}

			if (BN_is_prime_ex(&bnPrime, MTPMillerRabinIterCount, ctx, NULL) == 0) {
				return false;
			}

			switch (g) {
			case 2: {
				int32 mod8 = BN_mod_word(&bnPrime, 8);
				if (mod8 != 7) {
					LOG(("BigNum PT Error: bad g value: %1, mod8: %2").arg(g).arg(mod8));
					return false;
				}
			} break;
			case 3: {
				int32 mod3 = BN_mod_word(&bnPrime, 3);
				if (mod3 != 2) {
					LOG(("BigNum PT Error: bad g value: %1, mod3: %2").arg(g).arg(mod3));
					return false;
				}
			} break;
			case 4: break;
			case 5: {
				int32 mod5 = BN_mod_word(&bnPrime, 5);
				if (mod5 != 1 && mod5 != 4) {
					LOG(("BigNum PT Error: bad g value: %1, mod5: %2").arg(g).arg(mod5));
					return false;
				}
			} break;
			case 6: {
				int32 mod24 = BN_mod_word(&bnPrime, 24);
				if (mod24 != 19 && mod24 != 23) {
					LOG(("BigNum PT Error: bad g value: %1, mod24: %2").arg(g).arg(mod24));
					return false;
				}
			} break;
			case 7: {
				int32 mod7 = BN_mod_word(&bnPrime, 7);
				if (mod7 != 3 && mod7 != 5 && mod7 != 6) {
					LOG(("BigNum PT Error: bad g value: %1, mod7: %2").arg(g).arg(mod7));
					return false;
				}
			} break;
			default:
				LOG(("BigNum PT Error: bad g value: %1").arg(g));
				return false;
				break;
			}

			BN_sub_word(&bnPrime, 1); // (p - 1) / 2
			BN_div_word(&bnPrime, 2);

			if (BN_is_prime_ex(&bnPrime, MTPMillerRabinIterCount, ctx, NULL) == 0) {
				return false;
			}

			return true;
		}

		_BigNumPrimeTest() : ctx(BN_CTX_new()) {
			BN_init(&bnPrime);
		}
		~_BigNumPrimeTest() {
			BN_CTX_free(ctx);
			BN_clear_free(&bnPrime);
		}

	private:
		BIGNUM bnPrime;
		BN_CTX *ctx;
	};

	typedef QMap<uint64, mtpPublicRSA> PublicRSAKeys;
	PublicRSAKeys gPublicRSA;

	MTProtoConnection gMainConnection;

	bool gConfigInited = false;
	void initRSAConfig() {
		if (gConfigInited) return;
		gConfigInited = true;

		DEBUG_LOG(("MTP Info: MTP config init"));

		// read all public keys
		uint32 keysCnt;
		const char **keys = cPublicRSAKeys(keysCnt);
		for (uint32 i = 0; i < keysCnt; ++i) {
			mtpPublicRSA key(keys[i]);
			if (key.key()) {
				gPublicRSA.insert(key.fingerPrint(), key);
			} else {
				LOG(("MTP Error: could not read this public RSA key:"));
				LOG((keys[i]));
			}
		}
		DEBUG_LOG(("MTP Info: read %1 public RSA keys").arg(gPublicRSA.size()));
	}
}

MTPThread::MTPThread(QObject *parent) : QThread(parent) {
	static uint32 gThreadId = 0;
	threadId = ++gThreadId;
}

uint32 MTPThread::getThreadId() const {
	return threadId;
}

MTProtoConnection::MTProtoConnection() : thread(0), data(0) {
}

int32 MTProtoConnection::start(MTPSessionData *sessionData, int32 dc) {
	initRSAConfig();

	if (thread) {
		DEBUG_LOG(("MTP Info: MTP start called for already working connection"));
		return dc;
	}

	thread = new MTPThread(QApplication::instance());
	data = new MTProtoConnectionPrivate(thread, this, sessionData, dc);

	dc = data->getDC();
	if (!dc) {
		delete data;
		delete thread;
		data = 0;
		thread = 0;
		return 0;
	}
	thread->start();
	return dc;
}

void MTProtoConnection::stop() {
	if (data) data->stop();
	if (thread) thread->quit();
}

void MTProtoConnection::stopped() {
	if (thread) thread->deleteLater();
	if (data) data->deleteLater();
	thread = 0;
	data = 0;
	delete this;
}

int32 MTProtoConnection::state() const {
	if (!data) return Disconnected;

	return data->getState();
}

QString MTProtoConnection::transport() const {
	if (!data) return QString();

	return data->transport();
}

namespace {
	mtpBuffer _handleHttpResponse(QNetworkReply *reply) {
		QByteArray response = reply->readAll();
		TCP_LOG(("HTTP Info: read %1 bytes").arg(response.size()));

		if (response.isEmpty()) return mtpBuffer();

		if (response.size() & 0x03 || response.size() < 8) {
			LOG(("HTTP Error: bad response size %1").arg(response.size()));
			return mtpBuffer(1, -500);
		}

		mtpBuffer data(response.size() >> 2);
		memcpy(data.data(), response.constData(), response.size());

		return data;
	}

	bool _handleHttpError(QNetworkReply *reply) { // returnes "maybe bad key"
		bool mayBeBadKey = false;

		QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
		if (statusCode.isValid()) {
			int status = statusCode.toInt();
			mayBeBadKey = (status == 410);
			if (status == 429) {
				LOG(("Protocol Error: 429 flood code returned!"));
			}
		}

		switch (reply->error()) {
		case QNetworkReply::ConnectionRefusedError: LOG(("HTTP Error: connection refused - %1").arg(reply->errorString())); break;
		case QNetworkReply::RemoteHostClosedError: LOG(("HTTP Error: remote host closed - %1").arg(reply->errorString())); break;
		case QNetworkReply::HostNotFoundError: LOG(("HTTP Error: host not found - %2").arg(reply->error()).arg(reply->errorString())); break;
		case QNetworkReply::TimeoutError: LOG(("HTTP Error: timeout - %2").arg(reply->error()).arg(reply->errorString())); break;
		case QNetworkReply::OperationCanceledError: LOG(("HTTP Error: cancelled - %2").arg(reply->error()).arg(reply->errorString())); break;
		case QNetworkReply::SslHandshakeFailedError:
		case QNetworkReply::TemporaryNetworkFailureError:
		case QNetworkReply::NetworkSessionFailedError:
		case QNetworkReply::BackgroundRequestNotAllowedError:
		case QNetworkReply::UnknownNetworkError: LOG(("HTTP Error: network error %1 - %2").arg(reply->error()).arg(reply->errorString())); break;

			// proxy errors (101-199):
		case QNetworkReply::ProxyConnectionRefusedError:
		case QNetworkReply::ProxyConnectionClosedError:
		case QNetworkReply::ProxyNotFoundError:
		case QNetworkReply::ProxyTimeoutError:
		case QNetworkReply::ProxyAuthenticationRequiredError:
		case QNetworkReply::UnknownProxyError:LOG(("HTTP Error: proxy error %1 - %2").arg(reply->error()).arg(reply->errorString())); break;

			// content errors (201-299):
		case QNetworkReply::ContentAccessDenied:
		case QNetworkReply::ContentOperationNotPermittedError:
		case QNetworkReply::ContentNotFoundError:
		case QNetworkReply::AuthenticationRequiredError:
		case QNetworkReply::ContentReSendError:
		case QNetworkReply::UnknownContentError: LOG(("HTTP Error: content error %1 - %2").arg(reply->error()).arg(reply->errorString())); break;

			// protocol errors
		case QNetworkReply::ProtocolUnknownError:
		case QNetworkReply::ProtocolInvalidOperationError:
		case QNetworkReply::ProtocolFailure: LOG(("HTTP Error: protocol error %1 - %2").arg(reply->error()).arg(reply->errorString())); break;
		};
		TCP_LOG(("HTTP Error %1, restarting! - %2").arg(reply->error()).arg(reply->errorString()));

		return mayBeBadKey;
	}

	mtpBuffer _handleTcpResponse(mtpPrime *packet, uint32 size) {
		if (size < 4 || size * sizeof(mtpPrime) > MTPPacketSizeMax) {
			LOG(("TCP Error: bad packet size %1").arg(size * sizeof(mtpPrime)));
			return mtpBuffer(1, -500);
		}
        if (packet[0] != int32(size * sizeof(mtpPrime))) {
			LOG(("TCP Error: bad packet header"));
			TCP_LOG(("TCP Error: bad packet header, packet: %1").arg(Logs::mb(packet, size * sizeof(mtpPrime)).str()));
			return mtpBuffer(1, -500);
		}
		if (packet[size - 1] != hashCrc32(packet, (size - 1) * sizeof(mtpPrime))) {
			LOG(("TCP Error: bad packet checksum"));
			TCP_LOG(("TCP Error: bad packet checksum, packet: %1").arg(Logs::mb(packet, size * sizeof(mtpPrime)).str()));
			return mtpBuffer(1, -500);
		}
		TCP_LOG(("TCP Info: packet received, num = %1, size = %2").arg(packet[1]).arg(size * sizeof(mtpPrime)));
		if (size == 4) {
			if (packet[2] == -429) {
				LOG(("Protocol Error: -429 flood code returned!"));
			} else {
				LOG(("TCP Error: error packet received, code = %1").arg(packet[2]));
			}
			return mtpBuffer(1, packet[2]);
		}

		mtpBuffer data(size - 3);
		memcpy(data.data(), packet + 2, (size - 3) * sizeof(mtpPrime));

		return data;
	}

	void _handleTcpError(QAbstractSocket::SocketError e, QTcpSocket &sock) {
		switch (e) {
		case QAbstractSocket::ConnectionRefusedError:
			LOG(("TCP Error: socket connection refused - %1").arg(sock.errorString()));
			break;

		case QAbstractSocket::RemoteHostClosedError:
			TCP_LOG(("TCP Info: remote host closed socket connection - %1").arg(sock.errorString()));
			break;

		case QAbstractSocket::HostNotFoundError:
			LOG(("TCP Error: host not found - %1").arg(sock.errorString()));
			break;

		case QAbstractSocket::SocketTimeoutError:
			LOG(("TCP Error: socket timeout - %1").arg(sock.errorString()));
			break;

		case QAbstractSocket::NetworkError:
			LOG(("TCP Error: network - %1").arg(sock.errorString()));
			break;

		case QAbstractSocket::ProxyAuthenticationRequiredError:
		case QAbstractSocket::ProxyConnectionRefusedError:
		case QAbstractSocket::ProxyConnectionClosedError:
		case QAbstractSocket::ProxyConnectionTimeoutError:
		case QAbstractSocket::ProxyNotFoundError:
		case QAbstractSocket::ProxyProtocolError:
			LOG(("TCP Error: proxy (%1) - %2").arg(e).arg(sock.errorString()));
			break;

		default:
			LOG(("TCP Error: other (%1) - %2").arg(e).arg(sock.errorString()));
			break;
		}

		TCP_LOG(("TCP Error %1, restarting! - %2").arg(e).arg(sock.errorString()));
	}

	mtpBuffer _preparePQFake(const MTPint128 &nonce) {
		MTPReq_pq req_pq(nonce);
		mtpBuffer buffer;
		uint32 requestSize = req_pq.innerLength() >> 2;

		buffer.resize(0);
		buffer.reserve(8 + requestSize);
		buffer.push_back(0); // tcp packet len
		buffer.push_back(0); // tcp packet num
		buffer.push_back(0);
		buffer.push_back(0);
		buffer.push_back(0);
		buffer.push_back(unixtime());
		buffer.push_back(requestSize * 4);
		req_pq.write(buffer);
		buffer.push_back(0); // tcp crc32 hash

		return buffer;
	}

	MTPResPQ _readPQFakeReply(const mtpBuffer &buffer) {
		const mtpPrime *answer(buffer.constData());
		uint32 len = buffer.size();
		if (len < 5) {
			LOG(("Fake PQ Error: bad request answer, len = %1").arg(len * sizeof(mtpPrime)));
			DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
			throw Exception("bad pq reply");
		}
		if (answer[0] != 0 || answer[1] != 0 || (((uint32)answer[2]) & 0x03) != 1/* || (unixtime() - answer[3] > 300) || (answer[3] - unixtime() > 60)*/) { // didnt sync time yet
			LOG(("Fake PQ Error: bad request answer start (%1 %2 %3)").arg(answer[0]).arg(answer[1]).arg(answer[2]));
			DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
			throw Exception("bad pq reply");
		}
		uint32 answerLen = (uint32)answer[4];
		if (answerLen != (len - 5) * sizeof(mtpPrime)) {
			LOG(("Fake PQ Error: bad request answer %1 <> %2").arg(answerLen).arg((len - 5) * sizeof(mtpPrime)));
			DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
			throw Exception("bad pq reply");
		}
		const mtpPrime *from(answer + 5), *end(from + len - 5);
		MTPResPQ response;
		response.read(from, end);
		return response;
	}

}

MTPabstractTcpConnection::MTPabstractTcpConnection() :
packetNum(0), packetRead(0), packetLeft(0), readingToShort(true), currentPos((char*)shortBuffer) {
}

void MTPabstractTcpConnection::socketRead() {
	if (sock.state() != QAbstractSocket::ConnectedState) {
		LOG(("MTP error: socket not connected in socketRead(), state: %1").arg(sock.state()));
		emit error();
		return;
	}

	do {
		char *readTo = currentPos;
		uint32 toRead = packetLeft ? packetLeft : (readingToShort ? (MTPShortBufferSize * sizeof(mtpPrime)-packetRead) : 4);
		if (readingToShort) {
			if (currentPos + toRead > ((char*)shortBuffer) + MTPShortBufferSize * sizeof(mtpPrime)) {
				longBuffer.resize(((packetRead + toRead) >> 2) + 1);
				memcpy(&longBuffer[0], shortBuffer, packetRead);
				readTo = ((char*)&longBuffer[0]) + packetRead;
			}
		} else {
			if (longBuffer.size() * sizeof(mtpPrime) < packetRead + toRead) {
				longBuffer.resize(((packetRead + toRead) >> 2) + 1);
				readTo = ((char*)&longBuffer[0]) + packetRead;
			}
		}
		int32 bytes = (int32)sock.read(readTo, toRead);
		if (bytes > 0) {
			TCP_LOG(("TCP Info: read %1 bytes").arg(bytes));

			packetRead += bytes;
			currentPos += bytes;
			if (packetLeft) {
				packetLeft -= bytes;
				if (!packetLeft) {
					socketPacket((mtpPrime*)(currentPos - packetRead), packetRead >> 2);
					currentPos = (char*)shortBuffer;
					packetRead = packetLeft = 0;
					readingToShort = true;
				} else {
					TCP_LOG(("TCP Info: not enough %1 for packet! read %2").arg(packetLeft).arg(packetRead));
					emit receivedSome();
				}
			} else {
				bool move = false;
				while (packetRead >= 4) {
					uint32 packetSize = *(uint32*)(currentPos - packetRead);
					if (packetSize < 16 || packetSize > MTPPacketSizeMax || (packetSize & 0x03)) {
						LOG(("TCP Error: packet size = %1").arg(packetSize));
						emit error();
						return;
					}
					if (packetRead >= packetSize) {
						socketPacket((mtpPrime*)(currentPos - packetRead), packetSize >> 2);
						packetRead -= packetSize;
						packetLeft = 0;
						move = true;
					} else {
						packetLeft = packetSize - packetRead;
						TCP_LOG(("TCP Info: not enough %1 for packet! size %2 read %3").arg(packetLeft).arg(packetSize).arg(packetRead));
						emit receivedSome();
						break;
					}
				}
				if (move) {
					if (!packetRead) {
						currentPos = (char*)shortBuffer;
						readingToShort = true;
					} else if (!readingToShort && packetRead < MTPShortBufferSize * sizeof(mtpPrime)) {
						memcpy(shortBuffer, currentPos - packetRead, packetRead);
						currentPos = (char*)shortBuffer;
						readingToShort = true;
					}
				}
			}
		} else if (bytes < 0) {
			LOG(("TCP Error: socket read return -1"));
			emit error();
			return;
		} else {
			TCP_LOG(("TCP Info: no bytes read, but bytes available was true.."));
			break;
		}
	} while (sock.state() == QAbstractSocket::ConnectedState && sock.bytesAvailable());
}

MTPautoConnection::MTPautoConnection(QThread *thread) : status(WaitingBoth),
tcpNonce(MTP::nonce<MTPint128>()), httpNonce(MTP::nonce<MTPint128>()), _tcpTimeout(MTPMinReceiveDelay), _flags(0) {
	moveToThread(thread);

	manager.moveToThread(thread);
	manager.setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));

	httpStartTimer.moveToThread(thread);
	httpStartTimer.setSingleShot(true);
	connect(&httpStartTimer, SIGNAL(timeout()), this, SLOT(onHttpStart()));

	tcpTimeoutTimer.moveToThread(thread);
	tcpTimeoutTimer.setSingleShot(true);
	connect(&tcpTimeoutTimer, SIGNAL(timeout()), this, SLOT(onTcpTimeoutTimer()));

	sock.moveToThread(thread);
	sock.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	connect(&sock, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
	connect(&sock, SIGNAL(connected()), this, SLOT(onSocketConnected()));
	connect(&sock, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
}

void MTPautoConnection::onHttpStart() {
	if (status == HttpReady) {
		DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by timer").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
		status = UsingHttp;
		sock.disconnectFromHost();
		emit connected();
	}
}

void MTPautoConnection::onSocketConnected() {
	if (status == HttpReady || status == WaitingBoth || status == WaitingTcp) {
		mtpBuffer buffer(_preparePQFake(tcpNonce));

		DEBUG_LOG(("Connection Info: sending fake req_pq through TCP/%1 transport").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));

		if (_tcpTimeout < 0) _tcpTimeout = -_tcpTimeout;
		tcpTimeoutTimer.start(_tcpTimeout);

		tcpSend(buffer);
	} else if (status == WaitingHttp || status == UsingHttp) {
		sock.disconnectFromHost();
	}
}

void MTPautoConnection::onTcpTimeoutTimer() {
	if (status == HttpReady || status == WaitingBoth || status == WaitingTcp) {
		if (_tcpTimeout < MTPMaxReceiveDelay) _tcpTimeout *= 2;
		_tcpTimeout = -_tcpTimeout;

		QAbstractSocket::SocketState state = sock.state();
		if (state == QAbstractSocket::ConnectedState || state == QAbstractSocket::ConnectingState || state == QAbstractSocket::HostLookupState) {
			sock.disconnectFromHost();
		} else if (state != QAbstractSocket::ClosingState) {
			sock.connectToHost(QHostAddress(_addr), _port);
		}
	}
}

void MTPautoConnection::onSocketDisconnected() {
	if (_tcpTimeout < 0) {
		_tcpTimeout = -_tcpTimeout;
		if (status == HttpReady || status == WaitingBoth || status == WaitingTcp) {
			sock.connectToHost(QHostAddress(_addr), _port);
			return;
		}
	}
	if (status == WaitingBoth) {
		status = WaitingHttp;
	} else if (status == WaitingTcp || status == UsingTcp) {
		emit disconnected();
	} else if (status == HttpReady) {
		DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by socket disconnect").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
		status = UsingHttp;
		emit connected();
	}
}

void MTPautoConnection::sendData(mtpBuffer &buffer) {
	if (status == FinishedWork) return;

	if (buffer.size() < 3) {
		LOG(("TCP Error: writing bad packet, len = %1").arg(buffer.size() * sizeof(mtpPrime)));
		TCP_LOG(("TCP Error: bad packet %1").arg(Logs::mb(&buffer[0], buffer.size() * sizeof(mtpPrime)).str()));
		emit error();
		return;
	}

	if (status == UsingTcp) {
		tcpSend(buffer);
	} else {
		httpSend(buffer);
	}
}

void MTPautoConnection::tcpSend(mtpBuffer &buffer) {
	uint32 size = buffer.size(), len = size * 4;

	buffer[0] = len;
	buffer[1] = packetNum++;
	buffer[size - 1] = hashCrc32(&buffer[0], len - 4);
	TCP_LOG(("TCP Info: write %1 packet %2 bytes").arg(packetNum).arg(len));

	sock.write((const char*)&buffer[0], len);
}

void MTPautoConnection::httpSend(mtpBuffer &buffer) {
	int32 requestSize = (buffer.size() - 3) * sizeof(mtpPrime);

	QNetworkRequest request(address);
	request.setHeader(QNetworkRequest::ContentLengthHeader, QVariant(requestSize));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(qsl("application/x-www-form-urlencoded")));

	TCP_LOG(("HTTP Info: sending %1 len request").arg(requestSize));
	requests.insert(manager.post(request, QByteArray((const char*)(&buffer[2]), requestSize)));
}

void MTPautoConnection::disconnectFromServer() {
	if (status == FinishedWork) return;
	status = FinishedWork;

	Requests copy = requests;
	requests.clear();
	for (Requests::const_iterator i = copy.cbegin(), e = copy.cend(); i != e; ++i) {
		(*i)->abort();
		(*i)->deleteLater();
	}

	disconnect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestFinished(QNetworkReply*)));

	address = QUrl();

	disconnect(&sock, SIGNAL(readyRead()), 0, 0);
	sock.close();

	httpStartTimer.stop();
}

void MTPautoConnection::connectToServer(const QString &addr, int32 port, int32 flags) {
	address = QUrl(((flags & MTPDdcOption::flag_ipv6) ? qsl("http://[%1]:%2/api") : qsl("http://%1:%2/api")).arg(addr).arg(80));//not p - always 80 port for http transport
	TCP_LOG(("HTTP Info: address is %1").arg(address.toDisplayString()));
	connect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestFinished(QNetworkReply*)));

	_addr = addr;
	_port = port;
	_flags = flags;

	connect(&sock, SIGNAL(readyRead()), this, SLOT(socketRead()));
	sock.connectToHost(QHostAddress(_addr), _port);

	mtpBuffer buffer(_preparePQFake(httpNonce));

	DEBUG_LOG(("Connection Info: sending fake req_pq through HTTP/%1 transport").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));

	httpSend(buffer);
}

bool MTPautoConnection::isConnected() const {
	return (status == UsingTcp) || (status == UsingHttp);
}

void MTPautoConnection::requestFinished(QNetworkReply *reply) {
	if (status == FinishedWork) return;

	reply->deleteLater();
	if (reply->error() == QNetworkReply::NoError) {
		requests.remove(reply);

		mtpBuffer data = _handleHttpResponse(reply);
		if (data.size() == 1) {
			if (status == WaitingBoth) {
				status = WaitingTcp;
			} else {
				emit error();
			}
		} else if (!data.isEmpty()) {
			if (status == UsingHttp) {
				receivedQueue.push_back(data);
				emit receivedData();
			} else if (status == WaitingBoth || status == WaitingHttp) {
				try {
					MTPResPQ res_pq = _readPQFakeReply(data);
					const MTPDresPQ &res_pq_data(res_pq.c_resPQ());
					if (res_pq_data.vnonce == httpNonce) {
						if (status == WaitingBoth) {
							status = HttpReady;
							httpStartTimer.start(MTPTcpConnectionWaitTimeout);
						} else {
							DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by pq-response, awaited").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
							status = UsingHttp;
							sock.disconnectFromHost();
							emit connected();
						}
					}
				} catch (Exception &e) {
					DEBUG_LOG(("Connection Error: exception in parsing HTTP fake pq-responce, %1").arg(e.what()));
					if (status == WaitingBoth) {
						status = WaitingTcp;
					} else {
						emit error();
					}
				}
			} else if (status == UsingTcp) {
				DEBUG_LOG(("Connection Info: already using tcp, ignoring http response"));
			}
		}
	} else {
		if (!requests.remove(reply)) {
			return;
		}

		bool mayBeBadKey = _handleHttpError(reply) && _sentEncrypted;
		if (status == WaitingBoth) {
			status = WaitingTcp;
		} else if (status == WaitingHttp || status == UsingHttp) {
			emit error(mayBeBadKey);
		} else {
			LOG(("Strange Http Error: status %1").arg(status));
		}
	}
}

void MTPautoConnection::socketPacket(mtpPrime *packet, uint32 size) {
	if (status == FinishedWork) return;

	mtpBuffer data = _handleTcpResponse(packet, size);
	if (data.size() == 1) {
		if (status == WaitingBoth) {
			status = WaitingHttp;
			sock.disconnectFromHost();
		} else if (status == HttpReady) {
			DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by bad tcp response, ready").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
			status = UsingHttp;
			sock.disconnectFromHost();
			emit connected();
		} else if (status == WaitingTcp || status == UsingTcp) {
			bool mayBeBadKey = (data[0] == -410) && _sentEncrypted;
			emit error(mayBeBadKey);
		} else {
			LOG(("Strange Tcp Error; status %1").arg(status));
		}
	} else if (status == UsingTcp) {
		receivedQueue.push_back(data);
		emit receivedData();
	} else if (status == WaitingBoth || status == WaitingTcp || status == HttpReady) {
		tcpTimeoutTimer.stop();
		try {
			MTPResPQ res_pq = _readPQFakeReply(data);
			const MTPDresPQ &res_pq_data(res_pq.c_resPQ());
			if (res_pq_data.vnonce == tcpNonce) {
				DEBUG_LOG(("Connection Info: TCP/%1-transport chosen by pq-response").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
				status = UsingTcp;
				emit connected();
			}
		} catch (Exception &e) {
			DEBUG_LOG(("Connection Error: exception in parsing TCP fake pq-responce, %1").arg(e.what()));
			if (status == WaitingBoth) {
				status = WaitingHttp;
				sock.disconnectFromHost();
			} else if (status == HttpReady) {
				DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by bad tcp response, awaited").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
				status = UsingHttp;
				sock.disconnectFromHost();
				emit connected();
			} else {
				emit error();
			}
		}
	}
}

bool MTPautoConnection::usingHttpWait() {
	return (status == UsingHttp);
}

bool MTPautoConnection::needHttpWait() {
	return (status == UsingHttp) ? requests.isEmpty() : false;
}

int32 MTPautoConnection::debugState() const {
	return (status == UsingHttp) ? -1 : (UsingTcp ? sock.state() : -777);
}

QString MTPautoConnection::transport() const {
	if (status == UsingTcp) {
		return qsl("TCP");
	} else if (status == UsingHttp) {
		return qsl("HTTP");
	} else {
		return QString();
	}
}

void MTPautoConnection::socketError(QAbstractSocket::SocketError e) {
	if (status == FinishedWork) return;

	_handleTcpError(e, sock);
	if (status == WaitingBoth) {
		status = WaitingHttp;
	} else if (status == HttpReady) {
		DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by tcp error, ready").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
		status = UsingHttp;
		emit connected();
	} else if (status == WaitingTcp || status == UsingTcp) {
		emit error();
	} else {
		LOG(("Strange Tcp Error: status %1").arg(status));
	}
}

MTPtcpConnection::MTPtcpConnection(QThread *thread) : status(WaitingTcp),
tcpNonce(MTP::nonce<MTPint128>()), _tcpTimeout(MTPMinReceiveDelay), _flags(0) {
	moveToThread(thread);

	tcpTimeoutTimer.moveToThread(thread);
	tcpTimeoutTimer.setSingleShot(true);
	connect(&tcpTimeoutTimer, SIGNAL(timeout()), this, SLOT(onTcpTimeoutTimer()));

	sock.moveToThread(thread);
	App::setProxySettings(sock);
	connect(&sock, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
	connect(&sock, SIGNAL(connected()), this, SLOT(onSocketConnected()));
	connect(&sock, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
}

void MTPtcpConnection::onSocketConnected() {
	if (status == WaitingTcp) {
		mtpBuffer buffer(_preparePQFake(tcpNonce));

		DEBUG_LOG(("Connection Info: sending fake req_pq through TCP/%1 transport").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));

		if (_tcpTimeout < 0) _tcpTimeout = -_tcpTimeout;
		tcpTimeoutTimer.start(_tcpTimeout);

		sendData(buffer);
	}
}

void MTPtcpConnection::onTcpTimeoutTimer() {
	if (status == WaitingTcp) {
		if (_tcpTimeout < MTPMaxReceiveDelay) _tcpTimeout *= 2;
		_tcpTimeout = -_tcpTimeout;

		QAbstractSocket::SocketState state = sock.state();
		if (state == QAbstractSocket::ConnectedState || state == QAbstractSocket::ConnectingState || state == QAbstractSocket::HostLookupState) {
			sock.disconnectFromHost();
		} else if (state != QAbstractSocket::ClosingState) {
			sock.connectToHost(QHostAddress(_addr), _port);
		}
	}
}

void MTPtcpConnection::onSocketDisconnected() {
	if (_tcpTimeout < 0) {
		_tcpTimeout = -_tcpTimeout;
		if (status == WaitingTcp) {
			sock.connectToHost(QHostAddress(_addr), _port);
			return;
		}
	}
	if (status == WaitingTcp || status == UsingTcp) {
		emit disconnected();
	}
}

void MTPtcpConnection::sendData(mtpBuffer &buffer) {
	if (status == FinishedWork) return;

	if (buffer.size() < 3) {
		LOG(("TCP Error: writing bad packet, len = %1").arg(buffer.size() * sizeof(mtpPrime)));
		TCP_LOG(("TCP Error: bad packet %1").arg(Logs::mb(&buffer[0], buffer.size() * sizeof(mtpPrime)).str()));
		emit error();
		return;
	}

	uint32 size = buffer.size(), len = size * 4;

	buffer[0] = len;
	buffer[1] = packetNum++;
	buffer[size - 1] = hashCrc32(&buffer[0], len - 4);
	TCP_LOG(("TCP Info: write %1 packet %2 bytes %3").arg(packetNum).arg(len).arg(Logs::mb(&buffer[0], len).str()));

	sock.write((const char*)&buffer[0], len);
}

void MTPtcpConnection::disconnectFromServer() {
	if (status == FinishedWork) return;
	status = FinishedWork;

	disconnect(&sock, SIGNAL(readyRead()), 0, 0);
	sock.close();
}

void MTPtcpConnection::connectToServer(const QString &addr, int32 port, int32 flags) {
	_addr = addr;
	_port = port;
	_flags = flags;

	connect(&sock, SIGNAL(readyRead()), this, SLOT(socketRead()));
	sock.connectToHost(QHostAddress(_addr), _port);
}

void MTPtcpConnection::socketPacket(mtpPrime *packet, uint32 size) {
	if (status == FinishedWork) return;

	mtpBuffer data = _handleTcpResponse(packet, size);
	if (data.size() == 1) {
		bool mayBeBadKey = (data[0] == -410) && _sentEncrypted;
		emit error(mayBeBadKey);
	} else if (status == UsingTcp) {
		receivedQueue.push_back(data);
		emit receivedData();
	} else if (status == WaitingTcp) {
		tcpTimeoutTimer.stop();
		try {
			MTPResPQ res_pq = _readPQFakeReply(data);
			const MTPDresPQ &res_pq_data(res_pq.c_resPQ());
			if (res_pq_data.vnonce == tcpNonce) {
				DEBUG_LOG(("Connection Info: TCP/%1-transport chosen by pq-response").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
				status = UsingTcp;
				emit connected();
			}
		} catch (Exception &e) {
			DEBUG_LOG(("Connection Error: exception in parsing TCP fake pq-responce, %1").arg(e.what()));
			emit error();
		}
	}
}

bool MTPtcpConnection::isConnected() const {
	return (status == UsingTcp);
}

int32 MTPtcpConnection::debugState() const {
	return sock.state();
}

QString MTPtcpConnection::transport() const {
	return isConnected() ? qsl("TCP") : QString();
}

void MTPtcpConnection::socketError(QAbstractSocket::SocketError e) {
	if (status == FinishedWork) return;

	_handleTcpError(e, sock);
	emit error();
}

MTPhttpConnection::MTPhttpConnection(QThread *thread) : status(WaitingHttp), httpNonce(MTP::nonce<MTPint128>()), _flags(0) {
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);
}

void MTPhttpConnection::sendData(mtpBuffer &buffer) {
	if (status == FinishedWork) return;

	if (buffer.size() < 3) {
		LOG(("TCP Error: writing bad packet, len = %1").arg(buffer.size() * sizeof(mtpPrime)));
		TCP_LOG(("TCP Error: bad packet %1").arg(Logs::mb(&buffer[0], buffer.size() * sizeof(mtpPrime)).str()));
		emit error();
		return;
	}

	int32 requestSize = (buffer.size() - 3) * sizeof(mtpPrime);

	QNetworkRequest request(address);
	request.setHeader(QNetworkRequest::ContentLengthHeader, QVariant(requestSize));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(qsl("application/x-www-form-urlencoded")));

	TCP_LOG(("HTTP Info: sending %1 len request %2").arg(requestSize).arg(Logs::mb(&buffer[2], requestSize).str()));
	requests.insert(manager.post(request, QByteArray((const char*)(&buffer[2]), requestSize)));
}

void MTPhttpConnection::disconnectFromServer() {
	if (status == FinishedWork) return;
	status = FinishedWork;

	Requests copy = requests;
	requests.clear();
	for (Requests::const_iterator i = copy.cbegin(), e = copy.cend(); i != e; ++i) {
		(*i)->abort();
		(*i)->deleteLater();
	}

	disconnect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestFinished(QNetworkReply*)));

	address = QUrl();
}

void MTPhttpConnection::connectToServer(const QString &addr, int32 p, int32 flags) {
	address = QUrl(((flags & MTPDdcOption::flag_ipv6) ? qsl("http://[%1]:%2/api") : qsl("http://%1:%2/api")).arg(addr).arg(80));//not p - always 80 port for http transport
	TCP_LOG(("HTTP Info: address is %1").arg(address.toDisplayString()));
	connect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestFinished(QNetworkReply*)));

	_flags = flags;

	mtpBuffer buffer(_preparePQFake(httpNonce));

	DEBUG_LOG(("Connection Info: sending fake req_pq through HTTP/%1 transport").arg((flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));

	sendData(buffer);
}

bool MTPhttpConnection::isConnected() const {
	return (status == UsingHttp);
}

void MTPhttpConnection::requestFinished(QNetworkReply *reply) {
	if (status == FinishedWork) return;

	reply->deleteLater();
	if (reply->error() == QNetworkReply::NoError) {
		requests.remove(reply);

		mtpBuffer data = _handleHttpResponse(reply);
		if (data.size() == 1) {
			emit error();
		} else if (!data.isEmpty()) {
			if (status == UsingHttp) {
				receivedQueue.push_back(data);
				emit receivedData();
			} else {
				try {
					MTPResPQ res_pq = _readPQFakeReply(data);
					const MTPDresPQ &res_pq_data(res_pq.c_resPQ());
					if (res_pq_data.vnonce == httpNonce) {
						DEBUG_LOG(("Connection Info: HTTP/%1-transport connected by pq-response").arg((_flags & MTPDdcOption::flag_ipv6) ? "IPv6" : "IPv4"));
						status = UsingHttp;
						emit connected();
					}
				} catch (Exception &e) {
					DEBUG_LOG(("Connection Error: exception in parsing HTTP fake pq-responce, %1").arg(e.what()));
					emit error();
				}
			}
		}
	} else {
		if (!requests.remove(reply)) {
			return;
		}

		bool mayBeBadKey = _handleHttpError(reply) && _sentEncrypted;

		emit error(mayBeBadKey);
	}
}

bool MTPhttpConnection::usingHttpWait() {
	return true;
}

bool MTPhttpConnection::needHttpWait() {
	return requests.isEmpty();
}

int32 MTPhttpConnection::debugState() const {
	return -1;
}

QString MTPhttpConnection::transport() const {
	if (status == UsingHttp) {
		return qsl("HTTP");
	} else {
		return QString();
	}
}

void MTProtoConnectionPrivate::createConn(bool createIPv4, bool createIPv6) {
	destroyConn();
	if (createIPv4) {
		QWriteLocker lock(&stateConnMutex);
		if (cConnectionType() == dbictAuto) {
			_conn4 = new MTPautoConnection(thread());
		} else if (cConnectionType() == dbictTcpProxy) {
			_conn4 = new MTPtcpConnection(thread());
		} else {
			_conn4 = new MTPhttpConnection(thread());
		}
		connect(_conn4, SIGNAL(error(bool)), this, SLOT(onError4(bool)));
		connect(_conn4, SIGNAL(receivedSome()), this, SLOT(onReceivedSome()));
	}
	if (createIPv6) {
		QWriteLocker lock(&stateConnMutex);
		if (cConnectionType() == dbictAuto) {
			_conn6 = new MTPautoConnection(thread());
		} else if (cConnectionType() == dbictTcpProxy) {
			_conn6 = new MTPtcpConnection(thread());
		} else {
			_conn6 = new MTPhttpConnection(thread());
		}
		connect(_conn6, SIGNAL(error(bool)), this, SLOT(onError6(bool)));
		connect(_conn6, SIGNAL(receivedSome()), this, SLOT(onReceivedSome()));
	}
	firstSentAt = 0;
	if (oldConnection) {
		oldConnection = false;
		DEBUG_LOG(("This connection marked as not old!"));
	}
	oldConnectionTimer.start(MTPConnectionOldTimeout);
}

void MTProtoConnectionPrivate::destroyConn(MTPabstractConnection **conn) {
	if (conn) {
		MTPabstractConnection *toDisconnect = 0;

		{
			QWriteLocker lock(&stateConnMutex);
			if (*conn) {
				toDisconnect = *conn;
				disconnect(*conn, SIGNAL(connected()), 0, 0);
				disconnect(*conn, SIGNAL(disconnected()), 0, 0);
				disconnect(*conn, SIGNAL(error(bool)), 0, 0);
				disconnect(*conn, SIGNAL(receivedData()), 0, 0);
				disconnect(*conn, SIGNAL(receivedSome()), 0, 0);
				*conn = 0;
			}
		}
		if (toDisconnect) {
			toDisconnect->disconnectFromServer();
			toDisconnect->deleteLater();
		}
	} else {
		destroyConn(&_conn4);
		destroyConn(&_conn6);
		_conn = 0;
	}
}

MTProtoConnectionPrivate::MTProtoConnectionPrivate(QThread *thread, MTProtoConnection *owner, MTPSessionData *data, uint32 _dc)
	: QObject(0)
	, _state(MTProtoConnection::Disconnected)
	, _needSessionReset(false)
	, dc(_dc)
    , _owner(owner)
	, _conn(0), _conn4(0), _conn6(0)
    , retryTimeout(1)
    , oldConnection(true)
    , _waitForReceived(MTPMinReceiveDelay)
	, _waitForConnected(MTPMinConnectDelay)
    , firstSentAt(-1)
    , _pingId(0)
	, _pingIdToSend(0)
	, _pingSendAt(0)
    , _pingMsgId(0)
    , restarted(false)
    , keyId(0)
//	, sessionDataMutex(QReadWriteLock::Recursive)
    , sessionData(data)
    , myKeyLock(false)
	, authKeyData(0)
	, authKeyStrings(0) {

	oldConnectionTimer.moveToThread(thread);
	_waitForConnectedTimer.moveToThread(thread);
	_waitForReceivedTimer.moveToThread(thread);
	_waitForIPv4Timer.moveToThread(thread);
	_pingSender.moveToThread(thread);
	retryTimer.moveToThread(thread);
	moveToThread(thread);

	if (!dc) {
		QReadLocker lock(mtpDcOptionsMutex());
		const mtpDcOptions &options(cDcOptions());
		if (options.isEmpty()) {
			LOG(("MTP Error: connect failed, no DCs"));
			dc = 0;
			return;
		}
		dc = options.cbegin().value().id;
		DEBUG_LOG(("MTP Info: searching for any DC, %1 selected..").arg(dc));
	}

	connect(thread, SIGNAL(started()), this, SLOT(socketStart()));
	connect(thread, SIGNAL(finished()), this, SLOT(doFinish()));

	connect(&retryTimer, SIGNAL(timeout()), this, SLOT(retryByTimer()));
	connect(&_waitForConnectedTimer, SIGNAL(timeout()), this, SLOT(onWaitConnectedFailed()));
	connect(&_waitForReceivedTimer, SIGNAL(timeout()), this, SLOT(onWaitReceivedFailed()));
	connect(&_waitForIPv4Timer, SIGNAL(timeout()), this, SLOT(onWaitIPv4Failed()));
	connect(&oldConnectionTimer, SIGNAL(timeout()), this, SLOT(onOldConnection()));
	connect(&_pingSender, SIGNAL(timeout()), this, SLOT(onPingSender()));
	connect(sessionData->owner(), SIGNAL(authKeyCreated()), this, SLOT(updateAuthKey()), Qt::QueuedConnection);

	connect(sessionData->owner(), SIGNAL(needToRestart()), this, SLOT(restartNow()), Qt::QueuedConnection);
	connect(this, SIGNAL(needToReceive()), sessionData->owner(), SLOT(tryToReceive()), Qt::QueuedConnection);
	connect(this, SIGNAL(stateChanged(qint32)), sessionData->owner(), SLOT(onConnectionStateChange(qint32)), Qt::QueuedConnection);
	connect(sessionData->owner(), SIGNAL(needToSend()), this, SLOT(tryToSend()), Qt::QueuedConnection);
	connect(sessionData->owner(), SIGNAL(needToPing()), this, SLOT(onPingSendForce()), Qt::QueuedConnection);
	connect(this, SIGNAL(sessionResetDone()), sessionData->owner(), SLOT(onResetDone()), Qt::QueuedConnection);

	static bool _registered = false;
	if (!_registered) {
		_registered = true;
        qRegisterMetaType<QVector<quint64> >("QVector<quint64>");
	}

	connect(this, SIGNAL(needToSendAsync()), sessionData->owner(), SLOT(needToResumeAndSend()), Qt::QueuedConnection);
	connect(this, SIGNAL(sendAnythingAsync(quint64)), sessionData->owner(), SLOT(sendAnything(quint64)), Qt::QueuedConnection);
	connect(this, SIGNAL(sendHttpWaitAsync()), sessionData->owner(), SLOT(sendAnything()), Qt::QueuedConnection);
	connect(this, SIGNAL(sendPongAsync(quint64,quint64)), sessionData->owner(), SLOT(sendPong(quint64,quint64)), Qt::QueuedConnection);
	connect(this, SIGNAL(sendMsgsStateInfoAsync(quint64, QByteArray)), sessionData->owner(), SLOT(sendMsgsStateInfo(quint64,QByteArray)), Qt::QueuedConnection);
	connect(this, SIGNAL(resendAsync(quint64,quint64,bool,bool)), sessionData->owner(), SLOT(resend(quint64,quint64,bool,bool)), Qt::QueuedConnection);
	connect(this, SIGNAL(resendManyAsync(QVector<quint64>,quint64,bool,bool)), sessionData->owner(), SLOT(resendMany(QVector<quint64>,quint64,bool,bool)), Qt::QueuedConnection);
	connect(this, SIGNAL(resendAllAsync()), sessionData->owner(), SLOT(resendAll()));
}

void MTProtoConnectionPrivate::onConfigLoaded() {
	socketStart(true);
}

int32 MTProtoConnectionPrivate::getDC() const {
	return dc;
}

int32 MTProtoConnectionPrivate::getState() const {
	QReadLocker lock(&stateConnMutex);
	int32 result = _state;
	if (_state < 0) {
		if (retryTimer.isActive()) {
			result = int32(getms(true) - retryWillFinish);
			if (result >= 0) {
				result = -1;
			}
		}
	}
	return result;
}

QString MTProtoConnectionPrivate::transport() const {
	QReadLocker lock(&stateConnMutex);
	if ((!_conn4 && !_conn6) || (_conn4 && _conn6) || (_state < 0)) {
		return QString();
	}
	QString result = (_conn4 ? _conn4 : _conn6)->transport();
	if (!result.isEmpty() && cTryIPv6()) result += (_conn4 ? "/IPv4" : "/IPv6");
	return result;
}

bool MTProtoConnectionPrivate::setState(int32 state, int32 ifState) {
	if (ifState != MTProtoConnection::UpdateAlways) {
		QReadLocker lock(&stateConnMutex);
		if (_state != ifState) return false;
	}
	QWriteLocker lock(&stateConnMutex);
	if (_state == state) return false;
	_state = state;
	if (state < 0) {
		retryTimeout = -state;
		retryTimer.start(retryTimeout);
		retryWillFinish = getms(true) + retryTimeout;
	}
	emit stateChanged(state);
	return true;
}

void MTProtoConnectionPrivate::resetSession() { // recreate all msg_id and msg_seqno
	_needSessionReset = false;

	QWriteLocker locker1(sessionData->haveSentMutex());
	QWriteLocker locker2(sessionData->toResendMutex());
	QWriteLocker locker3(sessionData->toSendMutex());
	QWriteLocker locker4(sessionData->wereAckedMutex());
	mtpRequestMap &haveSent(sessionData->haveSentMap());
	mtpRequestIdsMap &toResend(sessionData->toResendMap());
	mtpPreRequestMap &toSend(sessionData->toSendMap());
	mtpRequestIdsMap &wereAcked(sessionData->wereAckedMap());

	mtpMsgId newId = msgid();
	mtpRequestMap setSeqNumbers;
	typedef QMap<mtpMsgId, mtpMsgId> Replaces;
	Replaces replaces;
	for (mtpRequestMap::const_iterator i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) {
		if (!mtpRequestData::isSentContainer(i.value())) {
			if (!*(mtpMsgId*)(i.value()->constData() + 4)) continue;

			mtpMsgId id = i.key();
			if (id > newId) {
				while (true) {
					if (toResend.constFind(newId) == toResend.cend() && wereAcked.constFind(newId) == wereAcked.cend() && haveSent.constFind(newId) == haveSent.cend()) {
						break;
					}
					mtpMsgId m = msgid();
					if (m <= newId) break; // wtf

					newId = m;
				}

				MTP_LOG(dc, ("Replacing msgId %1 to %2!").arg(id).arg(newId));
				replaces.insert(id, newId);
				id = newId;
				*(mtpMsgId*)(i.value()->data() + 4) = id;
			}
			setSeqNumbers.insert(id, i.value());
		}
	}
	for (mtpRequestIdsMap::const_iterator i = toResend.cbegin(), e = toResend.cend(); i != e; ++i) { // collect all non-container requests
		mtpPreRequestMap::const_iterator j = toSend.constFind(i.value());
		if (j == toSend.cend()) continue;

		if (!mtpRequestData::isSentContainer(j.value())) {
			if (!*(mtpMsgId*)(j.value()->constData() + 4)) continue;

			mtpMsgId id = i.key();
			if (id > newId) {
				while (true) {
					if (toResend.constFind(newId) == toResend.cend() && wereAcked.constFind(newId) == wereAcked.cend() && haveSent.constFind(newId) == haveSent.cend()) {
						break;
					}
					mtpMsgId m = msgid();
					if (m <= newId) break; // wtf

					newId = m;
				}

				MTP_LOG(dc, ("Replacing msgId %1 to %2!").arg(id).arg(newId));
				replaces.insert(id, newId);
				id = newId;
				*(mtpMsgId*)(j.value()->data() + 4) = id;
			}
			setSeqNumbers.insert(id, j.value());
		}
	}

	uint64 session = MTP::nonce<uint64>();
	DEBUG_LOG(("MTP Info: creating new session after bad_msg_notification, setting random server_session %1").arg(session));
	sessionData->setSession(session);

	for (mtpRequestMap::const_iterator i = setSeqNumbers.cbegin(), e = setSeqNumbers.cend(); i != e; ++i) { // generate new seq_numbers
		bool wasNeedAck = (*(i.value()->data() + 6) & 1);
		*(i.value()->data() + 6) = sessionData->nextRequestSeqNumber(wasNeedAck);
	}
	if (!replaces.isEmpty()) {
		for (Replaces::const_iterator i = replaces.cbegin(), e = replaces.cend(); i != e; ++i) { // replace msgIds keys in all data structs
			mtpRequestMap::iterator j = haveSent.find(i.key());
			if (j != haveSent.cend()) {
				mtpRequest req = j.value();
				haveSent.erase(j);
				haveSent.insert(i.value(), req);
			}
			mtpRequestIdsMap::iterator k = toResend.find(i.key());
			if (k != toResend.cend()) {
				mtpRequestId req = k.value();
				toResend.erase(k);
				toResend.insert(i.value(), req);
			}
			k = wereAcked.find(i.key());
			if (k != wereAcked.cend()) {
				mtpRequestId req = k.value();
				wereAcked.erase(k);
				wereAcked.insert(i.value(), req);
			}
		}
		for (mtpRequestMap::const_iterator i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) { // replace msgIds in saved containers
			if (mtpRequestData::isSentContainer(i.value())) {
				mtpMsgId *ids = (mtpMsgId *)(i.value()->data() + 8);
				for (uint32 j = 0, l = (i.value()->size() - 8) >> 1; j < l; ++j) {
					Replaces::const_iterator k = replaces.constFind(ids[j]);
					if (k != replaces.cend()) {
						ids[j] = k.value();
					}
				}
			}
		}
	}

	ackRequestData.clear();
	resendRequestData.clear();
	{
		QWriteLocker locker5(sessionData->stateRequestMutex());
		sessionData->stateRequestMap().clear();
	}

	emit sessionResetDone();
}

mtpMsgId MTProtoConnectionPrivate::prepareToSend(mtpRequest &request, mtpMsgId currentLastId) {
	if (request->size() < 9) return 0;
	mtpMsgId msgId = *(mtpMsgId*)(request->constData() + 4);
	if (msgId) { // resending this request
		QWriteLocker locker(sessionData->toResendMutex());
		mtpRequestIdsMap &toResend(sessionData->toResendMap());
		mtpRequestIdsMap::iterator i = toResend.find(msgId);
		if (i != toResend.cend()) {
			toResend.erase(i);
		}
	} else {
		msgId = *(mtpMsgId*)(request->data() + 4) = currentLastId;
		*(request->data() + 6) = sessionData->nextRequestSeqNumber(mtpRequestData::needAck(request));
	}
	return msgId;
}

mtpMsgId MTProtoConnectionPrivate::replaceMsgId(mtpRequest &request, mtpMsgId newId) {
	if (request->size() < 9) return 0;

	mtpMsgId oldMsgId = *(mtpMsgId*)(request->constData() + 4);
	if (oldMsgId != newId) {
		if (oldMsgId) {
			QWriteLocker locker(sessionData->toResendMutex());
			// haveSentMutex() and wereAckedMutex() were locked in tryToSend()

			mtpRequestIdsMap &toResend(sessionData->toResendMap());
			mtpRequestIdsMap &wereAcked(sessionData->wereAckedMap());
			mtpRequestMap &haveSent(sessionData->haveSentMap());

			while (true) {
				if (toResend.constFind(newId) == toResend.cend() && wereAcked.constFind(newId) == wereAcked.cend() && haveSent.constFind(newId) == haveSent.cend()) {
					break;
				}
				mtpMsgId m = msgid();
				if (m <= newId) break; // wtf

				newId = m;
			}

			mtpRequestIdsMap::iterator i = toResend.find(oldMsgId);
			if (i != toResend.cend()) {
				mtpRequestId req = i.value();
				toResend.erase(i);
				toResend.insert(newId, req);
			}

			mtpRequestIdsMap::iterator j = wereAcked.find(oldMsgId);
			if (j != wereAcked.cend()) {
				mtpRequestId req = j.value();
				wereAcked.erase(j);
				wereAcked.insert(newId, req);
			}

			mtpRequestMap::iterator k = haveSent.find(oldMsgId);
			if (k != haveSent.cend()) {
				mtpRequest req = k.value();
				haveSent.erase(k);
				haveSent.insert(newId, req);
			}

			for (k = haveSent.begin(); k != haveSent.cend(); ++k) {
				mtpRequest req(k.value());
				if (mtpRequestData::isSentContainer(req)) {
					mtpMsgId *ids = (mtpMsgId *)(req->data() + 8);
					for (uint32 i = 0, l = (req->size() - 8) >> 1; i < l; ++i) {
						if (ids[i] == oldMsgId) {
							ids[i] = newId;
						}
					}
				}
			}
		} else {
			*(request->data() + 6) = sessionData->nextRequestSeqNumber(mtpRequestData::needAck(request));
		}
		*(mtpMsgId*)(request->data() + 4) = newId;
	}
	return newId;
}

mtpMsgId MTProtoConnectionPrivate::placeToContainer(mtpRequest &toSendRequest, mtpMsgId &bigMsgId, mtpMsgId *&haveSentArr, mtpRequest &req) {
	mtpMsgId msgId = prepareToSend(req, bigMsgId);
	if (msgId > bigMsgId) msgId = replaceMsgId(req, bigMsgId);
	if (msgId >= bigMsgId) bigMsgId = msgid();
	*(haveSentArr++) = msgId;

	uint32 from = toSendRequest->size(), len = mtpRequestData::messageSize(req);
	toSendRequest->resize(from + len);
	memcpy(toSendRequest->data() + from, req->constData() + 4, len * sizeof(mtpPrime));

	return msgId;
}

void MTProtoConnectionPrivate::tryToSend() {
	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData || !_conn) {
		return;
	}

	bool needsLayer = !sessionData->layerWasInited();
	int32 state = getState();
	bool prependOnly = (state != MTProtoConnection::Connected);
	mtpRequest pingRequest;
	if (dc < _mtp_internal::dcShift) { // main session
		if (!prependOnly && !_pingIdToSend && !_pingId && _pingSendAt <= getms(true)) {
			_pingIdToSend = MTP::nonce<mtpPingId>();
		}
	}
	if (_pingIdToSend) {
		if (prependOnly || dc >= _mtp_internal::dcShift) {
			MTPPing ping(MTPping(MTP_long(_pingIdToSend)));
			uint32 pingSize = ping.innerLength() >> 2; // copy from MTProtoSession::send
			pingRequest = mtpRequestData::prepare(pingSize);
			ping.write(*pingRequest);
			DEBUG_LOG(("MTP Info: sending ping, ping_id: %1").arg(_pingIdToSend));
		} else {
			MTPPing_delay_disconnect ping(MTP_long(_pingIdToSend), MTP_int(MTPPingDelayDisconnect));
			uint32 pingSize = ping.innerLength() >> 2; // copy from MTProtoSession::send
			pingRequest = mtpRequestData::prepare(pingSize);
			ping.write(*pingRequest);
			DEBUG_LOG(("MTP Info: sending ping_delay_disconnect, ping_id: %1").arg(_pingIdToSend));
		}

		pingRequest->msDate = getms(true); // > 0 - can send without container
		_pingSendAt = pingRequest->msDate + (MTPPingSendAfterAuto * 1000ULL);
		pingRequest->requestId = 0; // dont add to haveSent / wereAcked maps

		if (dc < _mtp_internal::dcShift && !prependOnly) { // main session
			_pingSender.start(MTPPingSendAfter * 1000);
		}

		_pingId = _pingIdToSend;
		_pingIdToSend = 0;
	} else {
		if (prependOnly) {
			DEBUG_LOG(("MTP Info: dc %1 not sending, waiting for Connected state, state: %2").arg(dc).arg(state));
			return; // just do nothing, if is not connected yet
		} else {
			DEBUG_LOG(("MTP Info: dc %1 trying to send after ping, state: %2").arg(dc).arg(state));
		}
	}

	mtpRequest ackRequest, resendRequest, stateRequest, httpWaitRequest;
	if (!prependOnly && !ackRequestData.isEmpty()) {
		MTPMsgsAck ack(MTP_msgs_ack(MTP_vector<MTPlong>(ackRequestData)));

		ackRequest = mtpRequestData::prepare(ack.innerLength() >> 2);
		ack.write(*ackRequest);

		ackRequest->msDate = getms(true); // > 0 - can send without container
		ackRequest->requestId = 0; // dont add to haveSent / wereAcked maps

		ackRequestData.clear();
	}
	if (!prependOnly && !resendRequestData.isEmpty()) {
		MTPMsgResendReq resend(MTP_msg_resend_req(MTP_vector<MTPlong>(resendRequestData)));

		resendRequest = mtpRequestData::prepare(resend.innerLength() >> 2);
		resend.write(*resendRequest);

		resendRequest->msDate = getms(true); // > 0 - can send without container
		resendRequest->requestId = 0; // dont add to haveSent / wereAcked maps

		resendRequestData.clear();
	}
	if (!prependOnly) {
		QVector<MTPlong> stateReq;
		{
			QWriteLocker locker(sessionData->stateRequestMutex());
			mtpMsgIdsSet &ids(sessionData->stateRequestMap());
			if (!ids.isEmpty()) {
				stateReq.reserve(ids.size());
				for (mtpMsgIdsSet::const_iterator i = ids.cbegin(), e = ids.cend(); i != e; ++i) {
					stateReq.push_back(MTP_long(i.key()));
				}
			}
			ids.clear();
		}
		if (!stateReq.isEmpty()) {
			MTPMsgsStateReq req(MTP_msgs_state_req(MTP_vector<MTPlong>(stateReq)));

			stateRequest = mtpRequestData::prepare(req.innerLength() >> 2);
			req.write(*stateRequest);

			stateRequest->msDate = getms(true); // > 0 - can send without container
			stateRequest->requestId = reqid();// add to haveSent / wereAcked maps, but don't add to requestMap
		}
		if (_conn->usingHttpWait()) {
			MTPHttpWait req(MTP_http_wait(MTP_int(100), MTP_int(30), MTP_int(25000)));

			httpWaitRequest = mtpRequestData::prepare(req.innerLength() >> 2);
			req.write(*httpWaitRequest);

			httpWaitRequest->msDate = getms(true); // > 0 - can send without container
			httpWaitRequest->requestId = 0; // dont add to haveSent / wereAcked maps
		}
	}

	MTPInitConnection<mtpRequest> initWrapperImpl, *initWrapper = &initWrapperImpl;
	int32 initSize = 0, initSizeInInts = 0;
	if (needsLayer) {
		initWrapperImpl = MTPInitConnection<mtpRequest>(MTP_int(ApiId), MTP_string(cApiDeviceModel()), MTP_string(cApiSystemVersion()), MTP_string(cApiAppVersion()), MTP_string(ApiLang), mtpRequest());
		initSizeInInts = (initWrapper->innerLength() >> 2) + 2;
		initSize = initSizeInInts * sizeof(mtpPrime);
	}

	bool needAnyResponse = false;
	mtpRequest toSendRequest;
	{
		QWriteLocker locker1(sessionData->toSendMutex());

		mtpPreRequestMap toSendDummy, &toSend(prependOnly ? toSendDummy : sessionData->toSendMap());
		if (prependOnly) locker1.unlock();

		uint32 toSendCount = toSend.size();
		if (pingRequest) ++toSendCount;
		if (ackRequest) ++toSendCount;
		if (resendRequest) ++toSendCount;
		if (stateRequest) ++toSendCount;
		if (httpWaitRequest) ++toSendCount;

		if (!toSendCount) return; // nothing to send

		mtpRequest first = pingRequest ? pingRequest : (ackRequest ? ackRequest : (resendRequest ? resendRequest : (stateRequest ? stateRequest : (httpWaitRequest ? httpWaitRequest : toSend.cbegin().value()))));
		if (toSendCount == 1 && first->msDate > 0) { // if can send without container
			toSendRequest = first;
			if (!prependOnly) {
				toSend.clear();
				locker1.unlock();
			}

			mtpMsgId msgId = prepareToSend(toSendRequest, msgid());
			if (pingRequest) {
				_pingMsgId = msgId;
				needAnyResponse = true;
			} else if (resendRequest || stateRequest) {
				needAnyResponse = true;
			}

			if (toSendRequest->requestId) {
				if (mtpRequestData::needAck(toSendRequest)) {
					toSendRequest->msDate = mtpRequestData::isStateRequest(toSendRequest) ? 0 : getms(true);

					QWriteLocker locker2(sessionData->haveSentMutex());
					mtpRequestMap &haveSent(sessionData->haveSentMap());
					haveSent.insert(msgId, toSendRequest);

					if (needsLayer && !toSendRequest->needsLayer) needsLayer = false;
					if (toSendRequest->after) {
						int32 toSendSize = toSendRequest.innerLength() >> 2;
						mtpRequest wrappedRequest(mtpRequestData::prepare(toSendSize, toSendSize + 3)); // cons + msg_id
						wrappedRequest->resize(4);
						memcpy(wrappedRequest->data(), toSendRequest->constData(), 4 * sizeof(mtpPrime));
						_mtp_internal::wrapInvokeAfter(wrappedRequest, toSendRequest, haveSent);
						toSendRequest = wrappedRequest;
					}
					if (needsLayer) {
						int32 noWrapSize = (toSendRequest.innerLength() >> 2), toSendSize = noWrapSize + initSizeInInts;
						mtpRequest wrappedRequest(mtpRequestData::prepare(toSendSize));
						memcpy(wrappedRequest->data(), toSendRequest->constData(), 7 * sizeof(mtpPrime)); // all except length
						wrappedRequest->push_back(mtpc_invokeWithLayer);
						wrappedRequest->push_back(mtpCurrentLayer);
						initWrapper->write(*wrappedRequest);
						wrappedRequest->resize(wrappedRequest->size() + noWrapSize);
						memcpy(wrappedRequest->data() + wrappedRequest->size() - noWrapSize, toSendRequest->constData() + 8, noWrapSize * sizeof(mtpPrime));
						toSendRequest = wrappedRequest;
					}

					needAnyResponse = true;
				} else {
					QWriteLocker locker3(sessionData->wereAckedMutex());
					sessionData->wereAckedMap().insert(msgId, toSendRequest->requestId);
				}
			}
		} else { // send in container
			bool willNeedInit = false;
			uint32 containerSize = 1 + 1, idsWrapSize = (toSendCount << 1); // cons + vector size, idsWrapSize - size of "request-like" wrap for msgId vector
			if (pingRequest) containerSize += mtpRequestData::messageSize(pingRequest);
			if (ackRequest) containerSize += mtpRequestData::messageSize(ackRequest);
			if (resendRequest) containerSize += mtpRequestData::messageSize(resendRequest);
			if (stateRequest) containerSize += mtpRequestData::messageSize(stateRequest);
			if (httpWaitRequest) containerSize += mtpRequestData::messageSize(httpWaitRequest);
			for (mtpPreRequestMap::iterator i = toSend.begin(), e = toSend.end(); i != e; ++i) {
				containerSize += mtpRequestData::messageSize(i.value());
				if (needsLayer && i.value()->needsLayer) {
					containerSize += initSizeInInts;
					willNeedInit = true;
				}
			}
			mtpBuffer initSerialized;
			if (willNeedInit) {
				initSerialized.reserve(initSizeInInts);
				initSerialized.push_back(mtpc_invokeWithLayer);
				initSerialized.push_back(mtpCurrentLayer);
				initWrapper->write(initSerialized);
			}
			toSendRequest = mtpRequestData::prepare(containerSize, containerSize + 3 * toSend.size()); // prepare container + each in invoke after
			toSendRequest->push_back(mtpc_msg_container);
			toSendRequest->push_back(toSendCount);

			mtpMsgId bigMsgId = msgid(); // check for a valid container

			QWriteLocker locker2(sessionData->haveSentMutex()); // the fact of this lock is used in replaceMsgId()
			mtpRequestMap &haveSent(sessionData->haveSentMap());

			QWriteLocker locker3(sessionData->wereAckedMutex()); // the fact of this lock is used in replaceMsgId()
			mtpRequestIdsMap &wereAcked(sessionData->wereAckedMap());

			mtpRequest haveSentIdsWrap(mtpRequestData::prepare(idsWrapSize)); // prepare "request-like" wrap for msgId vector
			haveSentIdsWrap->requestId = 0;
			haveSentIdsWrap->resize(haveSentIdsWrap->size() + idsWrapSize);
			mtpMsgId *haveSentArr = (mtpMsgId*)(haveSentIdsWrap->data() + 8);

			if (pingRequest) {
				_pingMsgId = placeToContainer(toSendRequest, bigMsgId, haveSentArr, pingRequest);
				needAnyResponse = true;
			} else if (resendRequest || stateRequest) {
				needAnyResponse = true;
			}
			for (mtpPreRequestMap::iterator i = toSend.begin(), e = toSend.end(); i != e; ++i) {
				mtpRequest &req(i.value());
				mtpMsgId msgId = prepareToSend(req, bigMsgId);
				if (msgId > bigMsgId) msgId = replaceMsgId(req, bigMsgId);
				if (msgId >= bigMsgId) bigMsgId = msgid();
				*(haveSentArr++) = msgId;
				bool added = false;
				if (req->requestId) {
					if (mtpRequestData::needAck(req)) {
						req->msDate = mtpRequestData::isStateRequest(req) ? 0 : getms(true);
						int32 reqNeedsLayer = (needsLayer && req->needsLayer) ? toSendRequest->size() : 0;
						if (req->after) {
							_mtp_internal::wrapInvokeAfter(toSendRequest, req, haveSent, reqNeedsLayer ? initSizeInInts : 0);
							if (reqNeedsLayer) {
								memcpy(toSendRequest->data() + reqNeedsLayer + 4, initSerialized.constData(), initSize);
								*(toSendRequest->data() + reqNeedsLayer + 3) += initSize;
							}
							added = true;
						} else if (reqNeedsLayer) {
							toSendRequest->resize(reqNeedsLayer + initSizeInInts + mtpRequestData::messageSize(req));
							memcpy(toSendRequest->data() + reqNeedsLayer, req->constData() + 4, 4 * sizeof(mtpPrime));
							memcpy(toSendRequest->data() + reqNeedsLayer + 4, initSerialized.constData(), initSize);
							memcpy(toSendRequest->data() + reqNeedsLayer + 4 + initSizeInInts, req->constData() + 8, req.innerLength());
							*(toSendRequest->data() + reqNeedsLayer + 3) += initSize;
							added = true;
						}
						haveSent.insert(msgId, req);

						needAnyResponse = true;
					} else {
						wereAcked.insert(msgId, req->requestId);
					}
				}
				if (!added) {
					uint32 from = toSendRequest->size(), len = mtpRequestData::messageSize(req);
					toSendRequest->resize(from + len);
					memcpy(toSendRequest->data() + from, req->constData() + 4, len * sizeof(mtpPrime));
				}
			}
			if (stateRequest) {
				mtpMsgId msgId = placeToContainer(toSendRequest, bigMsgId, haveSentArr, stateRequest);
				stateRequest->msDate = 0; // 0 for state request, do not request state of it
				haveSent.insert(msgId, stateRequest);
			}
			if (resendRequest) placeToContainer(toSendRequest, bigMsgId, haveSentArr, resendRequest);
			if (ackRequest) placeToContainer(toSendRequest, bigMsgId, haveSentArr, ackRequest);
			if (httpWaitRequest) placeToContainer(toSendRequest, bigMsgId, haveSentArr, httpWaitRequest);

			mtpMsgId contMsgId = prepareToSend(toSendRequest, bigMsgId);
			*(mtpMsgId*)(haveSentIdsWrap->data() + 4) = contMsgId;
			(*haveSentIdsWrap)[6] = 0; // for container, msDate = 0, seqNo = 0
			haveSent.insert(contMsgId, haveSentIdsWrap);
			toSend.clear();
		}
	}
	mtpRequestData::padding(toSendRequest);
	sendRequest(toSendRequest, needAnyResponse, lockFinished);
}

void MTProtoConnectionPrivate::retryByTimer() {
	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData) return;

	if (retryTimeout < 3) {
		++retryTimeout;
	} else if (retryTimeout == 3) {
		retryTimeout = 1000;
	} else if (retryTimeout < 64000) {
		retryTimeout *= 2;
	}
	if (keyId == mtpAuthKey::RecreateKeyId) {
		if (sessionData->getKey()) {
			unlockKey();

			QWriteLocker lock(sessionData->keyMutex());
			sessionData->owner()->destroyKey();
		}
		keyId = 0;
	}
	socketStart();
}

void MTProtoConnectionPrivate::restartNow() {
	retryTimeout = 1;
	retryTimer.stop();
	restart();
}

void MTProtoConnectionPrivate::socketStart(bool afterConfig) {
	int32 flags4 = 0, flags6 = 0;
	std::string ip4, ip6;
	uint32 port4 = 0, port6 = 0;
	{
		QReadLocker lock(mtpDcOptionsMutex());
		const mtpDcOptions &options(cDcOptions());
		mtpDcOptions::const_iterator dcIndex4 = options.constFind(dc % _mtp_internal::dcShift);
		if (dcIndex4 != options.cend()) {
			ip4 = dcIndex4->ip;
			flags4 = dcIndex4->flags;
			port4 = dcIndex4->port;
		}
		mtpDcOptions::const_iterator dcIndex6 = options.constFind((dc % _mtp_internal::dcShift) + (_mtp_internal::dcShift * MTPDdcOption::flag_ipv6));
		if (dcIndex6 != options.cend()) {
			ip6 = dcIndex6->ip;
			flags6 = dcIndex6->flags;
			port6 = dcIndex6->port;
		}
	}
	bool noIPv4 = (!port4 || ip4.empty()), noIPv6 = (!cTryIPv6() || !port6 || ip6.empty());
	if (noIPv4 && noIPv6) {
		if (afterConfig) {
			if (noIPv4) LOG(("MTP Error: DC %1 options for IPv4 not found right after config load!").arg(dc));
			if (cTryIPv6() && noIPv6) LOG(("MTP Error: DC %1 options for IPv6 not found right after config load!").arg(dc));
			return restart();
		}
		if (noIPv4) DEBUG_LOG(("MTP Info: DC %1 options for IPv4 not found, waiting for config").arg(dc));
		if (cTryIPv6() && noIPv6) DEBUG_LOG(("MTP Info: DC %1 options for IPv6 not found, waiting for config").arg(dc));
		connect(mtpConfigLoader(), SIGNAL(loaded()), this, SLOT(onConfigLoaded()));
		mtpConfigLoader()->load();
		return;
	}

	if (afterConfig && (_conn4 || _conn6)) return;

	createConn(!noIPv4, !noIPv6);
	retryTimer.stop();
	_waitForConnectedTimer.stop();

	setState(MTProtoConnection::Connecting);
	_pingId = _pingMsgId = _pingIdToSend = _pingSendAt = 0;
	_pingSender.stop();

	if (!noIPv4) DEBUG_LOG(("MTP Info: creating IPv4 connection to %1:%2..").arg(ip4.c_str()).arg(port4));
	if (!noIPv6) DEBUG_LOG(("MTP Info: creating IPv6 connection to [%1]:%2..").arg(ip6.c_str()).arg(port6));

	_waitForConnectedTimer.start(_waitForConnected);
	if (_conn4) {
		connect(_conn4, SIGNAL(connected()), this, SLOT(onConnected4()));
		connect(_conn4, SIGNAL(disconnected()), this, SLOT(onDisconnected4()));
		_conn4->connectToServer(ip4.c_str(), port4, flags4);
	}
	if (_conn6) {
		connect(_conn6, SIGNAL(connected()), this, SLOT(onConnected6()));
		connect(_conn6, SIGNAL(disconnected()), this, SLOT(onDisconnected6()));
		_conn6->connectToServer(ip6.c_str(), port6, flags6);
	}
}

void MTProtoConnectionPrivate::restart(bool mayBeBadKey) {
	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData) return;

	DEBUG_LOG(("MTP Info: restarting MTProtoConnection, maybe bad key = %1").arg(Logs::b(mayBeBadKey)));

	_waitForReceivedTimer.stop();
	_waitForConnectedTimer.stop();

	mtpAuthKeyPtr key(sessionData->getKey());
	if (key) {
		if (!sessionData->isCheckedKey()) {
			if (mayBeBadKey) {
				clearMessages();
				keyId = mtpAuthKey::RecreateKeyId;
//				retryTimeout = 1; // no ddos please
				LOG(("MTP Info: key may be bad and was not checked - but won't be destroyed, no log outs because of bad server right now.."));
			}
		} else {
			sessionData->setCheckedKey(false);
		}
	}

	lockFinished.unlock();
	doDisconnect();

	lockFinished.relock();
	if (sessionData && _needSessionReset) {
		resetSession();
	}
	restarted = true;
	if (retryTimer.isActive()) return;

	DEBUG_LOG(("MTP Info: restart timeout: %1ms").arg(retryTimeout));
	setState(-retryTimeout);
}

void MTProtoConnectionPrivate::onSentSome(uint64 size) {
	if (!_waitForReceivedTimer.isActive()) {
		uint64 remain = _waitForReceived;
		if (!oldConnection) {
			uint64 remainBySize = size * _waitForReceived / 8192; // 8kb / sec, so 512 kb give 64 sec
			remain = snap(remainBySize, remain, uint64(MTPMaxReceiveDelay));
			if (remain != _waitForReceived) {
				DEBUG_LOG(("Checking connect for request with size %1 bytes, delay will be %2").arg(size).arg(remain));
			}
		}
		if (dc >= MTP::upl[0] && dc < MTP::upl[MTPUploadSessionsCount - 1] + _mtp_internal::dcShift) {
			remain *= MTPUploadSessionsCount;
		} else if (dc >= MTP::dld[0] && dc < MTP::dld[MTPDownloadSessionsCount - 1] + _mtp_internal::dcShift) {
			remain *= MTPDownloadSessionsCount;
		}
		_waitForReceivedTimer.start(remain);
	}
	if (!firstSentAt) firstSentAt = getms(true);
}

void MTProtoConnectionPrivate::onReceivedSome() {
	if (oldConnection) {
		oldConnection = false;
		DEBUG_LOG(("This connection marked as not old!"));
	}
	oldConnectionTimer.start(MTPConnectionOldTimeout);
	_waitForReceivedTimer.stop();
	if (firstSentAt > 0) {
		int32 ms = getms(true) - firstSentAt;
		DEBUG_LOG(("MTP Info: response in %1ms, _waitForReceived: %2ms").arg(ms).arg(_waitForReceived));

		if (ms > 0 && ms * 2 < int32(_waitForReceived)) _waitForReceived = qMax(ms * 2, int32(MTPMinReceiveDelay));
		firstSentAt = -1;
	}
}

void MTProtoConnectionPrivate::onOldConnection() {
	oldConnection = true;
	_waitForReceived = MTPMinReceiveDelay;
	DEBUG_LOG(("This connection marked as old! _waitForReceived now %1ms").arg(_waitForReceived));
}

void MTProtoConnectionPrivate::onPingSender() {
	if (_pingId) {
			if (_pingSendAt + (MTPPingSendAfter - MTPPingSendAfterAuto - 1) * 1000ULL < getms(true)) {
			LOG(("Could not send ping for MTPPingSendAfter seconds, restarting.."));
			return restart();
		} else {
			_pingSender.start(_pingSendAt + (MTPPingSendAfter - MTPPingSendAfterAuto) * 1000ULL - getms(true));
		}
	} else {
		emit needToSendAsync();
	}
}

void MTProtoConnectionPrivate::onPingSendForce() {
	if (!_pingId) {
		_pingSendAt = 0;
		DEBUG_LOG(("Will send ping!"));
		tryToSend();
	}
}

void MTProtoConnectionPrivate::onWaitReceivedFailed() {
	if (cConnectionType() != dbictAuto && cConnectionType() != dbictTcpProxy) {
		return;
	}

	DEBUG_LOG(("MTP Info: bad connection, _waitForReceived: %1ms").arg(_waitForReceived));
	if (_waitForReceived < MTPMaxReceiveDelay) {
		_waitForReceived *= 2;
	}
	doDisconnect();
	restarted = true;
	if (retryTimer.isActive()) return;

	DEBUG_LOG(("MTP Info: immediate restart!"));
	QTimer::singleShot(0, this, SLOT(socketStart()));
}

void MTProtoConnectionPrivate::onWaitConnectedFailed() {
	DEBUG_LOG(("MTP Info: can't connect in %1ms").arg(_waitForConnected));
	if (_waitForConnected < MTPMaxConnectDelay) _waitForConnected *= 2;

	doDisconnect();
	restarted = true;

	DEBUG_LOG(("MTP Info: immediate restart!"));
	QTimer::singleShot(0, this, SLOT(socketStart()));
}

void MTProtoConnectionPrivate::onWaitIPv4Failed() {
	_conn = _conn6;
	destroyConn(&_conn4);

	if (_conn) {
		DEBUG_LOG(("MTP Info: can't connect through IPv4, using IPv6 connection."));

		updateAuthKey();
	} else {
		restart();
	}
}

void MTProtoConnectionPrivate::doDisconnect() {
	destroyConn();

	{
		QReadLocker lockFinished(&sessionDataMutex);
		if (sessionData) {
			unlockKey();
		}
	}

	clearAuthKeyData();

	setState(MTProtoConnection::Disconnected);
	restarted = false;
}

void MTProtoConnectionPrivate::doFinish() {
	doDisconnect();
	_owner->stopped();
}

void MTProtoConnectionPrivate::handleReceived() {
	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData) return;

	onReceivedSome();

	ReadLockerAttempt lock(sessionData->keyMutex());
	if (!lock) {
		DEBUG_LOG(("MTP Error: auth_key for dc %1 busy, cant lock").arg(dc));
		clearMessages();
		keyId = 0;

		lockFinished.unlock();
		return restart();
	}

	mtpAuthKeyPtr key(sessionData->getKey());
	if (!key || key->keyId() != keyId) {
		DEBUG_LOG(("MTP Error: auth_key id for dc %1 changed").arg(dc));

		lockFinished.unlock();
		return restart();
	}

	while (_conn->received().size()) {
		const mtpBuffer &encryptedBuf(_conn->received().front());
		uint32 len = encryptedBuf.size();
		const mtpPrime *encrypted(encryptedBuf.data());
		if (len < 18) { // 2 auth_key_id, 4 msg_key, 2 salt, 2 session, 2 msg_id, 1 seq_no, 1 length, (1 data + 3 padding) min
			LOG(("TCP Error: bad message received, len %1").arg(len * sizeof(mtpPrime)));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(encrypted, len * sizeof(mtpPrime)).str()));

			lockFinished.unlock();
			return restart();
		}
		if (keyId != *(uint64*)encrypted) {
			LOG(("TCP Error: bad auth_key_id %1 instead of %2 received").arg(keyId).arg(*(uint64*)encrypted));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(encrypted, len * sizeof(mtpPrime)).str()));

			lockFinished.unlock();
			return restart();
		}

		QByteArray dataBuffer((len - 6) * sizeof(mtpPrime), Qt::Uninitialized);
		mtpPrime *data((mtpPrime*)dataBuffer.data()), *msg = data + 8;
		const mtpPrime *from(msg), *end;
		MTPint128 msgKey(*(MTPint128*)(encrypted + 2));

		aesDecrypt(encrypted + 6, data, dataBuffer.size(), key, msgKey);

		uint64 serverSalt = *(uint64*)&data[0], session = *(uint64*)&data[2], msgId = *(uint64*)&data[4];
		uint32 seqNo = *(uint32*)&data[6], msgLen = *(uint32*)&data[7];
		bool needAck = (seqNo & 0x01);

		if (uint32(dataBuffer.size()) < msgLen + 8 * sizeof(mtpPrime) || (msgLen & 0x03)) {
			LOG(("TCP Error: bad msg_len received %1, data size: %2").arg(msgLen).arg(dataBuffer.size()));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(encrypted, len * sizeof(mtpPrime)).str()));
			_conn->received().pop_front();

			lockFinished.unlock();
			return restart();
		}
		uchar sha1Buffer[20];
		if (memcmp(&msgKey, hashSha1(data, msgLen + 8 * sizeof(mtpPrime), sha1Buffer) + 1, sizeof(msgKey))) {
			LOG(("TCP Error: bad SHA1 hash after aesDecrypt in message"));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(encrypted, len * sizeof(mtpPrime)).str()));
			_conn->received().pop_front();

			lockFinished.unlock();
			return restart();
		}
		TCP_LOG(("TCP Info: decrypted message %1,%2,%3 is %4 len").arg(msgId).arg(seqNo).arg(Logs::b(needAck)).arg(msgLen + 8 * sizeof(mtpPrime)));

		uint64 serverSession = sessionData->getSession();
		if (session != serverSession) {
			LOG(("MTP Error: bad server session received"));
			TCP_LOG(("MTP Error: bad server session %1 instead of %2 in message received").arg(session).arg(serverSession));
			_conn->received().pop_front();

			lockFinished.unlock();
			return restart();
		}

		_conn->received().pop_front();

		int32 serverTime((int32)(msgId >> 32)), clientTime(unixtime());
		bool isReply = ((msgId & 0x03) == 1);
		if (!isReply && ((msgId & 0x03) != 3)) {
			LOG(("MTP Error: bad msg_id %1 in message received").arg(msgId));

			lockFinished.unlock();
			return restart();
		}

		bool badTime = false;
		uint64 mySalt = sessionData->getSalt();
		if (serverTime > clientTime + 60 || serverTime + 300 < clientTime) {
			DEBUG_LOG(("MTP Info: bad server time from msg_id: %1, my time: %2").arg(serverTime).arg(clientTime));
			badTime = true;
		}

		bool wasConnected = (getState() == MTProtoConnection::Connected);
		if (serverSalt != mySalt) {
			if (!badTime) {
				DEBUG_LOG(("MTP Info: other salt received.. received: %1, my salt: %2, updating..").arg(serverSalt).arg(mySalt));
				sessionData->setSalt(serverSalt);
				if (setState(MTProtoConnection::Connected, MTProtoConnection::Connecting)) { // only connected
					if (restarted) {
						emit resendAllAsync();
						restarted = false;
					}
				}
			} else {
				DEBUG_LOG(("MTP Info: other salt received.. received: %1, my salt: %2").arg(serverSalt).arg(mySalt));
			}
		} else {
			serverSalt = 0; // dont pass to handle method, so not to lock in setSalt()
		}

		if (needAck) ackRequestData.push_back(MTP_long(msgId));

		int32 res = 1; // if no need to handle, then succeed
		end = data + 8 + (msgLen >> 2);
		const mtpPrime *sfrom(data + 4);
		MTP_LOG(dc, ("Recv: ") + mtpTextSerialize(sfrom, end));

		bool needToHandle = false;
		{
			QWriteLocker lock(sessionData->receivedIdsMutex());
			mtpMsgIdsMap &receivedIds(sessionData->receivedIdsSet());
			needToHandle = receivedIds.insert(msgId, needAck);
		}
		if (needToHandle) {
			res = handleOneReceived(from, end, msgId, serverTime, serverSalt, badTime);
		}
		{
			QWriteLocker lock(sessionData->receivedIdsMutex());
			mtpMsgIdsMap &receivedIds(sessionData->receivedIdsSet());
			uint32 receivedIdsSize = receivedIds.size();
			while (receivedIdsSize-- > MTPIdsBufferSize) {
				receivedIds.erase(receivedIds.begin());
			}
		}

		// send acks
		uint32 toAckSize = ackRequestData.size();
		if (toAckSize) {
			DEBUG_LOG(("MTP Info: will send %1 acks, ids: %2").arg(toAckSize).arg(Logs::vector(ackRequestData)));
			emit sendAnythingAsync(MTPAckSendWaiting);
		}

		bool emitSignal = false;
		{
			QReadLocker locker(sessionData->haveReceivedMutex());
			emitSignal = !sessionData->haveReceivedMap().isEmpty();
			if (emitSignal) {
				DEBUG_LOG(("MTP Info: emitting needToReceive() - need to parse in another thread, haveReceivedMap.size() = %1").arg(sessionData->haveReceivedMap().size()));
			}
		}

		if (emitSignal) {
			emit needToReceive();
		}

		if (res < 0) {
			_needSessionReset = (res < -1);

			lockFinished.unlock();
			return restart();
		}
		retryTimeout = 1; // reset restart() timer

		if (!sessionData->isCheckedKey()) {
			DEBUG_LOG(("MTP Info: marked auth key as checked"));
			sessionData->setCheckedKey(true);
		}

		if (!wasConnected) {
			if (getState() == MTProtoConnection::Connected) {
				emit needToSendAsync();
			}
		}
	}
	if (_conn->needHttpWait()) {
		emit sendHttpWaitAsync();
	}
}

int32 MTProtoConnectionPrivate::handleOneReceived(const mtpPrime *from, const mtpPrime *end, uint64 msgId, int32 serverTime, uint64 serverSalt, bool badTime) {
	mtpTypeId cons = *from;
	try {

	switch (cons) {

	case mtpc_gzip_packed: {
		DEBUG_LOG(("Message Info: gzip container"));
		mtpBuffer response = ungzip(++from, end);
		if (!response.size()) {
			return -1;
		}
		return handleOneReceived(response.data(), response.data() + response.size(), msgId, serverTime, serverSalt, badTime);
	}

	case mtpc_msg_container: {
		if (++from >= end) throw mtpErrorInsufficient();

		const mtpPrime *otherEnd;
		uint32 msgsCount = (uint32)*(from++);
		DEBUG_LOG(("Message Info: container received, count: %1").arg(msgsCount));
		for (uint32 i = 0; i < msgsCount; ++i) {
			if (from + 4 >= end) throw mtpErrorInsufficient();
			otherEnd = from + 4;

			MTPlong inMsgId(from, otherEnd);
			bool isReply = ((inMsgId.v & 0x03) == 1);
			if (!isReply && ((inMsgId.v & 0x03) != 3)) {
				LOG(("Message Error: bad msg_id %1 in contained message received").arg(inMsgId.v));
				return -1;
			}

			MTPint inSeqNo(from, otherEnd);
			MTPint bytes(from, otherEnd);
			if ((bytes.v & 0x03) || bytes.v < 4) {
				LOG(("Message Error: bad length %1 of contained message received").arg(bytes.v));
				return -1;
			}

			bool needAck = (inSeqNo.v & 0x01);
			if (needAck) ackRequestData.push_back(inMsgId);

			DEBUG_LOG(("Message Info: message from container, msg_id: %1, needAck: %2").arg(inMsgId.v).arg(Logs::b(needAck)));

			otherEnd = from + (bytes.v >> 2);
			if (otherEnd > end) throw mtpErrorInsufficient();

			bool needToHandle = false;
			{
				QWriteLocker lock(sessionData->receivedIdsMutex());
				mtpMsgIdsMap &receivedIds(sessionData->receivedIdsSet());
				needToHandle = receivedIds.insert(inMsgId.v, needAck);
			}
			int32 res = 1; // if no need to handle, then succeed
			if (needToHandle) {
				res = handleOneReceived(from, otherEnd, inMsgId.v, serverTime, serverSalt, badTime);
				badTime = false;
			}
			if (res <= 0) {
				return res;
			}

			from = otherEnd;
		}
	} return 1;

	case mtpc_msgs_ack: {
		MTPMsgsAck msg(from, end);
		const QVector<MTPlong> &ids(msg.c_msgs_ack().vmsg_ids.c_vector().v);
		uint32 idsCount = ids.size();

		DEBUG_LOG(("Message Info: acks received, ids: %1").arg(Logs::vector(ids)));
		if (!idsCount) return (badTime ? 0 : 1);

		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				return 0;
			}
		}
		requestsAcked(ids);
	} return 1;

	case mtpc_bad_msg_notification: {
		MTPBadMsgNotification msg(from, end);
		const MTPDbad_msg_notification &data(msg.c_bad_msg_notification());
		LOG(("Message Info: bad message notification received (error_code %3) for msg_id = %1, seq_no = %2").arg(data.vbad_msg_id.v).arg(data.vbad_msg_seqno.v).arg(data.verror_code.v));

		mtpMsgId resendId = data.vbad_msg_id.v;
		if (resendId == _pingMsgId) {
			_pingId = 0;
		}
		int32 errorCode = data.verror_code.v;
		if (errorCode == 16 || errorCode == 17 || errorCode == 32 || errorCode == 33 || errorCode == 64) { // can handle
			bool needResend = (errorCode == 16 || errorCode == 17); // bad msg_id
			if (errorCode == 64) { // bad container!
				needResend = true;
				if (cDebug()) {
					mtpRequest request;
					{
						QWriteLocker locker(sessionData->haveSentMutex());
						mtpRequestMap &haveSent(sessionData->haveSentMap());

						mtpRequestMap::const_iterator i = haveSent.constFind(resendId);
						if (i == haveSent.cend()) {
							LOG(("Message Error: Container not found!"));
						} else {
							request = i.value();
						}
					}
					if (request) {
						if (mtpRequestData::isSentContainer(request)) {
							QStringList lst;
							const mtpMsgId *ids = (const mtpMsgId *)(request->constData() + 8);
							for (uint32 i = 0, l = (request->size() - 8) >> 1; i < l; ++i) {
								lst.push_back(QString::number(ids[i]));
							}
							LOG(("Message Info: bad container received! messages: %1").arg(lst.join(',')));
						} else {
							LOG(("Message Error: bad container received, but request is not a container!"));
						}
					}
				}
			}

			if (!wasSent(resendId)) {
				DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(resendId));
				return (badTime ? 0 : 1);
			}

			if (needResend) { // bad msg_id
				if (serverSalt) sessionData->setSalt(serverSalt);
				unixtimeSet(serverTime, true);

				DEBUG_LOG(("Message Info: unixtime updated, now %1, resending in container..").arg(serverTime));

				resend(resendId, 0, true);
			} else { // must create new session, because msg_id and msg_seqno are inconsistent
				if (badTime) {
					if (serverSalt) sessionData->setSalt(serverSalt);
					unixtimeSet(serverTime, true);
					badTime = false;
				}
				LOG(("Message Info: bad message notification received, msgId %1, error_code %2").arg(data.vbad_msg_id.v).arg(errorCode));
				return -2;
			}
		} else { // fatal (except 48, but it must not get here)
			mtpMsgId resendId = data.vbad_msg_id.v;
			mtpRequestId requestId = wasSent(resendId);
			if (requestId) {
				LOG(("Message Error: bad message notification received, msgId %1, error_code %2, fatal: clearing callbacks").arg(data.vbad_msg_id.v).arg(errorCode));
				_mtp_internal::clearCallbacksDelayed(RPCCallbackClears(1, RPCCallbackClear(requestId, -errorCode)));
			} else {
				DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(resendId));
			}
			return (badTime ? 0 : 1);
		}
	} return 1;

	case mtpc_bad_server_salt: {
		MTPBadMsgNotification msg(from, end);
		const MTPDbad_server_salt &data(msg.c_bad_server_salt());
		DEBUG_LOG(("Message Info: bad server salt received (error_code %4) for msg_id = %1, seq_no = %2, new salt: %3").arg(data.vbad_msg_id.v).arg(data.vbad_msg_seqno.v).arg(data.vnew_server_salt.v).arg(data.verror_code.v));

		mtpMsgId resendId = data.vbad_msg_id.v;
		if (resendId == _pingMsgId) {
			_pingId = 0;
		} else if (!wasSent(resendId)) {
			DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(resendId));
			return (badTime ? 0 : 1);
		}

		uint64 serverSalt = data.vnew_server_salt.v;
		sessionData->setSalt(serverSalt);
		unixtimeSet(serverTime);

		if (setState(MTProtoConnection::Connected, MTProtoConnection::Connecting)) { // maybe only connected
			if (restarted) {
				emit resendAllAsync();
				restarted = false;
			}
		}

		badTime = false;

		DEBUG_LOG(("Message Info: unixtime updated, now %1, server_salt updated, now %2, resending..").arg(serverTime).arg(serverSalt));
		resend(resendId);
	} return 1;

	case mtpc_msgs_state_req: {
		if (badTime) {
			DEBUG_LOG(("Message Info: skipping with bad time.."));
			return 0;
		}
		MTPMsgsStateReq msg(from, end);
		const QVector<MTPlong> ids(msg.c_msgs_state_req().vmsg_ids.c_vector().v);
		uint32 idsCount = ids.size();
		DEBUG_LOG(("Message Info: msgs_state_req received, ids: %1").arg(Logs::vector(ids)));
		if (!idsCount) return 1;

		QByteArray info(idsCount, Qt::Uninitialized);
		{
			QReadLocker lock(sessionData->receivedIdsMutex());
			const mtpMsgIdsMap &receivedIds(sessionData->receivedIdsSet());
			mtpMsgIdsMap::const_iterator receivedIdsEnd(receivedIds.cend());
			uint64 minRecv = receivedIds.min(), maxRecv = receivedIds.max();

			QReadLocker locker(sessionData->wereAckedMutex());
			const mtpRequestIdsMap &wereAcked(sessionData->wereAckedMap());
			mtpRequestIdsMap::const_iterator wereAckedEnd(wereAcked.cend());

			for (uint32 i = 0, l = idsCount; i < l; ++i) {
				char state = 0;
				uint64 reqMsgId = ids[i].v;
				if (reqMsgId < minRecv) {
					state |= 0x01;
				} else if (reqMsgId > maxRecv) {
					state |= 0x03;
				} else {
					mtpMsgIdsMap::const_iterator recv = receivedIds.constFind(reqMsgId);
					if (recv == receivedIdsEnd) {
						state |= 0x02;
					} else {
						state |= 0x04;
						if (wereAcked.constFind(reqMsgId) != wereAckedEnd) {
							state |= 0x80; // we know, that server knows, that we received request
						}
						if (recv.value()) { // need ack, so we sent ack
							state |= 0x08;
						} else {
							state |= 0x10;
						}
					}
				}
				info[i] = state;
			}
		}
		emit sendMsgsStateInfoAsync(msgId, info);
	} return 1;

	case mtpc_msgs_state_info: {
		MTPMsgsStateInfo msg(from, end);
		const MTPDmsgs_state_info &data(msg.c_msgs_state_info());

		uint64 reqMsgId = data.vreq_msg_id.v;
		const string &states(data.vinfo.c_string().v);

		DEBUG_LOG(("Message Info: msg state received, msgId %1, reqMsgId: %2, HEX states %3").arg(msgId).arg(reqMsgId).arg(Logs::mb(states.data(), states.length()).str()));
		mtpRequest requestBuffer;
		{ // find this request in session-shared sent requests map
			QReadLocker locker(sessionData->haveSentMutex());
			const mtpRequestMap &haveSent(sessionData->haveSentMap());
			mtpRequestMap::const_iterator replyTo = haveSent.constFind(reqMsgId);
			if (replyTo == haveSent.cend()) { // do not look in toResend, because we do not resend msgs_state_req requests
				DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(reqMsgId));
				return (badTime ? 0 : 1);
			}
			if (badTime) {
				if (serverSalt) sessionData->setSalt(serverSalt); // requestsFixTimeSalt with no lookup
				unixtimeSet(serverTime, true);

				DEBUG_LOG(("Message Info: unixtime updated from mtpc_msgs_state_info, now %1").arg(serverTime));

				badTime = false;
			}
			requestBuffer = replyTo.value();
		}
		QVector<MTPlong> toAckReq(1, MTP_long(reqMsgId)), toAck;
		requestsAcked(toAck, true);

		if (requestBuffer->size() < 9) {
			LOG(("Message Error: bad request %1 found in requestMap, size: %2").arg(reqMsgId).arg(requestBuffer->size()));
			return -1;
		}
		try {
			const mtpPrime *rFrom = requestBuffer->constData() + 8, *rEnd = requestBuffer->constData() + requestBuffer->size();
			if (mtpTypeId(*rFrom) == mtpc_msgs_state_req) {
				MTPMsgsStateReq request(rFrom, rEnd);
				handleMsgsStates(request.c_msgs_state_req().vmsg_ids.c_vector().v, states, toAck);
			} else {
				MTPMsgResendReq request(rFrom, rEnd);
				handleMsgsStates(request.c_msg_resend_req().vmsg_ids.c_vector().v, states, toAck);
			}
		} catch(Exception &e) {
			LOG(("Message Error: could not parse sent msgs_state_req"));
			throw;
		}

		requestsAcked(toAck);
	} return 1;

	case mtpc_msgs_all_info: {
		if (badTime) {
			DEBUG_LOG(("Message Info: skipping with bad time.."));
			return 0;
		}

		MTPMsgsAllInfo msg(from, end);
		const MTPDmsgs_all_info &data(msg.c_msgs_all_info());
		const QVector<MTPlong> ids(data.vmsg_ids.c_vector().v);
		const string &states(data.vinfo.c_string().v);

		QVector<MTPlong> toAck;

		DEBUG_LOG(("Message Info: msgs all info received, msgId %1, reqMsgIds: %2, states %3").arg(msgId).arg(Logs::vector(ids)).arg(Logs::mb(states.data(), states.length()).str()));
		handleMsgsStates(ids, states, toAck);

		requestsAcked(toAck);
	} return 1;

	case mtpc_msg_detailed_info: {
		MTPMsgDetailedInfo msg(from, end);
		const MTPDmsg_detailed_info &data(msg.c_msg_detailed_info());

		DEBUG_LOG(("Message Info: msg detailed info, sent msgId %1, answerId %2, status %3, bytes %4").arg(data.vmsg_id.v).arg(data.vanswer_msg_id.v).arg(data.vstatus.v).arg(data.vbytes.v));

		QVector<MTPlong> ids(1, data.vmsg_id);
		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				DEBUG_LOG(("Message Info: error, such message was not sent recently %1").arg(data.vmsg_id.v));
				return 0;
			}
		}
		requestsAcked(ids);

		bool received = false;
		MTPlong resMsgId = data.vanswer_msg_id;
		{
			QReadLocker lock(sessionData->receivedIdsMutex());
			const mtpMsgIdsMap &receivedIds(sessionData->receivedIdsSet());
			received = (receivedIds.find(resMsgId.v) != receivedIds.cend()) && (receivedIds.min() < resMsgId.v);
		}
		if (received) {
			ackRequestData.push_back(resMsgId);
		} else {
			DEBUG_LOG(("Message Info: answer message %1 was not received, requesting..").arg(resMsgId.v));
			resendRequestData.push_back(resMsgId);
		}
	} return 1;

	case mtpc_msg_new_detailed_info: {
		if (badTime) {
			DEBUG_LOG(("Message Info: skipping msg_new_detailed_info with bad time.."));
			return 0;
		}
		MTPMsgDetailedInfo msg(from, end);
		const MTPDmsg_new_detailed_info &data(msg.c_msg_new_detailed_info());

		DEBUG_LOG(("Message Info: msg new detailed info, answerId %2, status %3, bytes %4").arg(data.vanswer_msg_id.v).arg(data.vstatus.v).arg(data.vbytes.v));

		bool received = false;
		MTPlong resMsgId = data.vanswer_msg_id;
		{
			QReadLocker lock(sessionData->receivedIdsMutex());
			const mtpMsgIdsMap &receivedIds(sessionData->receivedIdsSet());
			received = (receivedIds.find(resMsgId.v) != receivedIds.cend()) && (receivedIds.min() < resMsgId.v);
		}
		if (received) {
			ackRequestData.push_back(resMsgId);
		} else {
			DEBUG_LOG(("Message Info: answer message %1 was not received, requesting..").arg(resMsgId.v));
			resendRequestData.push_back(resMsgId);
		}
	} return 1;

	case mtpc_msg_resend_req: {
		MTPMsgResendReq msg(from, end);
		const QVector<MTPlong> &ids(msg.c_msg_resend_req().vmsg_ids.c_vector().v);

		uint32 idsCount = ids.size();
		DEBUG_LOG(("Message Info: resend of msgs requested, ids: %1").arg(Logs::vector(ids)));
		if (!idsCount) return (badTime ? 0 : 1);

		QVector<quint64> toResend(ids.size(), Qt::Uninitialized);
		for (int32 i = 0, l = ids.size(); i < l; ++i) {
			toResend[i] = ids.at(i).v;
		}
		resendMany(toResend, 0, false, true);
	} return 1;

	case mtpc_rpc_result: {
		if (from + 3 > end) throw mtpErrorInsufficient();
		mtpResponse response;

		MTPlong reqMsgId(++from, end);
		mtpTypeId typeId = from[0];

		DEBUG_LOG(("RPC Info: response received for %1, queueing..").arg(reqMsgId.v));

		QVector<MTPlong> ids(1, reqMsgId);
		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				DEBUG_LOG(("Message Info: error, such message was not sent recently %1").arg(reqMsgId.v));
				return 0;
			}
		}
		requestsAcked(ids, true);

		if (typeId == mtpc_gzip_packed) {
			DEBUG_LOG(("RPC Info: gzip container"));
			response = ungzip(++from, end);
			if (!response.size()) {
				return -1;
			}
			typeId = response[0];
		} else {
			response.resize(end - from);
			memcpy(response.data(), from, (end - from) * sizeof(mtpPrime));
		}
		if (!sessionData->layerWasInited()) {
			sessionData->setLayerWasInited(true);
			sessionData->owner()->notifyLayerInited(true);
		}

		mtpRequestId requestId = wasSent(reqMsgId.v);
		if (requestId && requestId != mtpRequestId(0xFFFFFFFF)) {
			QWriteLocker locker(sessionData->haveReceivedMutex());
			sessionData->haveReceivedMap().insert(requestId, response); // save rpc_result for processing in main mtp thread
		} else {
			DEBUG_LOG(("RPC Info: requestId not found for msgId %1").arg(reqMsgId.v));
		}
	} return 1;

	case mtpc_new_session_created: {
		const mtpPrime *start = from;
		MTPNewSession msg(from, end);
		const MTPDnew_session_created &data(msg.c_new_session_created());

		if (badTime) {
			if (requestsFixTimeSalt(QVector<MTPlong>(1, data.vfirst_msg_id), serverTime, serverSalt)) {
				badTime = false;
			} else {
				DEBUG_LOG(("Message Info: error, such message was not sent recently %1").arg(data.vfirst_msg_id.v));
				return 0;
			}
		}

		DEBUG_LOG(("Message Info: new server session created, unique_id %1, first_msg_id %2, server_salt %3").arg(data.vunique_id.v).arg(data.vfirst_msg_id.v).arg(data.vserver_salt.v));
		sessionData->setSalt(data.vserver_salt.v);

		mtpMsgId firstMsgId = data.vfirst_msg_id.v;
		QVector<quint64> toResend;
		{
			QReadLocker locker(sessionData->haveSentMutex());
			const mtpRequestMap &haveSent(sessionData->haveSentMap());
			toResend.reserve(haveSent.size());
			for (mtpRequestMap::const_iterator i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) {
				if (i.key() >= firstMsgId) break;
				if (i.value()->requestId) toResend.push_back(i.key());
			}
		}
		resendMany(toResend, 10, true);

		mtpBuffer update(from - start);
		if (from > start) memcpy(update.data(), start, (from - start) * sizeof(mtpPrime));

		QWriteLocker locker(sessionData->haveReceivedMutex());
		mtpResponseMap &haveReceived(sessionData->haveReceivedMap());
		mtpRequestId fakeRequestId = sessionData->nextFakeRequestId();
		haveReceived.insert(fakeRequestId, mtpResponse(update)); // notify main process about new session - need to get difference
	} return 1;

	case mtpc_ping: {
		if (badTime) return 0;

		MTPPing msg(from, end);
		DEBUG_LOG(("Message Info: ping received, ping_id: %1, sending pong..").arg(msg.vping_id.v));

		emit sendPongAsync(msgId, msg.vping_id.v);
	} return 1;

	case mtpc_pong: {
		MTPPong msg(from, end);
		const MTPDpong &data(msg.c_pong());
		DEBUG_LOG(("Message Info: pong received, msg_id: %1, ping_id: %2").arg(data.vmsg_id.v).arg(data.vping_id.v));

		if (!wasSent(data.vmsg_id.v)) {
			DEBUG_LOG(("Message Error: such msg_id %1 ping_id %2 was not sent recently").arg(data.vmsg_id.v).arg(data.vping_id.v));
			return 0;
		}
		if (data.vping_id.v == _pingId) {
			_pingId = 0;
		} else {
			DEBUG_LOG(("Message Info: just pong.."));
		}

		QVector<MTPlong> ids(1, data.vmsg_id);
		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				return 0;
			}
		}
		requestsAcked(ids, true);
	} return 1;

	}

	} catch (Exception &e) {
		return -1;
	}

	if (badTime) {
		DEBUG_LOG(("Message Error: bad time in updates cons, must create new session"));
		return -2;
	}

	mtpBuffer update(end - from);
	if (end > from) memcpy(update.data(), from, (end - from) * sizeof(mtpPrime));

	QWriteLocker locker(sessionData->haveReceivedMutex());
	mtpResponseMap &haveReceived(sessionData->haveReceivedMap());
	mtpRequestId fakeRequestId = sessionData->nextFakeRequestId();
	haveReceived.insert(fakeRequestId, mtpResponse(update)); // notify main process about new updates

	if (cons != mtpc_updatesTooLong && cons != mtpc_updateShortMessage && cons != mtpc_updateShortChatMessage && cons != mtpc_updateShortSentMessage && cons != mtpc_updateShort && cons != mtpc_updatesCombined && cons != mtpc_updates) {
		LOG(("Message Error: unknown constructor %1").arg(cons)); // maybe new api?..
	}

	return 1;
}

mtpBuffer MTProtoConnectionPrivate::ungzip(const mtpPrime *from, const mtpPrime *end) const {
	MTPstring packed(from, end); // read packed string as serialized mtp string type
	uint32 packedLen = packed.c_string().v.size(), unpackedChunk = packedLen, unpackedLen = 0;

	mtpBuffer result; // * 4 because of mtpPrime type
	result.resize(0);
	z_stream stream;
	stream.zalloc = 0;
	stream.zfree = 0;
	stream.opaque = 0;
	stream.avail_in = 0;
	stream.next_in = 0;
	int res = inflateInit2(&stream, 16 + MAX_WBITS);
	if (res != Z_OK) {
		LOG(("RPC Error: could not init zlib stream, code: %1").arg(res));
		return result;
	}
	stream.avail_in = packedLen;
	stream.next_in = (Bytef*)&packed._string().v[0];

	stream.avail_out = 0;
	while (!stream.avail_out) {
		result.resize(result.size() + unpackedChunk);
		stream.avail_out = unpackedChunk * sizeof(mtpPrime);
		stream.next_out = (Bytef*)&result[result.size() - unpackedChunk];
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			inflateEnd(&stream);
			LOG(("RPC Error: could not unpack gziped data, code: %1").arg(res));
			DEBUG_LOG(("RPC Error: bad gzip: %1").arg(Logs::mb(&packed.c_string().v[0], packedLen).str()));
			return mtpBuffer();
		}
	}
	if (stream.avail_out & 0x03) {
		uint32 badSize = result.size() * sizeof(mtpPrime) - stream.avail_out;
		LOG(("RPC Error: bad length of unpacked data %1").arg(badSize));
		DEBUG_LOG(("RPC Error: bad unpacked data %1").arg(Logs::mb(result.data(), badSize).str()));
		return mtpBuffer();
	}
	result.resize(result.size() - (stream.avail_out >> 2));
	inflateEnd(&stream);
	if (!result.size()) {
		LOG(("RPC Error: bad length of unpacked data 0"));
	}
	return result;
}

bool MTProtoConnectionPrivate::requestsFixTimeSalt(const QVector<MTPlong> &ids, int32 serverTime, uint64 serverSalt) {
	uint32 idsCount = ids.size();

	for (uint32 i = 0; i < idsCount; ++i) {
		if (wasSent(ids[i].v)) {// found such msg_id in recent acked requests or in recent sent requests
			if (serverSalt) sessionData->setSalt(serverSalt);
			unixtimeSet(serverTime, true);
			return true;
		}
	}
	return false;
}

void MTProtoConnectionPrivate::requestsAcked(const QVector<MTPlong> &ids, bool byResponse) {
	uint32 idsCount = ids.size();

	DEBUG_LOG(("Message Info: requests acked, ids %1").arg(Logs::vector(ids)));

	RPCCallbackClears clearedAcked;
	QVector<MTPlong> toAckMore;
	{
		QWriteLocker locker1(sessionData->wereAckedMutex());
		mtpRequestIdsMap &wereAcked(sessionData->wereAckedMap());

		{
			QWriteLocker locker2(sessionData->haveSentMutex());
			mtpRequestMap &haveSent(sessionData->haveSentMap());

			for (uint32 i = 0; i < idsCount; ++i) {
				mtpMsgId msgId = ids[i].v;
				mtpRequestMap::iterator req = haveSent.find(msgId);
				if (req != haveSent.cend()) {
					if (!req.value()->msDate) {
						DEBUG_LOG(("Message Info: container ack received, msgId %1").arg(ids[i].v));
						uint32 inContCount = ((*req)->size() - 8) / 2;
						const mtpMsgId *inContId = (const mtpMsgId *)(req.value()->constData() + 8);
						toAckMore.reserve(toAckMore.size() + inContCount);
						for (uint32 j = 0; j < inContCount; ++j) {
							toAckMore.push_back(MTP_long(*(inContId++)));
						}
						haveSent.erase(req);
					} else {
						mtpRequestId reqId = req.value()->requestId;
						bool moveToAcked = byResponse;
						if (!moveToAcked) { // ignore ACK, if we need a response (if we have a handler)
							moveToAcked = !_mtp_internal::hasCallbacks(reqId);
						}
						if (moveToAcked) {
							wereAcked.insert(msgId, reqId);
							haveSent.erase(req);
						} else {
							DEBUG_LOG(("Message Info: ignoring ACK for msgId %1 because request %2 requires a response").arg(msgId).arg(reqId));
						}
					}
				} else {
					DEBUG_LOG(("Message Info: msgId %1 was not found in recent sent, while acking requests, searching in resend..").arg(msgId));
					QWriteLocker locker3(sessionData->toResendMutex());
					mtpRequestIdsMap &toResend(sessionData->toResendMap());
					mtpRequestIdsMap::iterator reqIt = toResend.find(msgId);
					if (reqIt != toResend.cend()) {
						mtpRequestId reqId = reqIt.value();
						bool moveToAcked = byResponse;
						if (!moveToAcked) { // ignore ACK, if we need a response (if we have a handler)
							moveToAcked = !_mtp_internal::hasCallbacks(reqId);
						}
						if (moveToAcked) {
							QWriteLocker locker4(sessionData->toSendMutex());
							mtpPreRequestMap &toSend(sessionData->toSendMap());
							mtpPreRequestMap::iterator req = toSend.find(reqId);
							if (req != toSend.cend()) {
								wereAcked.insert(msgId, req.value()->requestId);
								if (req.value()->requestId != reqId) {
									DEBUG_LOG(("Message Error: for msgId %1 found resent request, requestId %2, contains requestId %3").arg(msgId).arg(reqId).arg(req.value()->requestId));
								} else {
									DEBUG_LOG(("Message Info: acked msgId %1 that was prepared to resend, requestId %2").arg(msgId).arg(reqId));
								}
								toSend.erase(req);
							} else {
								DEBUG_LOG(("Message Info: msgId %1 was found in recent resent, requestId %2 was not found in prepared to send").arg(msgId));
							}
							toResend.erase(reqIt);
						} else {
							DEBUG_LOG(("Message Info: ignoring ACK for msgId %1 because request %2 requires a response").arg(msgId).arg(reqId));
						}
					} else {
						DEBUG_LOG(("Message Info: msgId %1 was not found in recent resent either").arg(msgId));
					}
				}
			}
		}

		uint32 ackedCount = wereAcked.size();
		if (ackedCount > MTPIdsBufferSize) {
			DEBUG_LOG(("Message Info: removing some old acked sent msgIds %1").arg(ackedCount - MTPIdsBufferSize));
			clearedAcked.reserve(ackedCount - MTPIdsBufferSize);
			while (ackedCount-- > MTPIdsBufferSize) {
				mtpRequestIdsMap::iterator i(wereAcked.begin());
				clearedAcked.push_back(RPCCallbackClear(i.key(), RPCError::TimeoutError));
				wereAcked.erase(i);
			}
		}
	}

	if (clearedAcked.size()) {
		_mtp_internal::clearCallbacksDelayed(clearedAcked);
	}

	if (toAckMore.size()) {
		requestsAcked(toAckMore);
	}
}

void MTProtoConnectionPrivate::handleMsgsStates(const QVector<MTPlong> &ids, const string &states, QVector<MTPlong> &acked) {
	uint32 idsCount = ids.size();
	if (!idsCount) {
		DEBUG_LOG(("Message Info: void ids vector in handleMsgsStates()"));
		return;
	}

	acked.reserve(acked.size() + idsCount);

	for (uint32 i = 0, count = idsCount; i < count; ++i) {
		char state = states[i];
		uint64 requestMsgId = ids[i].v;
		{
			QReadLocker locker(sessionData->haveSentMutex());
			const mtpRequestMap &haveSent(sessionData->haveSentMap());
			mtpRequestMap::const_iterator haveSentEnd = haveSent.cend();
			if (haveSent.find(requestMsgId) == haveSentEnd) {
				DEBUG_LOG(("Message Info: state was received for msgId %1, but request is not found, looking in resent requests..").arg(requestMsgId));
				QWriteLocker locker2(sessionData->toResendMutex());
				mtpRequestIdsMap &toResend(sessionData->toResendMap());
				mtpRequestIdsMap::iterator reqIt = toResend.find(requestMsgId);
				if (reqIt != toResend.cend()) {
					if ((state & 0x07) != 0x04) { // was received
						DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, already resending in container").arg(requestMsgId).arg((int32)state));
					} else {
						DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, ack, cancelling resend").arg(requestMsgId).arg((int32)state));
						acked.push_back(MTP_long(requestMsgId)); // will remove from resend in requestsAcked
					}
				} else {
					DEBUG_LOG(("Message Info: msgId %1 was not found in recent resent either").arg(requestMsgId));
				}
				continue;
			}
		}
		if ((state & 0x07) != 0x04) { // was received
			DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, resending in container").arg(requestMsgId).arg((int32)state));
			resend(requestMsgId, 10, true);
		} else {
			DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, ack").arg(requestMsgId).arg((int32)state));
			acked.push_back(MTP_long(requestMsgId));
		}
	}
}

void MTProtoConnectionPrivate::resend(quint64 msgId, quint64 msCanWait, bool forceContainer, bool sendMsgStateInfo) {
	if (msgId == _pingMsgId) return;
	emit resendAsync(msgId, msCanWait, forceContainer, sendMsgStateInfo);
}

void MTProtoConnectionPrivate::resendMany(QVector<quint64> msgIds, quint64 msCanWait, bool forceContainer, bool sendMsgStateInfo) {
	for (int32 i = 0, l = msgIds.size(); i < l; ++i) {
		if (msgIds.at(i) == _pingMsgId) {
			msgIds.remove(i);
			--l;
		}
	}
	emit resendManyAsync(msgIds, msCanWait, forceContainer, sendMsgStateInfo);
}

void MTProtoConnectionPrivate::onConnected4() {
	_waitForConnected = MTPMinConnectDelay;
	_waitForConnectedTimer.stop();

	_waitForIPv4Timer.stop();

	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData) return;

	disconnect(_conn4, SIGNAL(connected()), this, SLOT(onConnected4()));
	if (!_conn4->isConnected()) {
		LOG(("Connection Error: not connected in onConnected4(), state: %1").arg(_conn4->debugState()));

		lockFinished.unlock();
		return restart();
	}

	_conn = _conn4;
	destroyConn(&_conn6);

	DEBUG_LOG(("MTP Info: connection through IPv4 succeed."));

	lockFinished.unlock();
	updateAuthKey();
}

void MTProtoConnectionPrivate::onConnected6() {
	_waitForConnected = MTPMinConnectDelay;
	_waitForConnectedTimer.stop();

	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData) return;

	disconnect(_conn6, SIGNAL(connected()), this, SLOT(onConnected6()));
	if (!_conn6->isConnected()) {
		LOG(("Connection Error: not connected in onConnected(), state: %1").arg(_conn6->debugState()));

		lockFinished.unlock();
		return restart();
	}

	DEBUG_LOG(("MTP Info: connection through IPv6 succeed, waiting IPv4 for %1ms.").arg(MTPIPv4ConnectionWaitTimeout));

	_waitForIPv4Timer.start(MTPIPv4ConnectionWaitTimeout);
}

void MTProtoConnectionPrivate::onDisconnected4() {
	if (_conn && _conn == _conn6) return; // disconnected the unused

	if (_conn || !_conn6) {
		destroyConn();
		restart();
	} else {
		destroyConn(&_conn4);
	}
}

void MTProtoConnectionPrivate::onDisconnected6() {
	if (_conn && _conn == _conn4) return; // disconnected the unused

	if (_conn || !_conn4) {
		destroyConn();
		restart();
	} else {
		destroyConn(&_conn6);
	}
}

void MTProtoConnectionPrivate::updateAuthKey() 	{
	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData || !_conn) return;

	DEBUG_LOG(("AuthKey Info: MTProtoConnection updating key from MTProtoSession, dc %1").arg(dc));
	uint64 newKeyId = 0;
	{
		ReadLockerAttempt lock(sessionData->keyMutex());
		if (!lock) {
			DEBUG_LOG(("MTP Info: could not lock auth_key for read, waiting signal emit"));
			clearMessages();
			keyId = newKeyId;
			return; // some other connection is getting key
		}
		const mtpAuthKeyPtr &key(sessionData->getKey());
		newKeyId = key ? key->keyId() : 0;
	}
	if (keyId != newKeyId) {
		clearMessages();
		keyId = newKeyId;
	}
	DEBUG_LOG(("AuthKey Info: MTProtoConnection update key from MTProtoSession, dc %1 result: %2").arg(dc).arg(Logs::mb(&keyId, sizeof(keyId)).str()));
	if (keyId) {
		return authKeyCreated();
	}

	DEBUG_LOG(("AuthKey Info: No key in updateAuthKey(), will be creating auth_key"));
	lockKey();

	const mtpAuthKeyPtr &key(sessionData->getKey());
	if (key) {
		if (keyId != key->keyId()) clearMessages();
		keyId = key->keyId();
		unlockKey();
		return authKeyCreated();
	}

	authKeyData = new MTProtoConnectionPrivate::AuthKeyCreateData();
	authKeyStrings = new MTProtoConnectionPrivate::AuthKeyCreateStrings();
	authKeyData->req_num = 0;
	authKeyData->nonce = MTP::nonce<MTPint128>();

	MTPReq_pq req_pq;
	req_pq.vnonce = authKeyData->nonce;

	connect(_conn, SIGNAL(receivedData()), this, SLOT(pqAnswered()));

	DEBUG_LOG(("AuthKey Info: sending Req_pq.."));
	lockFinished.unlock();
	sendRequestNotSecure(req_pq);
}

void MTProtoConnectionPrivate::clearMessages() {
	if (keyId && keyId != mtpAuthKey::RecreateKeyId && _conn) {
		_conn->received().clear();
	}
}

void MTProtoConnectionPrivate::pqAnswered() {
	disconnect(_conn, SIGNAL(receivedData()), this, SLOT(pqAnswered()));
	DEBUG_LOG(("AuthKey Info: receiving Req_pq answer.."));

	MTPReq_pq::ResponseType res_pq;
	if (!readResponseNotSecure(res_pq)) {
		return restart();
	}

	const MTPDresPQ &res_pq_data(res_pq.c_resPQ());
	if (res_pq_data.vnonce != authKeyData->nonce) {
		LOG(("AuthKey Error: received nonce <> sent nonce (in res_pq)!"));
		DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&res_pq_data.vnonce, 16).str()).arg(Logs::mb(&authKeyData->nonce, 16).str()));
		return restart();
	}

	mtpPublicRSA *rsaKey = 0;
	const QVector<MTPlong> &fingerPrints(res_pq.c_resPQ().vserver_public_key_fingerprints.c_vector().v);
	for (uint32 i = 0, l = fingerPrints.size(); i < l; ++i) {
		uint64 print(fingerPrints[i].v);
		PublicRSAKeys::iterator rsaIndex = gPublicRSA.find(print);
		if (rsaIndex != gPublicRSA.end()) {
			rsaKey = &rsaIndex.value();
			break;
		}
	}
	if (!rsaKey) {
		QStringList suggested, my;
		for (uint32 i = 0, l = fingerPrints.size(); i < l; ++i) {
			suggested.push_back(QString("%1").arg(fingerPrints[i].v));
		}
		for (PublicRSAKeys::const_iterator i = gPublicRSA.cbegin(), e = gPublicRSA.cend(); i != e; ++i) {
			my.push_back(QString("%1").arg(i.key()));
		}
		LOG(("AuthKey Error: could not choose public RSA key, suggested fingerprints: %1, my fingerprints: %2").arg(suggested.join(", ")).arg(my.join(", ")));
		return restart();
	}

	authKeyData->server_nonce = res_pq_data.vserver_nonce;

	MTPP_Q_inner_data p_q_inner;
	MTPDp_q_inner_data &p_q_inner_data(p_q_inner._p_q_inner_data());
	p_q_inner_data.vnonce = authKeyData->nonce;
	p_q_inner_data.vserver_nonce = authKeyData->server_nonce;
	p_q_inner_data.vpq = res_pq_data.vpq;

	const string &pq(res_pq_data.vpq.c_string().v);
	string &p(p_q_inner_data.vp._string().v), &q(p_q_inner_data.vq._string().v);

	if (!parsePQ(pq, p, q)) {
		LOG(("AuthKey Error: could not factor pq!"));
		DEBUG_LOG(("AuthKey Error: problematic pq: %1").arg(Logs::mb(&pq[0], pq.length()).str()));
		return restart();
	}

	authKeyData->new_nonce = MTP::nonce<MTPint256>();
	p_q_inner_data.vnew_nonce = authKeyData->new_nonce;

	MTPReq_DH_params req_DH_params;
	req_DH_params.vnonce = authKeyData->nonce;
	req_DH_params.vserver_nonce = authKeyData->server_nonce;
	req_DH_params.vpublic_key_fingerprint = MTP_long(rsaKey->fingerPrint());
	req_DH_params.vp = p_q_inner_data.vp;
	req_DH_params.vq = p_q_inner_data.vq;

	string &dhEncString(req_DH_params.vencrypted_data._string().v);

	uint32 p_q_inner_size = p_q_inner.innerLength(), encSize = (p_q_inner_size >> 2) + 6;
	if (encSize >= 65) {
		mtpBuffer tmp;
		tmp.reserve(encSize);
		p_q_inner.write(tmp);
		LOG(("AuthKey Error: too large data for RSA encrypt, size %1").arg(encSize * sizeof(mtpPrime)));
		DEBUG_LOG(("AuthKey Error: bad data for RSA encrypt %1").arg(Logs::mb(&tmp[0], tmp.size() * 4).str()));
		return restart(); // can't be 255-byte string
	}

	mtpBuffer encBuffer;
	encBuffer.reserve(65); // 260 bytes
	encBuffer.resize(6);
	encBuffer[0] = 0;
	p_q_inner.write(encBuffer);

	hashSha1(&encBuffer[6], p_q_inner_size, &encBuffer[1]);
	if (encSize < 65) {
		encBuffer.resize(65);
		memset_rand(&encBuffer[encSize], (65 - encSize) * sizeof(mtpPrime));
	}

	dhEncString.resize(256);
	int32 res = RSA_public_encrypt(256, ((const uchar*)&encBuffer[0]) + 3, (uchar*)&dhEncString[0], rsaKey->key(), RSA_NO_PADDING);
	if (res != 256) {
		ERR_load_crypto_strings();
		LOG(("RSA Error: RSA_public_encrypt failed, key fp: %1, result: %2, error: %3").arg(rsaKey->fingerPrint()).arg(res).arg(ERR_error_string(ERR_get_error(), 0)));
		return restart();
	}

	connect(_conn, SIGNAL(receivedData()), this, SLOT(dhParamsAnswered()));

	DEBUG_LOG(("AuthKey Info: sending Req_DH_params.."));
	sendRequestNotSecure(req_DH_params);
}

void MTProtoConnectionPrivate::dhParamsAnswered() {
	disconnect(_conn, SIGNAL(receivedData()), this, SLOT(dhParamsAnswered()));
	DEBUG_LOG(("AuthKey Info: receiving Req_DH_params answer.."));

	MTPReq_DH_params::ResponseType res_DH_params;
	if (!readResponseNotSecure(res_DH_params)) {
		return restart();
	}

	switch (res_DH_params.type()) {
	case mtpc_server_DH_params_ok: {
		const MTPDserver_DH_params_ok &encDH(res_DH_params.c_server_DH_params_ok());
		if (encDH.vnonce != authKeyData->nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in server_DH_params_ok)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&encDH.vnonce, 16).str()).arg(Logs::mb(&authKeyData->nonce, 16).str()));
			return restart();
		}
		if (encDH.vserver_nonce != authKeyData->server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_params_ok)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&encDH.vserver_nonce, 16).str()).arg(Logs::mb(&authKeyData->server_nonce, 16).str()));
			return restart();
		}

		const string &encDHStr(encDH.vencrypted_answer.c_string().v);
		uint32 encDHLen = encDHStr.length(), encDHBufLen = encDHLen >> 2;
		if ((encDHLen & 0x03) || encDHBufLen < 6) {
			LOG(("AuthKey Error: bad encrypted data length %1 (in server_DH_params_ok)!").arg(encDHLen));
			DEBUG_LOG(("AuthKey Error: received encrypted data %1").arg(Logs::mb(&encDHStr[0], encDHLen).str()));
			return restart();
		}

		uint32 nlen = authKeyData->new_nonce.innerLength(), slen = authKeyData->server_nonce.innerLength();
		uchar tmp_aes[1024], sha1ns[20], sha1sn[20], sha1nn[20];
		memcpy(tmp_aes, &authKeyData->new_nonce, nlen);
		memcpy(tmp_aes + nlen, &authKeyData->server_nonce, slen);
		memcpy(tmp_aes + nlen + slen, &authKeyData->new_nonce, nlen);
		memcpy(tmp_aes + nlen + slen + nlen, &authKeyData->new_nonce, nlen);
		hashSha1(tmp_aes, nlen + slen, sha1ns);
		hashSha1(tmp_aes + nlen, nlen + slen, sha1sn);
		hashSha1(tmp_aes + nlen + slen, nlen + nlen, sha1nn);

		mtpBuffer decBuffer;
		decBuffer.resize(encDHBufLen);

		memcpy(authKeyData->aesKey, sha1ns, 20);
		memcpy(authKeyData->aesKey + 20, sha1sn, 12);
		memcpy(authKeyData->aesIV, sha1sn + 12, 8);
		memcpy(authKeyData->aesIV + 8, sha1nn, 20);
		memcpy(authKeyData->aesIV + 28, &authKeyData->new_nonce, 4);

		aesDecrypt(&encDHStr[0], &decBuffer[0], encDHLen, authKeyData->aesKey, authKeyData->aesIV);

		const mtpPrime *from(&decBuffer[5]), *to(from), *end(from + (encDHBufLen - 5));
		MTPServer_DH_inner_data dh_inner(to, end);
		const MTPDserver_DH_inner_data &dh_inner_data(dh_inner.c_server_DH_inner_data());
		if (dh_inner_data.vnonce != authKeyData->nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in server_DH_inner_data)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&dh_inner_data.vnonce, 16).str()).arg(Logs::mb(&authKeyData->nonce, 16).str()));
			return restart();
		}
		if (dh_inner_data.vserver_nonce != authKeyData->server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_inner_data)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&dh_inner_data.vserver_nonce, 16).str()).arg(Logs::mb(&authKeyData->server_nonce, 16).str()));
			return restart();
		}
		uchar sha1Buffer[20];
		if (memcmp(&decBuffer[0], hashSha1(&decBuffer[5], (to - from) * sizeof(mtpPrime), sha1Buffer), 20)) {
			LOG(("AuthKey Error: sha1 hash of encrypted part did not match!"));
			DEBUG_LOG(("AuthKey Error: sha1 did not match, server_nonce: %1, new_nonce %2, encrypted data %3").arg(Logs::mb(&authKeyData->server_nonce, 16).str()).arg(Logs::mb(&authKeyData->new_nonce, 16).str()).arg(Logs::mb(&encDHStr[0], encDHLen).str()));
			return restart();
		}
		unixtimeSet(dh_inner_data.vserver_time.v);

		const string &dhPrime(dh_inner_data.vdh_prime.c_string().v), &g_a(dh_inner_data.vg_a.c_string().v);
		if (dhPrime.length() != 256 || g_a.length() != 256) {
			LOG(("AuthKey Error: bad dh_prime len (%1) or g_a len (%2)").arg(dhPrime.length()).arg(g_a.length()));
			DEBUG_LOG(("AuthKey Error: dh_prime %1, g_a %2").arg(Logs::mb(&dhPrime[0], dhPrime.length()).str()).arg(Logs::mb(&g_a[0], g_a.length()).str()));
			return restart();
		}

		// check that dhPrime and (dhPrime - 1) / 2 are really prime using openssl BIGNUM methods
		_BigNumPrimeTest bnPrimeTest;
		if (!bnPrimeTest.isPrimeAndGood(&dhPrime[0], MTPMillerRabinIterCount, dh_inner_data.vg.v)) {
			LOG(("AuthKey Error: bad dh_prime primality!").arg(dhPrime.length()).arg(g_a.length()));
			DEBUG_LOG(("AuthKey Error: dh_prime %1").arg(Logs::mb(&dhPrime[0], dhPrime.length()).str()));
			return restart();
		}

		authKeyStrings->dh_prime = QByteArray(dhPrime.data(), dhPrime.size());
		authKeyData->g = dh_inner_data.vg.v;
		authKeyStrings->g_a = QByteArray(g_a.data(), g_a.size());
		authKeyData->retry_id = MTP_long(0);
		authKeyData->retries = 0;
	} return dhClientParamsSend();

	case mtpc_server_DH_params_fail: {
		const MTPDserver_DH_params_fail &encDH(res_DH_params.c_server_DH_params_fail());
		if (encDH.vnonce != authKeyData->nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in server_DH_params_fail)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&encDH.vnonce, 16).str()).arg(Logs::mb(&authKeyData->nonce, 16).str()));
			return restart();
		}
		if (encDH.vserver_nonce != authKeyData->server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in server_DH_params_fail)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&encDH.vserver_nonce, 16).str()).arg(Logs::mb(&authKeyData->server_nonce, 16).str()));
			return restart();
		}
		uchar sha1Buffer[20];
		if (encDH.vnew_nonce_hash != *(MTPint128*)(hashSha1(&authKeyData->new_nonce, 32, sha1Buffer) + 1)) {
			LOG(("AuthKey Error: received new_nonce_hash did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash: %1, new_nonce: %2").arg(Logs::mb(&encDH.vnew_nonce_hash, 16).str()).arg(Logs::mb(&authKeyData->new_nonce, 32).str()));
			return restart();
		}
		LOG(("AuthKey Error: server_DH_params_fail received!"));
	} return restart();

	}
	LOG(("AuthKey Error: unknown server_DH_params received, typeId = %1").arg(res_DH_params.type()));
	return restart();
}

void MTProtoConnectionPrivate::dhClientParamsSend() {
	if (++authKeyData->retries > 5) {
		LOG(("AuthKey Error: could not create auth_key for %1 retries").arg(authKeyData->retries - 1));
		return restart();
	}

	MTPClient_DH_Inner_Data client_dh_inner;
	MTPDclient_DH_inner_data &client_dh_inner_data(client_dh_inner._client_DH_inner_data());
	client_dh_inner_data.vnonce = authKeyData->nonce;
	client_dh_inner_data.vserver_nonce = authKeyData->server_nonce;
	client_dh_inner_data.vretry_id = authKeyData->retry_id;
	client_dh_inner_data.vg_b._string().v.resize(256);

	// gen rand 'b'
	uint32 b[64], *g_b((uint32*)&client_dh_inner_data.vg_b._string().v[0]), g_b_len;
	memset_rand(b, sizeof(b));

	// count g_b and auth_key using openssl BIGNUM methods
	_BigNumCounter bnCounter;
	if (!bnCounter.count(b, authKeyStrings->dh_prime.constData(), authKeyData->g, g_b, authKeyStrings->g_a.constData(), authKeyData->auth_key)) {
		return dhClientParamsSend();
	}

	// count auth_key hashes - parts of sha1(auth_key)
	uchar sha1Buffer[20];
	int32 *auth_key_sha = hashSha1(authKeyData->auth_key, 256, sha1Buffer);
	memcpy(&authKeyData->auth_key_aux_hash, auth_key_sha, 8);
	memcpy(&authKeyData->auth_key_hash, auth_key_sha + 3, 8);

	MTPSet_client_DH_params req_client_DH_params;
	req_client_DH_params.vnonce = authKeyData->nonce;
	req_client_DH_params.vserver_nonce = authKeyData->server_nonce;

	string &sdhEncString(req_client_DH_params.vencrypted_data._string().v);

	uint32 client_dh_inner_size = client_dh_inner.innerLength(), encSize = (client_dh_inner_size >> 2) + 5, encFullSize = encSize;
	if (encSize & 0x03) {
		encFullSize += 4 - (encSize & 0x03);
	}

	mtpBuffer encBuffer;
	encBuffer.reserve(encFullSize);
	encBuffer.resize(5);
	client_dh_inner.write(encBuffer);

	hashSha1(&encBuffer[5], client_dh_inner_size, &encBuffer[0]);
	if (encSize < encFullSize) {
		encBuffer.resize(encFullSize);
		memset_rand(&encBuffer[encSize], (encFullSize - encSize) * sizeof(mtpPrime));
	}

	sdhEncString.resize(encFullSize * 4);

	aesEncrypt(&encBuffer[0], &sdhEncString[0], encFullSize * sizeof(mtpPrime), authKeyData->aesKey, authKeyData->aesIV);

	connect(_conn, SIGNAL(receivedData()), this, SLOT(dhClientParamsAnswered()));

	DEBUG_LOG(("AuthKey Info: sending Req_client_DH_params.."));
	sendRequestNotSecure(req_client_DH_params);
}

void MTProtoConnectionPrivate::dhClientParamsAnswered() {
	QReadLocker lockFinished(&sessionDataMutex);
	if (!sessionData) return;

	disconnect(_conn, SIGNAL(receivedData()), this, SLOT(dhClientParamsAnswered()));
	DEBUG_LOG(("AuthKey Info: receiving Req_client_DH_params answer.."));

	MTPSet_client_DH_params::ResponseType res_client_DH_params;
	if (!readResponseNotSecure(res_client_DH_params)) {
		lockFinished.unlock();
		return restart();
	}

	switch (res_client_DH_params.type()) {
	case mtpc_dh_gen_ok: {
		const MTPDdh_gen_ok &resDH(res_client_DH_params.c_dh_gen_ok());
		if (resDH.vnonce != authKeyData->nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_ok)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&resDH.vnonce, 16).str()).arg(Logs::mb(&authKeyData->nonce, 16).str()));

			lockFinished.unlock();
			return restart();
		}
		if (resDH.vserver_nonce != authKeyData->server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_ok)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&resDH.vserver_nonce, 16).str()).arg(Logs::mb(&authKeyData->server_nonce, 16).str()));

			lockFinished.unlock();
			return restart();
		}
		authKeyData->new_nonce_buf[32] = 1;
		uchar sha1Buffer[20];
		if (resDH.vnew_nonce_hash1 != *(MTPint128*)(hashSha1(authKeyData->new_nonce_buf, 41, sha1Buffer) + 1)) {
			LOG(("AuthKey Error: received new_nonce_hash1 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash1: %1, new_nonce_buf: %2").arg(Logs::mb(&resDH.vnew_nonce_hash1, 16).str()).arg(Logs::mb(authKeyData->new_nonce_buf, 41).str()));

			lockFinished.unlock();
			return restart();
		}

		uint64 salt1 = authKeyData->new_nonce.l.l, salt2 = authKeyData->server_nonce.l, serverSalt = salt1 ^ salt2;
		sessionData->setSalt(serverSalt);

		mtpAuthKeyPtr authKey(new mtpAuthKey());
		authKey->setKey(authKeyData->auth_key);
		authKey->setDC(dc % _mtp_internal::dcShift);

		DEBUG_LOG(("AuthKey Info: auth key gen succeed, id: %1, server salt: %2, auth key: %3").arg(authKey->keyId()).arg(serverSalt).arg(Logs::mb(authKeyData->auth_key, 256).str()));

		sessionData->owner()->notifyKeyCreated(authKey); // slot will call authKeyCreated()
		sessionData->clear();
		unlockKey();
	} return;

	case mtpc_dh_gen_retry: {
		const MTPDdh_gen_retry &resDH(res_client_DH_params.c_dh_gen_retry());
		if (resDH.vnonce != authKeyData->nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_retry)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&resDH.vnonce, 16).str()).arg(Logs::mb(&authKeyData->nonce, 16).str()));

			lockFinished.unlock();
			return restart();
		}
		if (resDH.vserver_nonce != authKeyData->server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_retry)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&resDH.vserver_nonce, 16).str()).arg(Logs::mb(&authKeyData->server_nonce, 16).str()));

			lockFinished.unlock();
			return restart();
		}
		authKeyData->new_nonce_buf[32] = 2;
		uchar sha1Buffer[20];
		if (resDH.vnew_nonce_hash2 != *(MTPint128*)(hashSha1(authKeyData->new_nonce_buf, 41, sha1Buffer) + 1)) {
			LOG(("AuthKey Error: received new_nonce_hash2 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash2: %1, new_nonce_buf: %2").arg(Logs::mb(&resDH.vnew_nonce_hash2, 16).str()).arg(Logs::mb(authKeyData->new_nonce_buf, 41).str()));

			lockFinished.unlock();
			return restart();
		}
		authKeyData->retry_id = authKeyData->auth_key_aux_hash;
	} return dhClientParamsSend();

	case mtpc_dh_gen_fail: {
		const MTPDdh_gen_fail &resDH(res_client_DH_params.c_dh_gen_fail());
		if (resDH.vnonce != authKeyData->nonce) {
			LOG(("AuthKey Error: received nonce <> sent nonce (in dh_gen_fail)!"));
			DEBUG_LOG(("AuthKey Error: received nonce: %1, sent nonce: %2").arg(Logs::mb(&resDH.vnonce, 16).str()).arg(Logs::mb(&authKeyData->nonce, 16).str()));

			lockFinished.unlock();
			return restart();
		}
		if (resDH.vserver_nonce != authKeyData->server_nonce) {
			LOG(("AuthKey Error: received server_nonce <> sent server_nonce (in dh_gen_fail)!"));
			DEBUG_LOG(("AuthKey Error: received server_nonce: %1, sent server_nonce: %2").arg(Logs::mb(&resDH.vserver_nonce, 16).str()).arg(Logs::mb(&authKeyData->server_nonce, 16).str()));

			lockFinished.unlock();
			return restart();
		}
		authKeyData->new_nonce_buf[32] = 3;
		uchar sha1Buffer[20];
        if (resDH.vnew_nonce_hash3 != *(MTPint128*)(hashSha1(authKeyData->new_nonce_buf, 41, sha1Buffer) + 1)) {
			LOG(("AuthKey Error: received new_nonce_hash3 did not match!"));
			DEBUG_LOG(("AuthKey Error: received new_nonce_hash3: %1, new_nonce_buf: %2").arg(Logs::mb(&resDH.vnew_nonce_hash3, 16).str()).arg(Logs::mb(authKeyData->new_nonce_buf, 41).str()));

			lockFinished.unlock();
			return restart();
		}
		LOG(("AuthKey Error: dh_gen_fail received!"));
	}

		lockFinished.unlock();
		return restart();

	}
	LOG(("AuthKey Error: unknown set_client_DH_params_answer received, typeId = %1").arg(res_client_DH_params.type()));

	lockFinished.unlock();
	return restart();
}

void MTProtoConnectionPrivate::authKeyCreated() {
	clearAuthKeyData();

	connect(_conn, SIGNAL(receivedData()), this, SLOT(handleReceived()));

	if (sessionData->getSalt()) { // else receive salt in bad_server_salt first, then try to send all the requests
		setState(MTProtoConnection::Connected);
		if (restarted) {
			emit resendAllAsync();
			restarted = false;
		}
	}

	_pingIdToSend = MTP::nonce<uint64>(); // get server_salt

	emit needToSendAsync();
}

void MTProtoConnectionPrivate::clearAuthKeyData() {
	if (authKeyData) {
#ifdef Q_OS_WIN
		SecureZeroMemory(authKeyData, sizeof(AuthKeyCreateData));
		if (!authKeyStrings->dh_prime.isEmpty()) SecureZeroMemory(authKeyStrings->dh_prime.data(), authKeyStrings->dh_prime.size());
		if (!authKeyStrings->g_a.isEmpty()) SecureZeroMemory(authKeyStrings->g_a.data(), authKeyStrings->g_a.size());
#else
		memset(authKeyData, 0, sizeof(AuthKeyCreateData));
		if (!authKeyStrings->dh_prime.isEmpty()) memset(authKeyStrings->dh_prime.data(), 0, authKeyStrings->dh_prime.size());
		if (!authKeyStrings->g_a.isEmpty()) memset(authKeyStrings->g_a.data(), 0, authKeyStrings->g_a.size());
#endif
        delete authKeyData;
		authKeyData = 0;
		delete authKeyStrings;
		authKeyStrings = 0;
	}
}

void MTProtoConnectionPrivate::onError4(bool mayBeBadKey) {
	if (_conn && _conn == _conn6) return; // error in the unused

	if (_conn || !_conn6) {
		destroyConn();
		_waitForConnectedTimer.stop();

		MTP_LOG(dc, ("Restarting after error in IPv4 connection, maybe bad key: %1..").arg(Logs::b(mayBeBadKey)));
		return restart(mayBeBadKey);
	} else {
		destroyConn(&_conn4);
	}
}

void MTProtoConnectionPrivate::onError6(bool mayBeBadKey) {
	if (_conn && _conn == _conn4) return; // error in the unused

	if (_conn || !_conn4) {
		destroyConn();
		_waitForConnectedTimer.stop();

		MTP_LOG(dc, ("Restarting after error in IPv6 connection, maybe bad key: %1..").arg(Logs::b(mayBeBadKey)));
		return restart(mayBeBadKey);
	} else {
		destroyConn(&_conn6);
	}
}

void MTProtoConnectionPrivate::onReadyData() {
}

template <typename TRequest>
void MTProtoConnectionPrivate::sendRequestNotSecure(const TRequest &request) {
	try {
		mtpBuffer buffer;
		uint32 requestSize = request.innerLength() >> 2;

		buffer.resize(0);
		buffer.reserve(8 + requestSize);
		buffer.push_back(0); // tcp packet len
		buffer.push_back(0); // tcp packet num
		buffer.push_back(0);
		buffer.push_back(0);
		buffer.push_back(authKeyData->req_num);
		buffer.push_back(unixtime());
		buffer.push_back(requestSize * 4);
		request.write(buffer);
		buffer.push_back(0); // tcp crc32 hash
		++authKeyData->msgs_sent;

		DEBUG_LOG(("AuthKey Info: sending request, size: %1, num: %2, time: %3").arg(requestSize).arg(authKeyData->req_num).arg(buffer[5]));

		_conn->sendData(buffer);

		onSentSome(buffer.size() * sizeof(mtpPrime));

	} catch (Exception &e) {
		return restart();
	}
}

template <typename TResponse>
bool MTProtoConnectionPrivate::readResponseNotSecure(TResponse &response) {
	onReceivedSome();

	try {
		if (_conn->received().isEmpty()) {
			LOG(("AuthKey Error: trying to read response from empty received list"));
			return false;
		}
		mtpBuffer buffer(_conn->received().front());
		_conn->received().pop_front();

		const mtpPrime *answer(buffer.constData());
		uint32 len = buffer.size();
		if (len < 5) {
			LOG(("AuthKey Error: bad request answer, len = %1").arg(len * sizeof(mtpPrime)));
			DEBUG_LOG(("AuthKey Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
			return false;
		}
		if (answer[0] != 0 || answer[1] != 0 || (((uint32)answer[2]) & 0x03) != 1/* || (unixtime() - answer[3] > 300) || (answer[3] - unixtime() > 60)*/) { // didnt sync time yet
			LOG(("AuthKey Error: bad request answer start (%1 %2 %3)").arg(answer[0]).arg(answer[1]).arg(answer[2]));
			DEBUG_LOG(("AuthKey Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
			return false;
		}
		uint32 answerLen = (uint32)answer[4];
		if (answerLen != (len - 5) * sizeof(mtpPrime)) {
			LOG(("AuthKey Error: bad request answer %1 <> %2").arg(answerLen).arg((len - 5) * sizeof(mtpPrime)));
			DEBUG_LOG(("AuthKey Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
			return false;
		}
		const mtpPrime *from(answer + 5), *end(from + len - 5);
		response.read(from, end);
	} catch (Exception &e) {
		return false;
	}
	return true;
}

bool MTProtoConnectionPrivate::sendRequest(mtpRequest &request, bool needAnyResponse, QReadLocker &lockFinished) {
	uint32 fullSize = request->size();
	if (fullSize < 9) return false;

	uint32 messageSize = mtpRequestData::messageSize(request);
	if (messageSize < 5 || fullSize < messageSize + 4) return false;

	ReadLockerAttempt lock(sessionData->keyMutex());
	if (!lock) {
		DEBUG_LOG(("MTP Info: could not lock key for read in sendBuffer(), dc %1, restarting..").arg(dc));

		lockFinished.unlock();
		restart();
		return false;
	}

	mtpAuthKeyPtr key(sessionData->getKey());
	if (!key || key->keyId() != keyId) {
		DEBUG_LOG(("MTP Error: auth_key id for dc %1 changed").arg(dc));

		lockFinished.unlock();
		restart();
		return false;
	}

	uint32 padding = fullSize - 4 - messageSize;
	uint64 session(sessionData->getSession()), salt(sessionData->getSalt());

	memcpy(request->data() + 0, &salt, 2 * sizeof(mtpPrime));
	memcpy(request->data() + 2, &session, 2 * sizeof(mtpPrime));

	const mtpPrime *from = request->constData() + 4;
	MTP_LOG(dc, ("Send: ") + mtpTextSerialize(from, from + messageSize));

	uchar encryptedSHA[20];
	MTPint128 &msgKey(*(MTPint128*)(encryptedSHA + 4));
	hashSha1(request->constData(), (fullSize - padding) * sizeof(mtpPrime), encryptedSHA);

	mtpBuffer result;
	result.resize(9 + fullSize);
	*((uint64*)&result[2]) = keyId;
	*((MTPint128*)&result[4]) = msgKey;

	aesEncrypt(request->constData(), &result[8], fullSize * sizeof(mtpPrime), key, msgKey);

	DEBUG_LOG(("MTP Info: sending request, size: %1, num: %2, time: %3").arg(fullSize + 6).arg((*request)[4]).arg((*request)[5]));

	_conn->setSentEncrypted();
	_conn->sendData(result);

	if (needAnyResponse) {
		onSentSome(result.size() * sizeof(mtpPrime));
	}

	return true;
}

mtpRequestId MTProtoConnectionPrivate::wasSent(mtpMsgId msgId) const {
	if (msgId == _pingMsgId) return mtpRequestId(0xFFFFFFFF);
	{
		QReadLocker locker(sessionData->haveSentMutex());
		const mtpRequestMap &haveSent(sessionData->haveSentMap());
		mtpRequestMap::const_iterator i = haveSent.constFind(msgId);
		if (i != haveSent.cend()) return i.value()->requestId ? i.value()->requestId : mtpRequestId(0xFFFFFFFF);
	}
	{
		QReadLocker locker(sessionData->toResendMutex());
		const mtpRequestIdsMap &toResend(sessionData->toResendMap());
		mtpRequestIdsMap::const_iterator i = toResend.constFind(msgId);
		if (i != toResend.cend()) return i.value();
	}
	{
		QReadLocker locker(sessionData->wereAckedMutex());
		const mtpRequestIdsMap &wereAcked(sessionData->wereAckedMap());
		mtpRequestIdsMap::const_iterator i = wereAcked.constFind(msgId);
		if (i != wereAcked.cend()) return i.value();
	}
	return 0;
}

void MTProtoConnectionPrivate::lockKey() {
	unlockKey();
	sessionData->keyMutex()->lockForWrite();
	myKeyLock = true;
}

void MTProtoConnectionPrivate::unlockKey() {
	if (myKeyLock) {
		myKeyLock = false;
		sessionData->keyMutex()->unlock();
	}
}

MTProtoConnectionPrivate::~MTProtoConnectionPrivate() {
	doDisconnect();
}

void MTProtoConnectionPrivate::stop() {
	QWriteLocker lockFinished(&sessionDataMutex);
	if (sessionData) {
		if (myKeyLock) {
			sessionData->owner()->notifyKeyCreated(mtpAuthKeyPtr()); // release key lock, let someone else create it
			sessionData->keyMutex()->unlock();
			myKeyLock = false;
		}
		sessionData = 0;
	}
}

MTProtoConnection::~MTProtoConnection() {
}
