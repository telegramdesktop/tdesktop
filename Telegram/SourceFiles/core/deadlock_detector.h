/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core::DeadlockDetector {

class PingPongEvent : public QEvent {
public:
	static auto Type() {
		static const auto Result = QEvent::Type(QEvent::registerEventType());
		return Result;
	}

	PingPongEvent(not_null<QObject*> sender)
	: QEvent(Type())
	, _sender(sender) {
	}

	[[nodiscard]] not_null<QObject*> sender() const {
		return _sender;
	}

private:
	not_null<QObject*> _sender;

};

class Pinger : public QObject {
public:
	Pinger(not_null<QObject*> receiver)
	: _receiver(receiver)
	, _abortTimer([] { Unexpected("Deadlock found!"); }) {
		const auto callback = [=] {
			QCoreApplication::postEvent(_receiver, new PingPongEvent(this));
			_abortTimer.callOnce(30000);
		};
		_pingTimer.setCallback(callback);
		_pingTimer.callEach(60000);
		callback();
	}

protected:
	bool event(QEvent *e) override {
		if (e->type() == PingPongEvent::Type()
			&& static_cast<PingPongEvent*>(e)->sender() == _receiver) {
			_abortTimer.cancel();
		}
		return QObject::event(e);
	}

private:
	not_null<QObject*> _receiver;
	base::Timer _pingTimer;
	base::Timer _abortTimer;

};

class PingThread : public QThread {
public:
	PingThread(not_null<QObject*> parent)
	: QThread(parent) {
		start();
	}

	~PingThread() {
		quit();
		wait();
	}

protected:
	void run() override {
		Pinger pinger(parent());
		QThread::run();
	}

};

} // namespace Core::DeadlockDetector
