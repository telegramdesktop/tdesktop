/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_http.h"

#include "base/qthelp_url.h"

namespace MTP {
namespace internal {
namespace {

constexpr auto kForceHttpPort = 80;

} // namespace

mtpBuffer HTTPConnection::handleResponse(QNetworkReply *reply) {
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

qint32 HTTPConnection::handleError(QNetworkReply *reply) { // returnes "maybe bad key"
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

HTTPConnection::HTTPConnection(QThread *thread) : AbstractConnection(thread)
, status(WaitingHttp)
, httpNonce(rand_value<MTPint128>()) {
	manager.moveToThread(thread);
}

void HTTPConnection::setProxyOverride(const ProxyData &proxy) {
	manager.setProxy(ToNetworkProxy(proxy));
}

void HTTPConnection::sendData(mtpBuffer &buffer) {
	if (status == FinishedWork) return;

	if (buffer.size() < 3) {
		LOG(("TCP Error: writing bad packet, len = %1").arg(buffer.size() * sizeof(mtpPrime)));
		TCP_LOG(("TCP Error: bad packet %1").arg(Logs::mb(&buffer[0], buffer.size() * sizeof(mtpPrime)).str()));
		emit error(kErrorCodeOther);
		return;
	}

	int32 requestSize = (buffer.size() - 3) * sizeof(mtpPrime);

	QNetworkRequest request(url());
	request.setHeader(QNetworkRequest::ContentLengthHeader, QVariant(requestSize));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(qsl("application/x-www-form-urlencoded")));

	TCP_LOG(("HTTP Info: sending %1 len request %2").arg(requestSize).arg(Logs::mb(&buffer[2], requestSize).str()));
	requests.insert(manager.post(request, QByteArray((const char*)(&buffer[2]), requestSize)));
}

void HTTPConnection::disconnectFromServer() {
	if (status == FinishedWork) return;
	status = FinishedWork;

	Requests copy = requests;
	requests.clear();
	for (Requests::const_iterator i = copy.cbegin(), e = copy.cend(); i != e; ++i) {
		(*i)->abort();
		(*i)->deleteLater();
	}

	disconnect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestFinished(QNetworkReply*)));
}

void HTTPConnection::connectToServer(
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) {
	_address = ip;
	TCP_LOG(("HTTP Info: address is %1").arg(url().toDisplayString()));
	connect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestFinished(QNetworkReply*)));

	mtpBuffer buffer(preparePQFake(httpNonce));

	DEBUG_LOG(("Connection Info: sending fake req_pq through HTTP transport to %1").arg(ip));

	_pingTime = getms();
	sendData(buffer);
}

bool HTTPConnection::isConnected() const {
	return (status == UsingHttp);
}

void HTTPConnection::requestFinished(QNetworkReply *reply) {
	if (status == FinishedWork) return;

	reply->deleteLater();
	if (reply->error() == QNetworkReply::NoError) {
		requests.remove(reply);

		mtpBuffer data = handleResponse(reply);
		if (data.size() == 1) {
			emit error(data[0]);
		} else if (!data.isEmpty()) {
			if (status == UsingHttp) {
				_receivedQueue.push_back(data);
				emit receivedData();
			} else {
				try {
					auto res_pq = readPQFakeReply(data);
					const auto &res_pq_data(res_pq.c_resPQ());
					if (res_pq_data.vnonce == httpNonce) {
						DEBUG_LOG(("Connection Info: HTTP-transport to %1 connected by pq-response").arg(_address));
						status = UsingHttp;
						_pingTime = getms() - _pingTime;
						emit connected();
					}
				} catch (Exception &e) {
					DEBUG_LOG(("Connection Error: exception in parsing HTTP fake pq-responce, %1").arg(e.what()));
					emit error(kErrorCodeOther);
				}
			}
		}
	} else {
		if (!requests.remove(reply)) {
			return;
		}

		emit error(handleError(reply));
	}
}

TimeMs HTTPConnection::pingTime() const {
	return isConnected() ? _pingTime : TimeMs(0);
}

bool HTTPConnection::usingHttpWait() {
	return true;
}

bool HTTPConnection::needHttpWait() {
	return requests.isEmpty();
}

int32 HTTPConnection::debugState() const {
	return -1;
}

QString HTTPConnection::transport() const {
	if (!isConnected()) {
		return QString();
	}
	auto result = qsl("HTTP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	}
	return result;
}

QString HTTPConnection::tag() const {
	auto result = qsl("HTTP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	} else {
		result += qsl("/IPv4");
	}
	return result;
}

QUrl HTTPConnection::url() const {
	const auto pattern = qthelp::is_ipv6(_address)
		? qsl("http://[%1]:%2/api")
		: qsl("http://%1:%2/api");

	// Not endpoint.port - always 80 port for http transport.
	return QUrl(pattern.arg(_address).arg(kForceHttpPort));
}

} // namespace internal
} // namespace MTP
