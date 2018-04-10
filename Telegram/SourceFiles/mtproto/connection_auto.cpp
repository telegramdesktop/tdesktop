/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_auto.h"

#include "mtproto/connection_http.h"

namespace MTP {
namespace internal {

AutoConnection::AutoConnection(QThread *thread) : AbstractTCPConnection(thread)
, status(WaitingBoth)
, tcpNonce(rand_value<MTPint128>())
, httpNonce(rand_value<MTPint128>())
, _flagsTcp(0)
, _flagsHttp(0)
, _tcpTimeout(MTPMinReceiveDelay) {
	manager.moveToThread(thread);
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	manager.setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

	httpStartTimer.moveToThread(thread);
	httpStartTimer.setSingleShot(true);
	connect(&httpStartTimer, SIGNAL(timeout()), this, SLOT(onHttpStart()));

	tcpTimeoutTimer.moveToThread(thread);
	tcpTimeoutTimer.setSingleShot(true);
	connect(&tcpTimeoutTimer, SIGNAL(timeout()), this, SLOT(onTcpTimeoutTimer()));

	sock.moveToThread(thread);
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	sock.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	connect(&sock, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
	connect(&sock, SIGNAL(connected()), this, SLOT(onSocketConnected()));
	connect(&sock, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
}

void AutoConnection::onHttpStart() {
	if (status == HttpReady) {
		DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by timer").arg((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));
		status = UsingHttp;
		sock.disconnectFromHost();
		emit connected();
	}
}

void AutoConnection::onSocketConnected() {
	if (status == HttpReady || status == WaitingBoth || status == WaitingTcp) {
		mtpBuffer buffer(preparePQFake(tcpNonce));

		DEBUG_LOG(("Connection Info: sending fake req_pq through TCP/%1 transport").arg((_flagsTcp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));

		if (_tcpTimeout < 0) _tcpTimeout = -_tcpTimeout;
		tcpTimeoutTimer.start(_tcpTimeout);

		tcpSend(buffer);
	} else if (status == WaitingHttp || status == UsingHttp) {
		sock.disconnectFromHost();
	}
}

void AutoConnection::onTcpTimeoutTimer() {
	if (status == HttpReady || status == WaitingBoth || status == WaitingTcp) {
		if (_tcpTimeout < MTPMaxReceiveDelay) _tcpTimeout *= 2;
		_tcpTimeout = -_tcpTimeout;

		QAbstractSocket::SocketState state = sock.state();
		if (state == QAbstractSocket::ConnectedState || state == QAbstractSocket::ConnectingState || state == QAbstractSocket::HostLookupState) {
			sock.disconnectFromHost();
		} else if (state != QAbstractSocket::ClosingState) {
			sock.connectToHost(QHostAddress(_addrTcp), _portTcp);
		}
	}
}

void AutoConnection::onSocketDisconnected() {
	if (_tcpTimeout < 0) {
		_tcpTimeout = -_tcpTimeout;
		if (status == HttpReady || status == WaitingBoth || status == WaitingTcp) {
			sock.connectToHost(QHostAddress(_addrTcp), _portTcp);
			return;
		}
	}
	if (status == WaitingBoth) {
		status = WaitingHttp;
	} else if (status == WaitingTcp || status == UsingTcp) {
		emit disconnected();
	} else if (status == HttpReady) {
		DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by socket disconnect").arg((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));
		status = UsingHttp;
		emit connected();
	}
}

void AutoConnection::sendData(mtpBuffer &buffer) {
	if (status == FinishedWork) return;

	if (buffer.size() < 3) {
		LOG(("TCP Error: writing bad packet, len = %1").arg(buffer.size() * sizeof(mtpPrime)));
		TCP_LOG(("TCP Error: bad packet %1").arg(Logs::mb(&buffer[0], buffer.size() * sizeof(mtpPrime)).str()));
		emit error(kErrorCodeOther);
		return;
	}

	if (status == UsingTcp) {
		tcpSend(buffer);
	} else {
		httpSend(buffer);
	}
}

void AutoConnection::httpSend(mtpBuffer &buffer) {
	int32 requestSize = (buffer.size() - 3) * sizeof(mtpPrime);

	QNetworkRequest request(address);
	request.setHeader(QNetworkRequest::ContentLengthHeader, QVariant(requestSize));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(qsl("application/x-www-form-urlencoded")));

	TCP_LOG(("HTTP Info: sending %1 len request").arg(requestSize));
	requests.insert(manager.post(request, QByteArray((const char*)(&buffer[2]), requestSize)));
}

void AutoConnection::disconnectFromServer() {
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

void AutoConnection::connectTcp(const DcOptions::Endpoint &endpoint) {
	_addrTcp = QString::fromStdString(endpoint.ip);
	_portTcp = endpoint.port;
	_flagsTcp = endpoint.flags;

	connect(&sock, SIGNAL(readyRead()), this, SLOT(socketRead()));
	sock.connectToHost(QHostAddress(_addrTcp), _portTcp);
}

void AutoConnection::connectHttp(const DcOptions::Endpoint &endpoint) {
	_addrHttp = QString::fromStdString(endpoint.ip);
	_portHttp = endpoint.port;
	_flagsHttp = endpoint.flags;

	// not endpoint.port - always 80 port for http transport
	address = QUrl(((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? qsl("http://[%1]:%2/api") : qsl("http://%1:%2/api")).arg(_addrHttp).arg(80));
	TCP_LOG(("HTTP Info: address is %1").arg(address.toDisplayString()));
	connect(&manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(requestFinished(QNetworkReply*)));

	mtpBuffer buffer(preparePQFake(httpNonce));

	DEBUG_LOG(("Connection Info: sending fake req_pq through HTTP/%1 transport").arg((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));

	httpSend(buffer);
}

bool AutoConnection::isConnected() const {
	return (status == UsingTcp) || (status == UsingHttp);
}

void AutoConnection::requestFinished(QNetworkReply *reply) {
	if (status == FinishedWork) return;

	reply->deleteLater();
	if (reply->error() == QNetworkReply::NoError) {
		requests.remove(reply);

		mtpBuffer data = HTTPConnection::handleResponse(reply);
		if (data.size() == 1) {
			if (status == WaitingBoth) {
				status = WaitingTcp;
			} else {
				emit error(data[0]);
			}
		} else if (!data.isEmpty()) {
			if (status == UsingHttp) {
				_receivedQueue.push_back(data);
				emit receivedData();
			} else if (status == WaitingBoth || status == WaitingHttp) {
				try {
					auto res_pq = readPQFakeReply(data);
					const auto &res_pq_data(res_pq.c_resPQ());
					if (res_pq_data.vnonce == httpNonce) {
						if (status == WaitingBoth) {
							status = HttpReady;
							httpStartTimer.start(MTPTcpConnectionWaitTimeout);
						} else {
							DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by pq-response, awaited").arg((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));
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
						emit error(kErrorCodeOther);
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

		if (status == WaitingBoth) {
			status = WaitingTcp;
		} else if (status == WaitingHttp || status == UsingHttp) {
			emit error(HTTPConnection::handleError(reply));
		} else {
			LOG(("Strange Http Error: status %1").arg(status));
		}
	}
}

void AutoConnection::socketPacket(const char *packet, uint32 length) {
	if (status == FinishedWork) return;

	mtpBuffer data = AbstractTCPConnection::handleResponse(packet, length);
	if (data.size() == 1) {
		if (status == WaitingBoth) {
			status = WaitingHttp;
			sock.disconnectFromHost();
		} else if (status == HttpReady) {
			DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by bad tcp response, ready").arg((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));
			status = UsingHttp;
			sock.disconnectFromHost();
			emit connected();
		} else if (status == WaitingTcp || status == UsingTcp) {
			emit error(data[0]);
		} else {
			LOG(("Strange Tcp Error; status %1").arg(status));
		}
	} else if (status == UsingTcp) {
		_receivedQueue.push_back(data);
		emit receivedData();
	} else if (status == WaitingBoth || status == WaitingTcp || status == HttpReady) {
		tcpTimeoutTimer.stop();
		try {
			auto res_pq = readPQFakeReply(data);
			const auto &res_pq_data(res_pq.c_resPQ());
			if (res_pq_data.vnonce == tcpNonce) {
				DEBUG_LOG(("Connection Info: TCP/%1-transport chosen by pq-response").arg((_flagsTcp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));
				status = UsingTcp;
				emit connected();
			}
		} catch (Exception &e) {
			DEBUG_LOG(("Connection Error: exception in parsing TCP fake pq-responce, %1").arg(e.what()));
			if (status == WaitingBoth) {
				status = WaitingHttp;
				sock.disconnectFromHost();
			} else if (status == HttpReady) {
				DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by bad tcp response, awaited").arg((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));
				status = UsingHttp;
				sock.disconnectFromHost();
				emit connected();
			} else {
				emit error(kErrorCodeOther);
			}
		}
	}
}

bool AutoConnection::usingHttpWait() {
	return (status == UsingHttp);
}

bool AutoConnection::needHttpWait() {
	return (status == UsingHttp) ? requests.isEmpty() : false;
}

int32 AutoConnection::debugState() const {
	return (status == UsingHttp) ? -1 : ((status == UsingTcp) ? sock.state() : -777);
}

QString AutoConnection::transport() const {
	if (status == UsingTcp) {
		return qsl("TCP");
	} else if (status == UsingHttp) {
		return qsl("HTTP");
	} else {
		return QString();
	}
}

void AutoConnection::socketError(QAbstractSocket::SocketError e) {
	if (status == FinishedWork) return;

	AbstractTCPConnection::handleError(e, sock);
	if (status == WaitingBoth) {
		status = WaitingHttp;
	} else if (status == HttpReady) {
		DEBUG_LOG(("Connection Info: HTTP/%1-transport chosen by tcp error, ready").arg((_flagsHttp & MTPDdcOption::Flag::f_ipv6) ? "IPv6" : "IPv4"));
		status = UsingHttp;
		emit connected();
	} else if (status == WaitingTcp || status == UsingTcp) {
		emit error(kErrorCodeOther);
	} else {
		LOG(("Strange Tcp Error: status %1").arg(status));
	}
}

} // namespace internal
} // namespace MTP
