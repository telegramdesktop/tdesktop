/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

#include <QtCore/QByteArray>
#include <QtNetwork/QSslSocket>

#include <functional>

namespace Platform::WebAuthn::Cable {

class TunnelSocket final : public QObject {
public:
	explicit TunnelSocket(QObject *parent = nullptr);
	~TunnelSocket();

	void connectToTunnel(const QString &domain, const QString &path);
	void sendBinary(const QByteArray &message);
	void close();

	std::function<void()> onConnected;
	std::function<void(QByteArray)> onBinary;
	std::function<void()> onClosed;

private:
	void onSslConnected();
	void onReadyRead();
	void sendUpgradeRequest();
	[[nodiscard]] bool readUpgradeResponse();
	[[nodiscard]] bool parseFrames();
	void writeFrame(const QByteArray &payload, quint8 opcode);
	void fail();

	enum class State {
		Idle,
		Connecting,
		WaitingUpgrade,
		Connected,
		Closed,
	};

	QSslSocket _socket;
	QString _host;
	QString _path;
	QByteArray _key;
	QByteArray _incoming;
	State _state = State::Idle;
	bool _failed = false;

};

} // namespace Platform::WebAuthn::Cable
