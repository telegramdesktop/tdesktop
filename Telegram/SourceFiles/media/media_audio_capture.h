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

struct AVFrame;

namespace Media {
namespace Capture {

void Start();
void Finish();

class Instance : public QObject {
	Q_OBJECT

public:
	Instance();

	void check();
	bool available() const {
		return _available;
	}

	~Instance();

signals:
	void start();
	void stop(bool needResult);

	void done(QByteArray data, VoiceWaveform waveform, qint32 samples);
	void updated(quint16 level, qint32 samples);
	void error();

private:
	class Inner;
	friend class Inner;

	bool _available = false;
	QThread _thread;
	Inner *_inner;

};

Instance *instance();

class Instance::Inner : public QObject {
	Q_OBJECT

public:
	Inner(QThread *thread);
	~Inner();

signals:
	void error();
	void updated(quint16 level, qint32 samples);
	void done(QByteArray data, VoiceWaveform waveform, qint32 samples);

public slots:
	void onInit();
	void onStart();
	void onStop(bool needResult);

	void onTimeout();

private:
	void processFrame(int32 offset, int32 framesize);

	void writeFrame(AVFrame *frame);

	// Writes the packets till EAGAIN is got from av_receive_packet()
	// Returns number of packets written or -1 on error
	int writePackets();

	struct Private;
	Private *d;
	QTimer _timer;
	QByteArray _captured;

};

} // namespace Capture
} // namespace Media

