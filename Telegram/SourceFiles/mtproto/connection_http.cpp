/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_http.h"

#include "base/random.h"
#include "base/qthelp_url.h"

namespace MTP {
namespace details {
namespace {

constexpr auto kForceHttpPort = 80;
constexpr auto kFullConnectionTimeout = crl::time(8000);

} // namespace

HttpConnection::HttpConnection(QThread *thread, const ProxyData &proxy)
: AbstractConnection(thread, proxy)
, _checkNonce(base::RandomValue<MTPint128>()) {
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
	request.setHeader(
		QNetworkRequest::ContentLengthHeader,
		QVariant(requestSize));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		QVariant(u"application/x-www-form-urlencoded"_q));

	CONNECTION_LOG_INFO(u"Sending %1 len request."_q.arg(requestSize));
	_requests.insert(_manager.post(request, QByteArray((const char*)(&buffer[2]), requestSize)));
}

void HttpConnection::disconnectFromServer() {
	if (_status == Status::Finished) return;
	_status = Status::Finished;

	const auto requests = base::take(_requests);
	for (const auto request : requests) {
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
		int16 protocolDcId,
		bool protocolForFiles) {
	_address = address;
	connect(
		&_manager,
		&QNetworkAccessManager::finished,
		this,
		&HttpConnection::requestFinished);

	auto buffer = preparePQFake(_checkNonce);

	if (Logs::DebugEnabled()) {
		_debugId = u"%1(dc:%2,%3)"_q
			.arg(_debugId.toInt())
			.arg(ProtocolDcDebugId(protocolDcId), url().toDisplayString());
	}

	_pingTime = crl::now();
	sendData(std::move(buffer));
}

mtpBuffer HttpConnection::handleResponse(QNetworkReply *reply) {
	QByteArray response = reply->readAll();
	CONNECTION_LOG_INFO(u"Read %1 bytes."_q.arg(response.size()));

	if (response.isEmpty()) return mtpBuffer();

	if (response.size() & 0x03 || response.size() < 8) {
		CONNECTION_LOG_ERROR(u"Bad response size %1."_q.arg(response.size()));
		return mtpBuffer(1, -500);
	}

	mtpBuffer data(response.size() >> 2);
	memcpy(data.data(), response.constData(), response.size());

	return data;
}

// Returns "maybe bad key".
qint32 HttpConnection::handleError(QNetworkReply *reply) {
	auto result = qint32(kErrorCodeOther);

	QVariant statusCode = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		result = -status;
	}

	switch (reply->error()) {
	case QNetworkReply::ConnectionRefusedError:
		CONNECTION_LOG_ERROR(u"Connection refused - %1."_q
			.arg(reply->errorString()));
		break;
	case QNetworkReply::RemoteHostClosedError:
		CONNECTION_LOG_ERROR(u"Remote host closed - %1."_q
			.arg(reply->errorString()));
		break;
	case QNetworkReply::HostNotFoundError:
		CONNECTION_LOG_ERROR(u"Host not found - %1."_q
			.arg(reply->errorString()));
		break;
	case QNetworkReply::TimeoutError:
		CONNECTION_LOG_ERROR(u"Timeout - %1."_q
			.arg(reply->errorString()));
		break;
	case QNetworkReply::OperationCanceledError:
		CONNECTION_LOG_ERROR(u"Cancelled - %1."_q
			.arg(reply->errorString()));
		break;
	case QNetworkReply::SslHandshakeFailedError:
	case QNetworkReply::TemporaryNetworkFailureError:
	case QNetworkReply::NetworkSessionFailedError:
	case QNetworkReply::BackgroundRequestNotAllowedError:
	case QNetworkReply::UnknownNetworkError:
		CONNECTION_LOG_ERROR(u"Network error %1 - %2."_q
			.arg(reply->error())
			.arg(reply->errorString()));
		break;

	// proxy errors (101-199):
	case QNetworkReply::ProxyConnectionRefusedError:
	case QNetworkReply::ProxyConnectionClosedError:
	case QNetworkReply::ProxyNotFoundError:
	case QNetworkReply::ProxyTimeoutError:
	case QNetworkReply::ProxyAuthenticationRequiredError:
	case QNetworkReply::UnknownProxyError:
		CONNECTION_LOG_ERROR(u"Proxy error %1 - %2."_q
			.arg(reply->error())
			.arg(reply->errorString()));
		break;

	// content errors (201-299):
	case QNetworkReply::ContentAccessDenied:
	case QNetworkReply::ContentOperationNotPermittedError:
	case QNetworkReply::ContentNotFoundError:
	case QNetworkReply::AuthenticationRequiredError:
	case QNetworkReply::ContentReSendError:
	case QNetworkReply::UnknownContentError:
		CONNECTION_LOG_ERROR(u"Content error %1 - %2."_q
			.arg(reply->error())
			.arg(reply->errorString()));
		break;

	// protocol errors
	case QNetworkReply::ProtocolUnknownError:
	case QNetworkReply::ProtocolInvalidOperationError:
	case QNetworkReply::ProtocolFailure:
		CONNECTION_LOG_ERROR(u"Protocol error %1 - %2."_q
			.arg(reply->error())
			.arg(reply->errorString()));
		break;
	};

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
					CONNECTION_LOG_INFO(
						"HTTP-transport connected by pq-response.");
					_status = Status::Ready;
					_pingTime = crl::now() - _pingTime;
					connected();
				} else {
					CONNECTION_LOG_ERROR(
						"Wrong nonce in HTTP fake pq-response.");
					error(kErrorCodeOther);
				}
			} else {
				CONNECTION_LOG_ERROR(
					"Could not parse HTTP fake pq-response.");
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
	auto result = u"HTTP"_q;
	if (qthelp::is_ipv6(_address)) {
		result += u"/IPv6"_q;
	}
	return result;
}

QString HttpConnection::tag() const {
	auto result = u"HTTP"_q;
	if (qthelp::is_ipv6(_address)) {
		result += u"/IPv6"_q;
	} else {
		result += u"/IPv4"_q;
	}
	return result;
}

QUrl HttpConnection::url() const {
	const auto pattern = qthelp::is_ipv6(_address)
		? u"http://[%1]:%2/api"_q
		: u"http://%1:%2/api"_q;

	// Not endpoint.port - always 80 port for http transport.
	return QUrl(pattern.arg(_address).arg(kForceHttpPort));
}

} // namespace details
} // namespace MTP
