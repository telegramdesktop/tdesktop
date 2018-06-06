/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_tcp.h"

#include "base/bytes.h"
#include "base/openssl_help.h"
#include "base/qthelp_url.h"

extern "C" {
#include <openssl/aes.h>
} // extern "C"

namespace MTP {
namespace internal {
namespace {

constexpr auto kPacketSizeMax = 64 * 1024 * 1024U;
constexpr auto kFullConnectionTimeout = 8 * TimeMs(1000);

uint32 CountTcpPacketSize(const char *packet) { // must have at least 4 bytes readable
	uint32 result = (packet[0] > 0) ? packet[0] : 0;
	if (result == 0x7f) {
		const uchar *bytes = reinterpret_cast<const uchar*>(packet);
		result = (((uint32(bytes[3]) << 8) | uint32(bytes[2])) << 8) | uint32(bytes[1]);
		return (result << 2) + 4;
	}
	return (result << 2) + 1;
}

using ErrorSignal = void(QTcpSocket::*)(QAbstractSocket::SocketError);
const auto QTcpSocket_error = ErrorSignal(&QAbstractSocket::error);

} // namespace

TcpConnection::TcpConnection(QThread *thread, const ProxyData &proxy)
: AbstractConnection(thread, proxy)
, _currentPosition(reinterpret_cast<char*>(_shortBuffer))
, _checkNonce(rand_value<MTPint128>()) {
	_socket.moveToThread(thread);
	_socket.setProxy(ToNetworkProxy(proxy));
	connect(
		&_socket,
		&QTcpSocket::connected,
		this,
		&TcpConnection::socketConnected);
	connect(
		&_socket,
		&QTcpSocket::disconnected,
		this,
		&TcpConnection::socketDisconnected);
	connect(
		&_socket,
		&QTcpSocket::readyRead,
		this,
		&TcpConnection::socketRead);
	connect(
		&_socket,
		QTcpSocket_error,
		this,
		&TcpConnection::socketError);
}

ConnectionPointer TcpConnection::clone(const ProxyData &proxy) {
	return ConnectionPointer::New<TcpConnection>(thread(), proxy);
}

void TcpConnection::socketRead() {
	if (_socket.state() != QAbstractSocket::ConnectedState) {
		LOG(("MTP error: "
			"socket not connected in socketRead(), state: %1"
			).arg(_socket.state()));
		emit error(kErrorCodeOther);
		return;
	}

	do {
		uint32 toRead = _packetLeft
			? _packetLeft
			: (_readingToShort
				? (kShortBufferSize * sizeof(mtpPrime) - _packetRead)
				: 4);
		if (_readingToShort) {
			if (_currentPosition + toRead > ((char*)_shortBuffer) + kShortBufferSize * sizeof(mtpPrime)) {
				_longBuffer.resize(((_packetRead + toRead) >> 2) + 1);
				memcpy(&_longBuffer[0], _shortBuffer, _packetRead);
				_currentPosition = ((char*)&_longBuffer[0]) + _packetRead;
				_readingToShort = false;
			}
		} else {
			if (_longBuffer.size() * sizeof(mtpPrime) < _packetRead + toRead) {
				_longBuffer.resize(((_packetRead + toRead) >> 2) + 1);
				_currentPosition = ((char*)&_longBuffer[0]) + _packetRead;
			}
		}
		int32 bytes = (int32)_socket.read(_currentPosition, toRead);
		if (bytes > 0) {
			aesCtrEncrypt(_currentPosition, bytes, _receiveKey, &_receiveState);
			TCP_LOG(("TCP Info: read %1 bytes").arg(bytes));

			_packetRead += bytes;
			_currentPosition += bytes;
			if (_packetLeft) {
				_packetLeft -= bytes;
				if (!_packetLeft) {
					socketPacket(_currentPosition - _packetRead, _packetRead);
					_currentPosition = (char*)_shortBuffer;
					_packetRead = _packetLeft = 0;
					_readingToShort = true;
					_longBuffer.clear();
				} else {
					TCP_LOG(("TCP Info: not enough %1 for packet! read %2").arg(_packetLeft).arg(_packetRead));
					emit receivedSome();
				}
			} else {
				bool move = false;
				while (_packetRead >= 4) {
					uint32 packetSize = CountTcpPacketSize(_currentPosition - _packetRead);
					if (packetSize < 5 || packetSize > kPacketSizeMax) {
						LOG(("TCP Error: packet size = %1").arg(packetSize));
						emit error(kErrorCodeOther);
						return;
					}
					if (_packetRead >= packetSize) {
						socketPacket(_currentPosition - _packetRead, packetSize);
						_packetRead -= packetSize;
						_packetLeft = 0;
						move = true;
					} else {
						_packetLeft = packetSize - _packetRead;
						TCP_LOG(("TCP Info: not enough %1 for packet! size %2 read %3").arg(_packetLeft).arg(packetSize).arg(_packetRead));
						emit receivedSome();
						break;
					}
				}
				if (move) {
					if (!_packetRead) {
						_currentPosition = (char*)_shortBuffer;
						_readingToShort = true;
						_longBuffer.clear();
					} else if (!_readingToShort && _packetRead < kShortBufferSize * sizeof(mtpPrime)) {
						memcpy(_shortBuffer, _currentPosition - _packetRead, _packetRead);
						_currentPosition = (char*)_shortBuffer + _packetRead;
						_readingToShort = true;
						_longBuffer.clear();
					}
				}
			}
		} else if (bytes < 0) {
			LOG(("TCP Error: socket read return -1"));
			emit error(kErrorCodeOther);
			return;
		} else {
			TCP_LOG(("TCP Info: no bytes read, but bytes available was true..."));
			break;
		}
	} while (_socket.state() == QAbstractSocket::ConnectedState && _socket.bytesAvailable());
}

mtpBuffer TcpConnection::handleResponse(const char *packet, uint32 length) {
	if (length < 5 || length > kPacketSizeMax) {
		LOG(("TCP Error: bad packet size %1").arg(length));
		return mtpBuffer(1, -500);
	}
	int32 size = packet[0], len = length - 1;
	if (size == 0x7f) {
		const uchar *bytes = reinterpret_cast<const uchar*>(packet);
		size = (((uint32(bytes[3]) << 8) | uint32(bytes[2])) << 8) | uint32(bytes[1]);
		len -= 3;
	}
	if (size * int32(sizeof(mtpPrime)) != len) {
		LOG(("TCP Error: bad packet header"));
		TCP_LOG(("TCP Error: bad packet header, packet: %1").arg(Logs::mb(packet, length).str()));
		return mtpBuffer(1, -500);
	}
	const mtpPrime *packetdata = reinterpret_cast<const mtpPrime*>(packet + (length - len));
	TCP_LOG(("TCP Info: packet received, size = %1").arg(size * sizeof(mtpPrime)));
	if (size == 1) {
		LOG(("TCP Error: "
			"error packet received, endpoint: '%1:%2', "
			"protocolDcId: %3, secret_len: %4, code = %5"
			).arg(_address.isEmpty() ? ("proxy_" + _proxy.host) : _address
			).arg(_address.isEmpty() ? _proxy.port : _port
			).arg(_protocolDcId
			).arg(_protocolSecret.size()
			).arg(*packetdata));
		return mtpBuffer(1, *packetdata);
	}

	mtpBuffer data(size);
	memcpy(data.data(), packetdata, size * sizeof(mtpPrime));

	return data;
}

void TcpConnection::handleError(QAbstractSocket::SocketError e, QTcpSocket &socket) {
	switch (e) {
	case QAbstractSocket::ConnectionRefusedError:
	LOG(("TCP Error: socket connection refused - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::RemoteHostClosedError:
	TCP_LOG(("TCP Info: remote host closed socket connection - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::HostNotFoundError:
	LOG(("TCP Error: host not found - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::SocketTimeoutError:
	LOG(("TCP Error: socket timeout - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::NetworkError:
	LOG(("TCP Error: network - %1").arg(socket.errorString()));
	break;

	case QAbstractSocket::ProxyAuthenticationRequiredError:
	case QAbstractSocket::ProxyConnectionRefusedError:
	case QAbstractSocket::ProxyConnectionClosedError:
	case QAbstractSocket::ProxyConnectionTimeoutError:
	case QAbstractSocket::ProxyNotFoundError:
	case QAbstractSocket::ProxyProtocolError:
	LOG(("TCP Error: proxy (%1) - %2").arg(e).arg(socket.errorString()));
	break;

	default:
	LOG(("TCP Error: other (%1) - %2").arg(e).arg(socket.errorString()));
	break;
	}

	TCP_LOG(("TCP Error %1, restarting! - %2").arg(e).arg(socket.errorString()));
}

void TcpConnection::socketConnected() {
	Expects(_status == Status::Waiting);

	auto buffer = preparePQFake(_checkNonce);

	DEBUG_LOG(("TCP Info: "
		"dc:%1 - Sending fake req_pq to '%2'"
		).arg(_protocolDcId
		).arg(_address + ':' + QString::number(_port)));

	_pingTime = getms();
	sendData(buffer);
}

void TcpConnection::socketDisconnected() {
	if (_status == Status::Waiting || _status == Status::Ready) {
		emit disconnected();
	}
}

void TcpConnection::sendData(mtpBuffer &buffer) {
	if (_status == Status::Finished) return;

	if (buffer.size() < 3) {
		LOG(("TCP Error: writing bad packet, len = %1").arg(buffer.size() * sizeof(mtpPrime)));
		TCP_LOG(("TCP Error: bad packet %1").arg(Logs::mb(&buffer[0], buffer.size() * sizeof(mtpPrime)).str()));
		emit error(kErrorCodeOther);
		return;
	}

	sendBuffer(buffer);
}

void TcpConnection::writeConnectionStart() {
	// prepare random part
	auto nonceBytes = bytes::vector(64);
	const auto nonce = bytes::make_span(nonceBytes);

	const auto zero = reinterpret_cast<uchar*>(nonce.data());
	const auto first = reinterpret_cast<uint32*>(nonce.data());
	const auto second = first + 1;
	const auto reserved01 = 0x000000EFU;
	const auto reserved11 = 0x44414548U;
	const auto reserved12 = 0x54534F50U;
	const auto reserved13 = 0x20544547U;
	const auto reserved14 = 0xEEEEEEEEU;
	const auto reserved21 = 0x00000000U;
	do {
		bytes::set_random(nonce);
	} while (*zero == reserved01
		|| *first == reserved11
		|| *first == reserved12
		|| *first == reserved13
		|| *first == reserved14
		|| *second == reserved21);

	const auto prepareKey = [&](bytes::span key, bytes::const_span from) {
		if (_protocolSecret.size() == 16) {
			const auto payload = bytes::concatenate(from, _protocolSecret);
			bytes::copy(key, openssl::Sha256(payload));
		} else if (_protocolSecret.empty()) {
			bytes::copy(key, from);
		} else {
			bytes::set_with_const(key, gsl::byte{});
		}
	};

	// prepare encryption key/iv
	prepareKey(
		bytes::make_span(_sendKey),
		nonce.subspan(8, CTRState::KeySize));
	bytes::copy(
		bytes::make_span(_sendState.ivec),
		nonce.subspan(8 + CTRState::KeySize, CTRState::IvecSize));

	// prepare decryption key/iv
	auto reversedBytes = bytes::vector(48);
	const auto reversed = bytes::make_span(reversedBytes);
	bytes::copy(reversed, nonce.subspan(8, reversed.size()));
	std::reverse(reversed.begin(), reversed.end());
	prepareKey(
		bytes::make_span(_receiveKey),
		reversed.subspan(0, CTRState::KeySize));
	bytes::copy(
		bytes::make_span(_receiveState.ivec),
		reversed.subspan(CTRState::KeySize, CTRState::IvecSize));

	// write protocol and dc ids
	const auto protocol = reinterpret_cast<uint32*>(nonce.data() + 56);
	*protocol = 0xEFEFEFEFU;
	const auto dcId = reinterpret_cast<int16*>(nonce.data() + 60);
	*dcId = _protocolDcId;

	_socket.write(reinterpret_cast<const char*>(nonce.data()), 56);
	aesCtrEncrypt(nonce.data(), 64, _sendKey, &_sendState);
	_socket.write(reinterpret_cast<const char*>(nonce.subspan(56).data()), 8);
}

void TcpConnection::sendBuffer(mtpBuffer &buffer) {
	if (!_packetIndex++) {
		writeConnectionStart();
	}

	uint32 size = buffer.size() - 3, len = size * 4;
	char *data = reinterpret_cast<char*>(&buffer[0]);
	if (size < 0x7f) {
		data[7] = char(size);
		TCP_LOG(("TCP Info: write %1 packet %2").arg(_packetIndex).arg(len + 1));

		aesCtrEncrypt(data + 7, len + 1, _sendKey, &_sendState);
		_socket.write(data + 7, len + 1);
	} else {
		data[4] = 0x7f;
		reinterpret_cast<uchar*>(data)[5] = uchar(size & 0xFF);
		reinterpret_cast<uchar*>(data)[6] = uchar((size >> 8) & 0xFF);
		reinterpret_cast<uchar*>(data)[7] = uchar((size >> 16) & 0xFF);
		TCP_LOG(("TCP Info: write %1 packet %2").arg(_packetIndex).arg(len + 4));

		aesCtrEncrypt(data + 4, len + 4, _sendKey, &_sendState);
		_socket.write(data + 4, len + 4);
	}
}

void TcpConnection::disconnectFromServer() {
	if (_status == Status::Finished) return;
	_status = Status::Finished;

	disconnect(&_socket, &QTcpSocket::connected, nullptr, nullptr);
	disconnect(&_socket, &QTcpSocket::disconnected, nullptr, nullptr);
	disconnect(&_socket, &QTcpSocket::readyRead, nullptr, nullptr);
	disconnect(&_socket, QTcpSocket_error, nullptr, nullptr);
	_socket.close();
}

void TcpConnection::connectToServer(
		const QString &address,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) {
	Expects(_address.isEmpty());
	Expects(_port == 0);
	Expects(_protocolSecret.empty());
	Expects(_protocolDcId == 0);

	if (_proxy.type == ProxyData::Type::Mtproto) {
		_address = _proxy.host;
		_port = _proxy.port;
		_protocolSecret = ProtocolSecretFromPassword(_proxy.password);

		DEBUG_LOG(("TCP Info: "
			"dc:%1 - Connecting to proxy '%2'"
			).arg(protocolDcId
			).arg(_address + ':' + QString::number(_port)));
	} else {
		_address = address;
		_port = port;
		_protocolSecret = protocolSecret;

		DEBUG_LOG(("TCP Info: "
			"dc:%1 - Connecting to '%2'"
			).arg(protocolDcId
			).arg(_address + ':' + QString::number(_port)));
	}
	_protocolDcId = protocolDcId;

	_socket.connectToHost(_address, _port);
}

TimeMs TcpConnection::pingTime() const {
	return isConnected() ? _pingTime : TimeMs(0);
}

TimeMs TcpConnection::fullConnectTimeout() const {
	return kFullConnectionTimeout;
}

void TcpConnection::socketPacket(const char *packet, uint32 length) {
	if (_status == Status::Finished) return;

	const auto data = handleResponse(packet, length);
	if (data.size() == 1) {
		emit error(data[0]);
	} else if (_status == Status::Ready) {
		_receivedQueue.push_back(data);
		emit receivedData();
	} else if (_status == Status::Waiting) {
		try {
			const auto res_pq = readPQFakeReply(data);
			const auto &data = res_pq.c_resPQ();
			if (data.vnonce == _checkNonce) {
				DEBUG_LOG(("Connection Info: Valid pq response by TCP."));
				_status = Status::Ready;
				disconnect(
					&_socket,
					&QTcpSocket::connected,
					nullptr,
					nullptr);
				_pingTime = (getms() - _pingTime);
				emit connected();
			}
		} catch (Exception &e) {
			DEBUG_LOG(("Connection Error: exception in parsing TCP fake pq-responce, %1").arg(e.what()));
			emit error(kErrorCodeOther);
		}
	}
}

bool TcpConnection::isConnected() const {
	return (_status == Status::Ready);
}

int32 TcpConnection::debugState() const {
	return _socket.state();
}

QString TcpConnection::transport() const {
	if (!isConnected()) {
		return QString();
	}
	auto result = qsl("TCP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	}
	return result;
}

QString TcpConnection::tag() const {
	auto result = qsl("TCP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	} else {
		result += qsl("/IPv4");
	}
	return result;
}

void TcpConnection::socketError(QAbstractSocket::SocketError e) {
	if (_status == Status::Finished) return;

	handleError(e, _socket);
	emit error(kErrorCodeOther);
}

} // namespace internal
} // namespace MTP
