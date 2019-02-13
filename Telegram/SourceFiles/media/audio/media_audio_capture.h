/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

