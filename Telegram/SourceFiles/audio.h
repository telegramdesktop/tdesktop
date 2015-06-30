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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "types.h"

void audioInit();
bool audioWorks();
void audioPlayNotify();
void audioFinish();

enum AudioPlayerState {
	AudioPlayerStopped,
	AudioPlayerStoppedAtStart,
	AudioPlayerStarting,
	AudioPlayerPlaying,
	AudioPlayerFinishing,
	AudioPlayerPausing,
	AudioPlayerPaused,
	AudioPlayerResuming,
};

class AudioPlayerFader;
class AudioPlayerLoaders;

class AudioPlayer : public QObject {
	Q_OBJECT

public:

	AudioPlayer();

	void play(const AudioMsgId &audio);
	void play(const SongMsgId &song);
	void pauseresume(MediaOverviewType type);

	void currentState(AudioMsgId *audio, AudioPlayerState *state = 0, int64 *position = 0, int64 *duration = 0, int32 *frequency = 0);
	void currentState(SongMsgId *song, AudioPlayerState *state = 0, int64 *position = 0, int64 *duration = 0, int32 *frequency = 0);

	void clearStoppedAtStart(const AudioMsgId &audio);
	void clearStoppedAtStart(const SongMsgId &song);

	void resumeDevice();

	~AudioPlayer();

public slots:

	void onError(const AudioMsgId &audio);
	void onError(const SongMsgId &song);

	void onStopped(const AudioMsgId &audio);
	void onStopped(const SongMsgId &song);

signals:

	void updated(const AudioMsgId &audio);
	void updated(const SongMsgId &song);

	void stopped(const AudioMsgId &audio);
	void stopped(const SongMsgId &song);

	void loaderOnStart(const AudioMsgId &audio);
	void loaderOnStart(const SongMsgId &song);

	void loaderOnCancel(const AudioMsgId &audio);
	void loaderOnCancel(const SongMsgId &song);

	void faderOnTimer();
	void suppressSong();
	void unsuppressSong();
	void suppressAll();

private:

	bool startedOther(MediaOverviewType type, bool &fadedStart);
	bool updateCurrentStarted(MediaOverviewType type, int32 pos = -1);

	struct Msg {
		Msg() : position(0), duration(0), frequency(AudioVoiceMsgFrequency), skipStart(0), skipEnd(0), loading(0), started(0),
			state(AudioPlayerStopped), source(0), nextBuffer(0) {
			memset(buffers, 0, sizeof(buffers));
			memset(samplesCount, 0, sizeof(samplesCount));
		}

		QString fname;
		QByteArray data;
		int64 position, duration;
		int32 frequency;
		int64 skipStart, skipEnd;
		bool loading;
		int64 started;
		AudioPlayerState state;

		uint32 source;
		int32 nextBuffer;
		uint32 buffers[3];
		int64 samplesCount[3];
	};
	struct AudioMsg : public Msg {
		AudioMsg() {
		}
		AudioMsgId audio;
	};
	struct SongMsg : public Msg {
		SongMsg() {
		}
		SongMsgId song;
	};

	void currentState(Msg *current, AudioPlayerState *state, int64 *position, int64 *duration, int32 *frequency);

	int32 _audioCurrent;
	AudioMsg _audioData[AudioVoiceMsgSimultaneously];

	int32 _songCurrent;
	SongMsg _songData[AudioSongSimultaneously];

	QMutex _mutex;

	friend class AudioPlayerFader;
	friend class AudioPlayerLoaders;

	QThread _faderThread, _loaderThread;
	AudioPlayerFader *_fader;
	AudioPlayerLoaders *_loader;

};

class AudioCaptureInner;

class AudioCapture : public QObject {
	Q_OBJECT

public:

	AudioCapture();

	void start();
	void stop(bool needResult);

	bool check();

	~AudioCapture();

signals:

	void captureOnStart();
	void captureOnStop(bool needResult);

	void onDone(QByteArray data, qint32 samples);
	void onUpdate(qint16 level, qint32 samples);
	void onError();

private:

	friend class AudioCaptureInner;

	QThread _captureThread;
	AudioCaptureInner *_capture;

};

AudioPlayer *audioPlayer();
AudioCapture *audioCapture();

class AudioPlayerFader : public QObject {
	Q_OBJECT

public:

	AudioPlayerFader(QThread *thread);
	void resumeDevice();

signals:

	void error(const AudioMsgId &audio);
	void error(const SongMsgId &audio);
	void playPositionUpdated(const AudioMsgId &audio);
	void playPositionUpdated(const SongMsgId &audio);
	void audioStopped(const AudioMsgId &audio);
	void audioStopped(const SongMsgId &audio);
	void needToPreload(const AudioMsgId &audio);
	void needToPreload(const SongMsgId &audio);

	void stopPauseDevice();

public slots:

	void onInit();
	void onTimer();
	void onPauseTimer();
	void onPauseTimerStop();

	void onSuppressSong();
	void onUnsuppressSong();
	void onSuppressAll();

private:

	enum {
		EmitError           = 0x01,
		EmitStopped         = 0x02,
		EmitPositionUpdated = 0x04,
		EmitNeedToPreload   = 0x08,
	};
	int32 updateOnePlayback(AudioPlayer::Msg *m, bool &hasPlaying, bool &hasFading, float64 suppressGain, bool suppressGainChanged);

	QTimer _timer, _pauseTimer;
	QMutex _pauseMutex;
	bool _pauseFlag, _paused;

	bool _suppressAll, _suppressAllAnim, _suppressSong, _suppressSongAnim;
	anim::fvalue _suppressAllGain, _suppressSongGain;
	uint64 _suppressAllStart, _suppressSongStart;

};

class AudioPlayerLoader;
class AudioPlayerLoaders : public QObject {
	Q_OBJECT

public:

	AudioPlayerLoaders(QThread *thread);
	~AudioPlayerLoaders();

signals:

	void error(const AudioMsgId &audio);
	void error(const SongMsgId &song);
	void needToCheck();

public slots:

	void onInit();

	void onStart(const AudioMsgId &audio);
	void onStart(const SongMsgId &audio);

	void onLoad(const AudioMsgId &audio);
	void onLoad(const SongMsgId &audio);

	void onCancel(const AudioMsgId &audio);
	void onCancel(const SongMsgId &audio);

private:

	AudioMsgId _audio;
	AudioPlayerLoader *_audioLoader;

	SongMsgId _song;
	AudioPlayerLoader *_songLoader;

	void emitError(MediaOverviewType type);
	void clear(MediaOverviewType type);
	AudioMsgId clearAudio();
	SongMsgId clearSong();

	enum SetupError {
		SetupErrorAtStart    = 0,
		SetupErrorNotPlaying = 1,
		SetupErrorLoadedFull = 2,
		SetupNoErrorStarted  = 3,
	};
	void loadData(MediaOverviewType type, const void *objId);
	AudioPlayerLoader *setupLoader(MediaOverviewType type, const void *objId, SetupError &err);
	AudioPlayer::Msg *checkLoader(MediaOverviewType type);

};

struct AudioCapturePrivate;

class AudioCaptureInner : public QObject {
	Q_OBJECT

public:

	AudioCaptureInner(QThread *thread);
	~AudioCaptureInner();

signals:

	void error();
	void update(qint16 level, qint32 samples);
	void done(QByteArray data, qint32 samples);

public slots:

	void onInit();
	void onStart();
	void onStop(bool needResult);

	void onTimeout();

private:

	void writeFrame(int32 offset, int32 framesize);

	AudioCapturePrivate *d;
	QTimer _timer;
	QByteArray _captured;

};

MTPDocumentAttribute audioReadSongAttributes(const QString &fname, const QByteArray &data, QImage &cover, QByteArray &coverBytes, QByteArray &coverFormat);
