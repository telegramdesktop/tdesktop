/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_stream.h"

#include "base/unixtime.h"
#include "core/websocket_client.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_api.h"

namespace Wallet {
namespace {

constexpr auto kCoalesceDelay = crl::time(250);
constexpr auto kHistoryRecheckDelay = crl::time(3000);
constexpr auto kHistoryLastRecheckDelay = crl::time(10000);
constexpr auto kKeepaliveInterval = 10 * crl::time(1000);
constexpr auto kStallTimeout = 30 * crl::time(1000);
constexpr auto kStableTimeout = 60 * crl::time(1000);
constexpr auto kRenewMargin = crl::time(60000);
constexpr auto kMaxRenewDelay = 30 * 60 * crl::time(1000);
constexpr auto kRetryDelays = std::array{
	crl::time(500),
	crl::time(1000),
	crl::time(2000),
	crl::time(4000),
	crl::time(8000),
	crl::time(15000),
	crl::time(30000),
};
constexpr auto kAbsoluteExpiresThreshold = TimeId(1'000'000'000);

} // namespace

Stream::Stream(not_null<Api*> api, Fn<void(StreamRefresh)> refresh)
: _api(api)
, _refresh(std::move(refresh))
, _retryTimer([=] { acquire(); })
, _renewTimer([=] { acquire(); })
, _coalesceTimer([=] { flush(); })
, _keepaliveTimer([=] { keepaliveTick(); })
, _historyRecheckTimer([=] { recheckHistory(); }) {
}

Stream::~Stream() {
	stop();
}

void Stream::start(const QString &address) {
	if (_started) {
		if (_address == address) {
			return;
		}
		stop();
	}
	_address = address;
	_addressFriendly = FormatFriendly(address, false);
	_started = true;
	_retryAttempt = 0;
	acquire();
}

void Stream::stop() {
	_started = false;
	++_generation;
	_api->cancelRequest(base::take(_acquireId));
	_retryTimer.cancel();
	_renewTimer.cancel();
	_coalesceTimer.cancel();
	_keepaliveTimer.cancel();
	_historyRecheckTimer.cancel();
	detachSocket();
	_socket = nullptr;
	_state = State::Idle;
	_liveSince = 0;
	_wanted = StreamRefresh();
}

bool Stream::healthy() const {
	return (_state == State::Live);
}

void Stream::acquire() {
	if (!_started) {
		return;
	}
	_api->cancelRequest(base::take(_acquireId));
	_state = State::Acquiring;
	const auto generation = _generation;
	_acquireId = _api->requestStreamingUrl([=](
			const QString &url,
			TimeId expires) {
		if (generation != _generation) {
			return;
		}
		_acquireId = 0;
		applyUrl(url, expires);
	}, [=](const Gram::ApiError &error) {
		if (generation != _generation) {
			return;
		}
		_acquireId = 0;
		failed();
	});
}

void Stream::applyUrl(const QString &url, TimeId expires) {
	const auto endpoint = Gram::ParseStreamEndpoint(url);
	if (!endpoint) {
		failed();
		return;
	}
	scheduleRenew(expires);
	openSocket(*endpoint);
}

void Stream::openSocket(const Gram::StreamEndpoint &endpoint) {
	++_generation;
	_socket = nullptr;
	_socket = std::make_unique<Core::WebSocketClient>();

	const auto raw = _socket.get();
	const auto generation = _generation;
	raw->onConnected = [=] {
		if (generation != _generation) {
			return;
		}
		handleUpgraded();
	};
	raw->onText = [=](QByteArray frame) {
		if (generation != _generation) {
			return;
		}
		handleFrame(frame);
	};
	raw->onClosed = [=] {
		if (generation != _generation) {
			return;
		}
		handleClosed();
	};
	raw->onActivity = [=] {
		if (generation != _generation) {
			return;
		}
		_lastFrameAt = crl::now();
	};
	_state = State::Connecting;
	_lastFrameAt = crl::now();
	_keepaliveTimer.callEach(kKeepaliveInterval);
	DEBUG_LOG(("Wallet: streaming from %1."
		).arg(Gram::StreamEndpointLabel(endpoint)));
	raw->connectTo(
		endpoint.host,
		endpoint.port,
		endpoint.requestTarget,
		QString());
}

void Stream::handleUpgraded() {
	const auto now = crl::now();
	_state = State::Live;
	_liveSince = now;
	_lastFrameAt = now;
	if (const auto socket = _socket.get()) {
		socket->sendText(
			Gram::StreamSubscribeMessage(_addressFriendly, ++_messageId));
	}
	_keepaliveTimer.callEach(kKeepaliveInterval);
}

void Stream::handleFrame(const QByteArray &frame) {
	const auto event = Gram::ParseStreamEvent(frame);
	switch (event.kind) {
	case Gram::StreamEventKind::AccountState:
		if (mine(event.accounts)) {
			want({ .state = true });
		}
		break;
	case Gram::StreamEventKind::Transactions:
		if (mine(event.accounts)) {
			want({ .state = true, .history = true, .collectibles = true });
			_historyRecheckedOnce = false;
			_historyRecheckTimer.callOnce(kHistoryRecheckDelay);
		}
		break;
	case Gram::StreamEventKind::TraceInvalidated:
		want({ .state = true, .history = true });
		break;
	case Gram::StreamEventKind::Unknown:
		break;
	}
}

void Stream::handleClosed() {
	failed();
}

void Stream::keepaliveTick() {
	if (crl::now() - _lastFrameAt > kStallTimeout) {
		failed();
	} else if (const auto socket = _socket.get()) {
		socket->sendText(Gram::StreamPingMessage(++_messageId));
	}
}

void Stream::detachSocket() {
	const auto socket = _socket.get();
	if (!socket) {
		return;
	}
	socket->onConnected = nullptr;
	socket->onText = nullptr;
	socket->onBinary = nullptr;
	socket->onClosed = nullptr;
	socket->onActivity = nullptr;
	socket->close();
}

void Stream::failed() {
	++_generation;
	const auto liveSince = base::take(_liveSince);
	if (liveSince && (crl::now() - liveSince > kStableTimeout)) {
		_retryAttempt = 0;
	}
	detachSocket();
	_state = State::Backoff;
	_keepaliveTimer.cancel();
	_renewTimer.cancel();
	_historyRecheckTimer.cancel();
	_api->cancelRequest(base::take(_acquireId));
	scheduleRetry();
}

void Stream::scheduleRetry() {
	if (!_started) {
		return;
	}
	const auto index = std::min(_retryAttempt, int(kRetryDelays.size()) - 1);
	++_retryAttempt;
	_retryTimer.callOnce(kRetryDelays[index]);
}

void Stream::scheduleRenew(TimeId expires) {
	const auto seconds = (expires > kAbsoluteExpiresThreshold)
		? (expires - base::unixtime::now())
		: expires;
	if (seconds <= 0) {
		return;
	}
	const auto lifetime = seconds * crl::time(1000);
	const auto minimal = std::min(lifetime / 2, kMaxRenewDelay);
	_renewTimer.callOnce(
		std::clamp(lifetime - kRenewMargin, minimal, kMaxRenewDelay));
}

void Stream::want(StreamRefresh wanted) {
	_wanted.state = _wanted.state || wanted.state;
	_wanted.history = _wanted.history || wanted.history;
	_wanted.collectibles = _wanted.collectibles || wanted.collectibles;
	if (!_coalesceTimer.isActive()) {
		_coalesceTimer.callOnce(kCoalesceDelay);
	}
}

void Stream::flush() {
	const auto wanted = base::take(_wanted);
	if (_refresh) {
		_refresh(wanted);
	}
}

void Stream::recheckHistory() {
	// WHY: a frame can arrive before wallet.getTransactions lists its
	// transfer and may be the last one the provider sends, so the head is
	// asked again shortly after the last frame naming this wallet.
	if (!_historyRecheckedOnce) {
		_historyRecheckedOnce = true;
		_historyRecheckTimer.callOnce(
			kHistoryLastRecheckDelay - kHistoryRecheckDelay);
	}
	if (_refresh) {
		_refresh({ .history = true });
	}
}

bool Stream::mine(const std::vector<QString> &accounts) const {
	return ranges::any_of(accounts, [&](const QString &account) {
		const auto canonical = CanonicalAddress(account);
		return !canonical.isEmpty() && (canonical == _address);
	});
}


} // namespace Wallet
