/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QTimer>

struct AVFrame;

namespace Media {
namespace Capture {

struct Update {
	int samples = 0;
	ushort level = 0;
};

struct Result {
	QByteArray bytes;
	VoiceWaveform waveform;
	int samples = 0;
};

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

	[[nodiscard]] rpl::producer<Update, rpl::empty_error> updated() const {
		return _updates.events();
	}

	void start();
	void stop(Fn<void(Result&&)> callback = nullptr);

private:
	class Inner;
	friend class Inner;

	bool _available = false;
	rpl::event_stream<Update, rpl::empty_error> _updates;
	QThread _thread;
	std::unique_ptr<Inner> _inner;

};

[[nodiscard]] Instance *instance();

} // namespace Capture
} // namespace Media

