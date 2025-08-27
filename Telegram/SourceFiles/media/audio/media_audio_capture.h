/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QThread>
#include <QtCore/QTimer>

namespace Media {
namespace Capture {

struct Update {
	int samples = 0;
	ushort level = 0;

	bool finished = false;
};

enum class Error : uchar {
	Other,
	AudioInit,
	VideoInit,
	AudioTimeout,
	VideoTimeout,
	Encoding,
};

struct Chunk {
	crl::time finished = 0;
	QByteArray samples;
	int frequency = 0;
};

struct Result;

void Start();
void Finish();

class Instance final : public QObject {
public:
	Instance();
	~Instance();

	void check();
	[[nodiscard]] bool available() const {
		return _available;
	}

	[[nodiscard]] rpl::producer<Update, Error> updated() const {
		return _updates.events();
	}

	[[nodiscard]] bool started() const {
		return _started.current();
	}
	[[nodiscard]] rpl::producer<bool> startedChanges() const {
		return _started.changes();
	}

	void start(Fn<void(Chunk)> externalProcessing = nullptr);
	void stop(Fn<void(Result&&)> callback = nullptr);
	void pause(bool value, Fn<void(Result&&)> callback = nullptr);

private:
	class Inner;
	friend class Inner;

	bool _available = false;
	rpl::variable<bool> _started = false;
	rpl::event_stream<Update, Error> _updates;
	QThread _thread;
	std::unique_ptr<Inner> _inner;

};

[[nodiscard]] Instance *instance();

} // namespace Capture
} // namespace Media

