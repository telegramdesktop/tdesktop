/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "gram/api/gram_api_stream.h"

namespace Core {
class WebSocketClient;
} // namespace Core

namespace Wallet {

class Api;

struct StreamRefresh {
	bool state = false;
	bool history = false;
	bool collectibles = false;
};

class Stream final {
public:
	Stream(not_null<Api*> api, Fn<void(StreamRefresh)> refresh);
	~Stream();

	void start(const QString &address);
	void stop();

	[[nodiscard]] bool healthy() const;


private:
	enum class State {
		Idle,
		Acquiring,
		Connecting,
		Live,
		Backoff,
	};

	void acquire();
	void applyUrl(const QString &url, TimeId expires);
	void openSocket(const Gram::StreamEndpoint &endpoint);
	void handleUpgraded();
	void handleFrame(const QByteArray &frame);
	void handleClosed();
	void keepaliveTick();
	void detachSocket();
	void failed();
	void scheduleRetry();
	void scheduleRenew(TimeId expires);
	void want(StreamRefresh wanted);
	void flush();
	void recheckHistory();
	[[nodiscard]] bool mine(const std::vector<QString> &accounts) const;

	const not_null<Api*> _api;
	const Fn<void(StreamRefresh)> _refresh;
	std::unique_ptr<Core::WebSocketClient> _socket;
	base::Timer _retryTimer;
	base::Timer _renewTimer;
	base::Timer _coalesceTimer;
	base::Timer _keepaliveTimer;
	base::Timer _historyRecheckTimer;
	QString _address;
	QString _addressFriendly;
	StreamRefresh _wanted;
	crl::time _lastFrameAt = 0;
	crl::time _liveSince = 0;
	mtpRequestId _acquireId = 0;
	State _state = State::Idle;
	int _generation = 0;
	int _retryAttempt = 0;
	quint32 _messageId = 0;
	bool _started = false;
	bool _historyRecheckedOnce = false;

};

} // namespace Wallet
