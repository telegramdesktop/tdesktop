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

namespace Core {

class WebSocketClient final : public QObject {
public:
	explicit WebSocketClient(QObject *parent = nullptr);
	~WebSocketClient();

	void connectTo(
		const QString &host,
		int port,
		const QString &requestTarget,
		const QString &subprotocol);
	void sendText(const QByteArray &message);
	void sendBinary(const QByteArray &message);
	void close();

	std::function<void()> onConnected;
	std::function<void(QByteArray)> onText;
	std::function<void(QByteArray)> onBinary;
	std::function<void()> onClosed;
	std::function<void()> onActivity;

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
	QString _target;
	QString _subprotocol;
	QByteArray _key;
	QByteArray _incoming;
	State _state = State::Idle;
	int _port = 0;
	bool _failed = false;

};

} // namespace Core
