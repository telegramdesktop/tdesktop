/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_resolving.h"

#include "mtproto/mtp_instance.h"

namespace MTP {
namespace details {
namespace {

constexpr auto kOneConnectionTimeout = 4000;

} // namespace

ResolvingConnection::ResolvingConnection(
	not_null<Instance*> instance,
	QThread *thread,
	const ProxyData &proxy,
	ConnectionPointer &&child)
: AbstractConnection(thread, proxy)
, _instance(instance)
, _timeoutTimer([=] { handleError(kErrorCodeOther); }) {
	setChild(std::move(child));
	if (proxy.resolvedExpireAt < crl::now()) {
		const auto host = proxy.host;
		connect(
			instance,
			&Instance::proxyDomainResolved,
			this,
			&ResolvingConnection::domainResolved,
			Qt::QueuedConnection);
		InvokeQueued(instance, [=] {
			instance->resolveProxyDomain(host);
		});
	}
	if (!proxy.resolvedIPs.empty()) {
		refreshChild();
	}
}

ConnectionPointer ResolvingConnection::clone(const ProxyData &proxy) {
	Unexpected("ResolvingConnection::clone call.");
}

void ResolvingConnection::setChild(ConnectionPointer &&child) {
	_child = std::move(child);
	connect(
		_child,
		&AbstractConnection::receivedData,
		this,
		&ResolvingConnection::handleReceivedData);
	connect(
		_child,
		&AbstractConnection::receivedSome,
		this,
		&ResolvingConnection::receivedSome);
	connect(
		_child,
		&AbstractConnection::error,
		this,
		&ResolvingConnection::handleError);
	connect(_child,
		&AbstractConnection::connected,
		this,
		&ResolvingConnection::handleConnected);
	connect(_child,
		&AbstractConnection::disconnected,
		this,
		&ResolvingConnection::handleDisconnected);
	DEBUG_LOG(("Resolving Info: dc:%1 proxy '%2' got new child '%3'").arg(
		QString::number(_protocolDcId),
		_proxy.host + ':' + QString::number(_proxy.port),
		(_ipIndex >= 0 && _ipIndex < _proxy.resolvedIPs.size())
			? _proxy.resolvedIPs[_ipIndex]
			: _proxy.host));
	if (_protocolDcId) {
		_child->connectToServer(
			_address,
			_port,
			_protocolSecret,
			_protocolDcId,
			_protocolForFiles);
	}
}

void ResolvingConnection::domainResolved(
		const QString &host,
		const QStringList &ips,
		qint64 expireAt) {
	if (host != _proxy.host || !_child) {
		return;
	}
	_proxy.resolvedExpireAt = expireAt;

	auto index = 0;
	for (const auto &ip : ips) {
		if (index >= _proxy.resolvedIPs.size()) {
			_proxy.resolvedIPs.push_back(ip);
		} else if (_proxy.resolvedIPs[index] != ip) {
			_proxy.resolvedIPs[index] = ip;
			if (_ipIndex >= index) {
				_ipIndex = index - 1;
				refreshChild();
			}
		}
		++index;
	}
	if (index < _proxy.resolvedIPs.size()) {
		_proxy.resolvedIPs.resize(index);
		if (_ipIndex >= index) {
			emitError(kErrorCodeOther);
		}
	}
	if (_ipIndex < 0) {
		refreshChild();
	}
}

bool ResolvingConnection::refreshChild() {
	if (!_child) {
		return true;
	} else if (++_ipIndex >= _proxy.resolvedIPs.size()) {
		return false;
	}
	setChild(_child->clone(ToDirectIpProxy(_proxy, _ipIndex)));
	_timeoutTimer.callOnce(kOneConnectionTimeout);
	return true;
}

void ResolvingConnection::emitError(int errorCode) {
	_ipIndex = -1;
	_child = nullptr;
	error(errorCode);
}

void ResolvingConnection::handleError(int errorCode) {
	if (_connected) {
		emitError(errorCode);
	} else if (!_proxy.resolvedIPs.empty()) {
		if (!refreshChild()) {
			emitError(errorCode);
		}
	} else {
		// Wait for the domain to be resolved.
	}
}

void ResolvingConnection::handleDisconnected() {
	if (_connected) {
		disconnected();
	} else {
		handleError(kErrorCodeOther);
	}
}

void ResolvingConnection::handleReceivedData() {
	auto &my = received();
	auto &his = _child->received();
	for (auto &item : his) {
		my.push_back(std::move(item));
	}
	his.clear();
	receivedData();
}

void ResolvingConnection::handleConnected() {
	_connected = true;
	_timeoutTimer.cancel();
	if (_ipIndex >= 0) {
		const auto host = _proxy.host;
		const auto good = _proxy.resolvedIPs[_ipIndex];
		const auto instance = _instance;
		InvokeQueued(_instance, [=] {
			instance->setGoodProxyDomain(host, good);
		});
	}
	connected();
}

crl::time ResolvingConnection::pingTime() const {
	Expects(_child != nullptr);

	return _child->pingTime();
}

crl::time ResolvingConnection::fullConnectTimeout() const {
	return kOneConnectionTimeout * qMax(int(_proxy.resolvedIPs.size()), 1);
}

void ResolvingConnection::sendData(mtpBuffer &&buffer) {
	Expects(_child != nullptr);

	_child->sendData(std::move(buffer));
}

void ResolvingConnection::disconnectFromServer() {
	_address = QString();
	_port = 0;
	_protocolSecret = bytes::vector();
	_protocolDcId = 0;
	if (!_child) {
		return;
	}
	_child->disconnectFromServer();
}

void ResolvingConnection::connectToServer(
		const QString &address,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId,
		bool protocolForFiles) {
	if (!_child) {
		InvokeQueued(this, [=] { emitError(kErrorCodeOther); });
		return;
	}
	_address = address;
	_port = port;
	_protocolSecret = protocolSecret;
	_protocolDcId = protocolDcId;
	_protocolForFiles = protocolForFiles;
	DEBUG_LOG(("Resolving Info: dc:%1 proxy '%2' connects a child '%3'").arg(
		QString::number(_protocolDcId),
		_proxy.host +':' + QString::number(_proxy.port),
		(_ipIndex >= 0 && _ipIndex < _proxy.resolvedIPs.size())
			? _proxy.resolvedIPs[_ipIndex]
			: _proxy.host));
	return _child->connectToServer(
		address,
		port,
		protocolSecret,
		protocolDcId,
		protocolForFiles);
}

bool ResolvingConnection::isConnected() const {
	return _child ? _child->isConnected() : false;
}

int32 ResolvingConnection::debugState() const {
	return _child ? _child->debugState() : -1;
}

QString ResolvingConnection::transport() const {
	return _child ? _child->transport() : QString();
}

QString ResolvingConnection::tag() const {
	return _child ? _child->tag() : QString();
}

} // namespace details
} // namespace MTP
