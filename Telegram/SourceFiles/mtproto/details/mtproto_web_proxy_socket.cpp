/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_web_proxy_socket.h"

#include "mtproto/web_proxy/web_proxy_transport.h"

namespace MTP::details {

WebProxySocket::WebProxySocket(
		not_null<QThread*> thread,
		const ProxyData &proxy)
: AbstractSocket(thread)
, _streamId(WebProxy::Transport::NextStreamId())
, _transport(WebProxy::Transport::Instance()) {
	Expects(proxy.type == ProxyData::Type::Web);
}

WebProxySocket::~WebProxySocket() {
	if (_transport) {
		_transport->closeStream(_streamId);
	}
}

void WebProxySocket::connectToHost(const QString &, int) {
	Expects(_state == State::NotConnected);

	_state = State::Connecting;
	if (_transport) {
		_transport->registerStream(_streamId, {
			.context = this,
			.connected = [=] { transportConnected(); },
			.data = [=](QByteArray data) {
				transportData(std::move(data));
			},
			.disconnected = [=] { transportDisconnected(); },
			.failed = [=] { transportFailed(); },
		});
	} else {
		transportFailed();
	}
}

bool WebProxySocket::isGoodStartNonce(bytes::const_span nonce) {
	Expects(nonce.size() >= 2 * sizeof(uint32));

	const auto bytes = nonce.data();
	const auto zero = *reinterpret_cast<const uchar*>(bytes);
	const auto first = *reinterpret_cast<const uint32*>(bytes);
	const auto second = *(reinterpret_cast<const uint32*>(bytes) + 1);
	return (zero != 0xEFU)
		&& (first != 0x44414548U)
		&& (first != 0x54534F50U)
		&& (first != 0x20544547U)
		&& (first != 0xEEEEEEEEU)
		&& (first != 0xDDDDDDDDU)
		&& (first != 0x02010316U)
		&& (second != 0x00000000U);
}

void WebProxySocket::timedOut() {
}

bool WebProxySocket::isConnected() {
	return (_state == State::Connected);
}

bool WebProxySocket::hasBytesAvailable() {
	return (_incomingOffset < _incoming.size());
}

int64 WebProxySocket::read(bytes::span buffer) {
	const auto available = _incoming.size() - _incomingOffset;
	if (available <= 0) {
		return 0;
	}
	const auto count = int(std::min<std::size_t>(available, buffer.size()));
	if (!count) {
		return 0;
	}
	bytes::copy(
		buffer,
		bytes::make_span(_incoming).subspan(_incomingOffset, count));
	_incomingOffset += count;
	if (_incomingOffset == _incoming.size()) {
		_incoming.clear();
		_incomingOffset = 0;
	} else if (_incomingOffset >= 64 * 1024
		&& _incomingOffset >= _incoming.size() / 2) {
		_incoming.remove(0, _incomingOffset);
		_incomingOffset = 0;
	}
	if (_transport) {
		_transport->grantWindow(_streamId, count);
	}
	return count;
}

void WebProxySocket::write(
		bytes::const_span prefix,
		bytes::const_span buffer) {
	Expects(!buffer.empty());

	if (!isConnected() || !_transport) {
		return;
	}
	auto data = QByteArray(
		int(prefix.size() + buffer.size()),
		Qt::Uninitialized);
	if (!prefix.empty()) {
		memcpy(data.data(), prefix.data(), prefix.size());
	}
	memcpy(data.data() + prefix.size(), buffer.data(), buffer.size());
	_transport->sendData(_streamId, std::move(data));
}

int32 WebProxySocket::debugState() {
	return int32(_state);
}

QString WebProxySocket::debugPostfix() const {
	return u"_web"_q;
}

void WebProxySocket::transportConnected() {
	if (_state != State::Connecting) {
		return;
	}
	_state = State::Connected;
	_connected.fire({});
}

void WebProxySocket::transportData(QByteArray data) {
	if (_state != State::Connected || data.isEmpty()) {
		return;
	}
	_incoming.append(data);
	_readyRead.fire({});
}

void WebProxySocket::transportDisconnected() {
	if (_state == State::Disconnected || _state == State::Error) {
		return;
	}
	_state = State::Disconnected;
	_disconnected.fire({});
}

void WebProxySocket::transportFailed() {
	if (_state == State::Disconnected || _state == State::Error) {
		return;
	}
	_state = State::Error;
	_error.fire({});
}

} // namespace MTP::details
