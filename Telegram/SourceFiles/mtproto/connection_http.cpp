/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_http.h"

#include "base/openssl_help.h"
#include "base/qthelp_url.h"

namespace MTP {
namespace details {
namespace {

constexpr auto kForceHttpPort = 80;
constexpr auto kFullConnectionTimeout = crl::time(8000);

} // namespace

HttpConnection::HttpConnection(QThread *thread, const ProxyData &proxy)
: AbstractConnection(thread, proxy)
, _checkNonce(openssl::RandomValue<MTPint128>()) {
	_manager.moveToThread(thread);
	_manager.setProxy(ToNetworkProxy(proxy));
}

ConnectionPointer HttpConnection::clone(const ProxyData &proxy) {
	return ConnectionPointer::New<HttpConnection>(thread(), proxy);
}

void HttpConnection::sendData(mtpBuffer &&buffer) {
	Expects(buffer.size() > 2);

	if (_status == Status::Finished) {
		return;
	}

	int32 requestSize = (buffer.size() - 2) * sizeof(mtpPrime);

	QNetworkRequest request(url());
	request.setHeader(QNetworkRequest::ContentLengthHeader, QVariant(requestSize));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(qsl("application/x-www-form-urlencoded")));

	TCP_LOG(("HTTP Info: sending %1 len request").arg(requestSize));
	_requests.insert(_manager.post(request, QByteArray((const char*)(&buffer[2]), requestSize)));
}

void HttpConnection::disconnectFromServer() {
	if (_status == Status::Finished) return;
	_status = Status::Finished;

	for (const auto request : base::take(_requests)) {
		request->abort();
		request->deleteLater();
	}

	disconnect(
		&_manager,
		&QNetworkAccessManager::finished,
		this,
		&HttpConnection::requestFinished);
}

void HttpConnection::connectToServer(
		const QString &address,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) {
	_address = address;
	connect(
		&_manager,
		&QNetworkAccessManager::finished,
		this,
		&HttpConnection::requestFinished);

	auto buffer = preparePQFake(_checkNonce);

	DEBUG_LOG(("HTTP Info: "
		"dc:%1 - Sending fake req_pq to '%2'"
		).arg(protocolDcId
		).arg(url().toDisplayString()));

	_pingTime = crl::now();
	sendData(std::move(buffer));
}

mtpBuffer HttpConnection::handleResponse(QNetworkReply *reply) {
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

qint32 HttpConnection::handleError(QNetworkReply *reply) { // returnes "maybe bad key"
	auto result = qint32(kErrorCodeOther);

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		result = -status;
	}

	switch (reply->error()) {
	case QNetworkReply::ConnectionRefusedError: LOG(("HTTP Error: connection refused - %1").arg(reply->errorString())); break;
	case QNetworkReply::RemoteHostClosedError: LOG(("HTTP Error: remote host closed - %1").arg(reply->errorString())); break;
	case QNetworkReply::HostNotFoundError: LOG(("HTTP Error: host not found - %1").arg(reply->errorString())); break;
	case QNetworkReply::TimeoutError: LOG(("HTTP Error: timeout - %1").arg(reply->errorString())); break;
	case QNetworkReply::OperationCanceledError: LOG(("HTTP Error: cancelled - %1").arg(reply->errorString())); break;
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
	case QNetworkReply::UnknownProxyError: LOG(("HTTP Error: proxy error %1 - %2").arg(reply->error()).arg(reply->errorString())); break;

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

	return result;
}

bool HttpConnection::isConnected() const {
	return (_status == Status::Ready);
}

void HttpConnection::requestFinished(QNetworkReply *reply) {
	if (_status == Status::Finished) return;

	reply->deleteLater();
	if (reply->error() == QNetworkReply::NoError) {
		_requests.remove(reply);

		mtpBuffer data = handleResponse(reply);
		if (data.size() == 1) {
			error(data[0]);
		} else if (!data.isEmpty()) {
			if (_status == Status::Ready) {
				_receivedQueue.push_back(data);
				receivedData();
			} else if (const auto res_pq = readPQFakeReply(data)) {
				const auto &data = res_pq->c_resPQ();
				if (data.vnonce() == _checkNonce) {
					DEBUG_LOG(("Connection Info: "
						"HTTP-transport to %1 connected by pq-response"
						).arg(_address));
					_status = Status::Ready;
					_pingTime = crl::now() - _pingTime;
					connected();
				} else {
					DEBUG_LOG(("Connection Error: "
						"Wrong nonce received in HTTP fake pq-responce"));
					error(kErrorCodeOther);
				}
			} else {
				DEBUG_LOG(("Connection Error: "
					"Could not parse HTTP fake pq-responce"));
				error(kErrorCodeOther);
			}
		}
	} else {
		if (!_requests.remove(reply)) {
			return;
		}

		error(handleError(reply));
	}
}

crl::time HttpConnection::pingTime() const {
	return isConnected() ? _pingTime : crl::time(0);
}

crl::time HttpConnection::fullConnectTimeout() const {
	return kFullConnectionTimeout;
}

bool HttpConnection::usingHttpWait() {
	return true;
}

bool HttpConnection::needHttpWait() {
	return _requests.isEmpty();
}

int32 HttpConnection::debugState() const {
	return -1;
}

QString HttpConnection::transport() const {
	if (!isConnected()) {
		return QString();
	}
	auto result = qsl("HTTP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	}
	return result;
}

QString HttpConnection::tag() const {
	auto result = qsl("HTTP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	} else {
		result += qsl("/IPv4");
	}
	return result;
}

QUrl HttpConnection::url() const {
	const auto pattern = qthelp::is_ipv6(_address)
		? qsl("http://[%1]:%2/api")
		: qsl("http://%1:%2/api");

	// Not endpoint.port - always 80 port for http transport.
	return QUrl(pattern.arg(_address).arg(kForceHttpPort));
}

} // namespace details
} // namespace MTP
