/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/dc_options.h"
#include "base/bytes.h"

namespace MTP {
namespace internal {

struct ConnectionOptions;

class AbstractConnection;

class ConnectionPointer {
public:
	ConnectionPointer();
	ConnectionPointer(std::nullptr_t);
	explicit ConnectionPointer(AbstractConnection *value);
	ConnectionPointer(ConnectionPointer &&other);
	ConnectionPointer &operator=(ConnectionPointer &&other);

	AbstractConnection *get() const;
	void reset(AbstractConnection *value = nullptr);
	operator AbstractConnection*() const;
	AbstractConnection *operator->() const;
	AbstractConnection &operator*() const;
	explicit operator bool() const;

	~ConnectionPointer();

private:
	AbstractConnection *_value = nullptr;

};

class AbstractConnection : public QObject {
	Q_OBJECT

public:
	AbstractConnection(QThread *thread);
	AbstractConnection(const AbstractConnection &other) = delete;
	AbstractConnection &operator=(const AbstractConnection &other) = delete;
	virtual ~AbstractConnection() = 0;

	// virtual constructor
	static ConnectionPointer create(
		DcOptions::Variants::Protocol protocol,
		QThread *thread);

	void setSentEncrypted() {
		_sentEncrypted = true;
	}

	virtual void setProxyOverride(const ProxyData &proxy) = 0;
	virtual TimeMs pingTime() const = 0;
	virtual void sendData(mtpBuffer &buffer) = 0; // has size + 3, buffer[0] = len, buffer[1] = packetnum, buffer[last] = crc32
	virtual void disconnectFromServer() = 0;
	virtual void connectToServer(
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) = 0;
	virtual bool isConnected() const = 0;
	virtual bool usingHttpWait() {
		return false;
	}
	virtual bool needHttpWait() {
		return false;
	}

	virtual int32 debugState() const = 0;

	virtual QString transport() const = 0;
	virtual QString tag() const = 0;

	using BuffersQueue = std::deque<mtpBuffer>;
	BuffersQueue &received() {
		return _receivedQueue;
	}

	// Used to emit error(...) with no real code from the server.
	static constexpr auto kErrorCodeOther = -499;

signals:
	void receivedData();
	void receivedSome(); // to stop restart timer

	void error(qint32 errorCodebool);

	void connected();
	void disconnected();

protected:
	BuffersQueue _receivedQueue; // list of received packets, not processed yet
	bool _sentEncrypted = false;
	int _pingTime = 0;

	// first we always send fake MTPReq_pq to see if connection works at all
	// we send them simultaneously through TCP/HTTP/IPv4/IPv6 to choose the working one
	static mtpBuffer preparePQFake(const MTPint128 &nonce);
	static MTPResPQ readPQFakeReply(const mtpBuffer &buffer);

};

} // namespace internal
} // namespace MTP
