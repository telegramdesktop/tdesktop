/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <crl/crl_time.h>

#include <deque>
#include <memory>
#include <string>

class QTimer;

namespace Webview {
class Window;
} // namespace Webview

namespace MTP::WebProxy {

class WebviewCarrier final : public QObject {
public:
	struct Callbacks {
		Fn<void(uint64)> ready;
		Fn<void(uint64, QByteArray)> payload;
		Fn<void(uint64, int)> written;
		Fn<void(uint64)> failed;
	};

	WebviewCarrier(
		const ProxyData &proxy,
		uint64 generation,
		Callbacks callbacks);
	~WebviewCarrier();

	[[nodiscard]] static bool Supported();
	[[nodiscard]] bool valid() const;
	void send(QByteArray frame);

	// Asks the bridge page to close its relay session and stops reacting
	// to the page. The object should be kept alive shortly afterwards so
	// that the asynchronous close script gets a chance to run.
	void close();

private:
	struct Pending {
		QByteArray frame;
		bool notifyWritten = false;
	};

	void handleMessage(std::string message, std::string sourceUrl);
	void handleControl(const QByteArray &control);
	void handleBinary(QByteArray frame);
	[[nodiscard]] bool validNavigation(const QString &url) const;
	[[nodiscard]] bool validSource(const std::string &sourceUrl) const;
	void extendHandshake();
	void probeRestrictions();
	void enqueue(Pending pending);
	void drain();
	void heartbeat();
	void fail(const char *reason);

	const ProxyData _proxy;
	const uint64 _generation = 0;
	const QString _nonce;
	const QString _url;
	Callbacks _callbacks;
	std::unique_ptr<Webview::Window> _window;
	std::unique_ptr<QTimer> _handshakeTimer;
	std::unique_ptr<QTimer> _healthTimer;
	std::unique_ptr<QTimer> _probeTimer;
	std::unique_ptr<QTimer> _writeTimer;
	std::deque<Pending> _pending;
	Pending _inFlight;
	uint64 _writeSequence = 0;
	crl::time _handshakeStarted = 0;
	int _pendingBytes = 0;
	bool _bridgeInitialized = false;
	bool _adopted = false;
	bool _failed = false;
	bool _closing = false;
	bool _probed = false;
};

} // namespace MTP::WebProxy
