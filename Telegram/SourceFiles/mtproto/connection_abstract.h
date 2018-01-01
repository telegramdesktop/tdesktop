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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "mtproto/dc_options.h"

namespace MTP {
namespace internal {

class AbstractConnection : public QObject {
	Q_OBJECT

public:
	AbstractConnection(QThread *thread) : _sentEncrypted(false) {
		moveToThread(thread);
	}
	AbstractConnection(const AbstractConnection &other) = delete;
	AbstractConnection &operator=(const AbstractConnection &other) = delete;
	virtual ~AbstractConnection() = 0;

	// virtual constructor
	static AbstractConnection *create(DcType type, QThread *thread);

	void setSentEncrypted() {
		_sentEncrypted = true;
	}

	virtual void sendData(mtpBuffer &buffer) = 0; // has size + 3, buffer[0] = len, buffer[1] = packetnum, buffer[last] = crc32
	virtual void disconnectFromServer() = 0;
	virtual void connectTcp(const DcOptions::Endpoint &endpoint) = 0;
	virtual void connectHttp(const DcOptions::Endpoint &endpoint) = 0;
	virtual bool isConnected() const = 0;
	virtual bool usingHttpWait() {
		return false;
	}
	virtual bool needHttpWait() {
		return false;
	}

	virtual int32 debugState() const = 0;

	virtual QString transport() const = 0;

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
	bool _sentEncrypted;

	// first we always send fake MTPReq_pq to see if connection works at all
	// we send them simultaneously through TCP/HTTP/IPv4/IPv6 to choose the working one
	static mtpBuffer preparePQFake(const MTPint128 &nonce);
	static MTPResPQ readPQFakeReply(const mtpBuffer &buffer);

};

} // namespace internal
} // namespace MTP
