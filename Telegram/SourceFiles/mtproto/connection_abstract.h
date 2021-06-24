/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtproto_proxy_data.h"
#include "base/bytes.h"

#include <QtCore/QObject>
#include <QtCore/QThread>

namespace MTP {

class Instance;

namespace details {

struct ConnectionOptions;

class AbstractConnection;

class ConnectionPointer {
public:
	ConnectionPointer();
	ConnectionPointer(std::nullptr_t);
	ConnectionPointer(ConnectionPointer &&other);
	ConnectionPointer &operator=(ConnectionPointer &&other);

	template <typename ConnectionType, typename ...Args>
	static ConnectionPointer New(Args &&...args) {
		return ConnectionPointer(new ConnectionType(
			std::forward<Args>(args)...
		));
	}

	AbstractConnection *get() const;
	void reset(AbstractConnection *value = nullptr);
	operator AbstractConnection*() const;
	AbstractConnection *operator->() const;
	AbstractConnection &operator*() const;
	explicit operator bool() const;

	~ConnectionPointer();

private:
	explicit ConnectionPointer(AbstractConnection *value);

	AbstractConnection *_value = nullptr;

};

class AbstractConnection : public QObject {
	Q_OBJECT

public:
	AbstractConnection(QThread *thread, const ProxyData &proxy);
	AbstractConnection(const AbstractConnection &other) = delete;
	AbstractConnection &operator=(const AbstractConnection &other) = delete;
	virtual ~AbstractConnection() = default;

	// virtual constructor
	[[nodiscard]] static ConnectionPointer Create(
		not_null<Instance*> instance,
		DcOptions::Variants::Protocol protocol,
		QThread *thread,
		const bytes::vector &secret,
		const ProxyData &proxy);

	[[nodiscard]] virtual ConnectionPointer clone(const ProxyData &proxy) = 0;

	[[nodiscard]] virtual crl::time pingTime() const = 0;
	[[nodiscard]] virtual crl::time fullConnectTimeout() const = 0;
	virtual void sendData(mtpBuffer &&buffer) = 0;
	virtual void disconnectFromServer() = 0;
	virtual void connectToServer(
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId,
		bool protocolForFiles) = 0;
	virtual void timedOut() {
	}
	[[nodiscard]] virtual bool isConnected() const = 0;
	[[nodiscard]] virtual bool usingHttpWait() {
		return false;
	}
	[[nodiscard]] virtual bool needHttpWait() {
		return false;
	}

	[[nodiscard]] virtual int32 debugState() const = 0;

	[[nodiscard]] virtual QString transport() const = 0;
	[[nodiscard]] virtual QString tag() const = 0;

	void setSentEncryptedWithKeyId(uint64 keyId) {
		_sentEncryptedWithKeyId = keyId;
	}
	[[nodiscard]] uint64 sentEncryptedWithKeyId() const {
		return _sentEncryptedWithKeyId;
	}

	using BuffersQueue = std::deque<mtpBuffer>;
	[[nodiscard]] BuffersQueue &received() {
		return _receivedQueue;
	}

	template <typename Request>
	[[nodiscard]] mtpBuffer prepareNotSecurePacket(
		const Request &request,
		mtpMsgId newId) const;
	[[nodiscard]] mtpBuffer prepareSecurePacket(
		uint64 keyId,
		MTPint128 msgKey,
		uint32 size) const;

	[[nodiscard]] gsl::span<const mtpPrime> parseNotSecureResponse(
		const mtpBuffer &buffer) const;

	// Used to emit error(...) with no real code from the server.
	static constexpr auto kErrorCodeOther = -499;

Q_SIGNALS:
	void receivedData();
	void receivedSome(); // to stop restart timer

	void error(qint32 errorCodebool);

	void connected();
	void disconnected();

	void syncTimeRequest();

protected:
	BuffersQueue _receivedQueue; // list of received packets, not processed yet
	int _pingTime = 0;
	ProxyData _proxy;

	// first we always send fake MTPReq_pq to see if connection works at all
	// we send them simultaneously through TCP/HTTP/IPv4/IPv6 to choose the working one
	[[nodiscard]] mtpBuffer preparePQFake(const MTPint128 &nonce) const;
	[[nodiscard]] std::optional<MTPResPQ> readPQFakeReply(
		const mtpBuffer &buffer) const;

private:
	[[nodiscard]] uint32 extendedNotSecurePadding() const;

	uint64 _sentEncryptedWithKeyId = 0;

};

template <typename Request>
mtpBuffer AbstractConnection::prepareNotSecurePacket(
		const Request &request,
		mtpMsgId newId) const {
	const auto intsSize = tl::count_length(request) >> 2;
	const auto intsPadding = extendedNotSecurePadding();

	auto result = mtpBuffer();
	constexpr auto kTcpPrefixInts = 2;
	constexpr auto kAuthKeyIdInts = 2;
	constexpr auto kMessageIdInts = 2;
	constexpr auto kMessageLengthInts = 1;
	constexpr auto kPrefixInts = kTcpPrefixInts
		+ kAuthKeyIdInts
		+ kMessageIdInts
		+ kMessageLengthInts;
	constexpr auto kTcpPostfixInts = 4;

	result.reserve(kPrefixInts + intsSize + intsPadding + kTcpPostfixInts);
	result.resize(kPrefixInts);

	const auto messageId = &result[kTcpPrefixInts + kAuthKeyIdInts];
	*reinterpret_cast<mtpMsgId*>(messageId) = newId;

	request.write(result);

	const auto messageLength = messageId + kMessageIdInts;
	*messageLength = (result.size() - kPrefixInts + intsPadding) << 2;

	if (intsPadding > 0) {
		const auto skipPrimes = result.size();
		result.resize(skipPrimes + intsPadding);
		const auto skipBytes = skipPrimes * sizeof(mtpPrime);
		bytes::set_random(bytes::make_span(result).subspan(skipBytes));
	}

	return result;
}

} // namespace details
} // namespace MTP
