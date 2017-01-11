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

#include "core/basic_types.h"

void audioInit();
bool audioWorks();
void audioPlayNotify();
void audioFinish();

enum AudioPlayerState {
	AudioPlayerStopped = 0x01,
	AudioPlayerStoppedAtEnd = 0x02,
	AudioPlayerStoppedAtError = 0x03,
	AudioPlayerStoppedAtStart = 0x04,
	AudioPlayerStoppedMask = 0x07,

	AudioPlayerStarting = 0x08,
	AudioPlayerPlaying = 0x10,
	AudioPlayerFinishing = 0x18,
	AudioPlayerPausing = 0x20,
	AudioPlayerPaused = 0x28,
	AudioPlayerPausedAtEnd = 0x30,
	AudioPlayerResuming = 0x38,
};

class AudioPlayerFader;
class AudioPlayerLoaders;

struct VideoSoundData;
struct VideoSoundPart;
struct AudioPlaybackState {
	AudioPlayerState state = AudioPlayerStopped;
	int64 position = 0;
	TimeMs duration = 0;
	int32 frequency = 0;
};

class AudioPlayer : public QObject, public base::Observable<AudioMsgId>, private base::Subscriber {
	Q_OBJECT

public:
	AudioPlayer();

	void play(const AudioMsgId &audio, int64 position = 0);
	void pauseresume(AudioMsgId::Type type, bool fast = false);
	void seek(int64 position); // type == AudioMsgId::Type::Song
	void stop(AudioMsgId::Type type);

	// Video player audio stream interface.
	void initFromVideo(uint64 videoPlayId, std_::unique_ptr<VideoSoundData> &&data, int64 position);
	void feedFromVideo(VideoSoundPart &&part);
	int64 getVideoCorrectedTime(uint64 playId, TimeMs frameMs, TimeMs systemMs);
	AudioPlaybackState currentVideoState(uint64 videoPlayId);
	void stopFromVideo(uint64 videoPlayId);
	void pauseFromVideo(uint64 videoPlayId);
	void resumeFromVideo(uint64 videoPlayId);

	void stopAndClear();

	AudioPlaybackState currentState(AudioMsgId *audio, AudioMsgId::Type type);

	void clearStoppedAtStart(const AudioMsgId &audio);

	void resumeDevice();

	~AudioPlayer();

private slots:
	void onError(const AudioMsgId &audio);
	void onStopped(const AudioMsgId &audio);

	void onUpdated(const AudioMsgId &audio);

signals:
	void updated(const AudioMsgId &audio);
	void stoppedOnError(const AudioMsgId &audio);
	void loaderOnStart(const AudioMsgId &audio, qint64 position);
	void loaderOnCancel(const AudioMsgId &audio);

	void faderOnTimer();

	void suppressSong();
	void unsuppressSong();
	void suppressAll();

private:
	bool fadedStop(AudioMsgId::Type type, bool *fadedStart = 0);
	bool updateCurrentStarted(AudioMsgId::Type type, int32 pos = -1);
	bool checkCurrentALError(AudioMsgId::Type type);

	void videoSoundProgress(const AudioMsgId &audio);

	struct AudioMsg {
		void clear();

		AudioMsgId audio;

		FileLocation file;
		QByteArray data;
		AudioPlaybackState playbackState = defaultState();
		int64 skipStart = 0;
		int64 skipEnd = 0;
		bool loading = false;
		int64 started = 0;

		uint32 source = 0;
		int32 nextBuffer = 0;
		uint32 buffers[3] = { 0 };
		int64 samplesCount[3] = { 0 };

		uint64 videoPlayId = 0;
		std_::unique_ptr<VideoSoundData> videoData;

	private:
		static AudioPlaybackState defaultState() {
			AudioPlaybackState result;
			result.frequency = AudioVoiceMsgFrequency;
			return result;
		}

	};

	void setStoppedState(AudioMsg *current, AudioPlayerState state = AudioPlayerStopped);

	AudioMsg *dataForType(AudioMsgId::Type type, int index = -1); // -1 uses currentIndex(type)
	const AudioMsg *dataForType(AudioMsgId::Type type, int index = -1) const;
	int *currentIndex(AudioMsgId::Type type);
	const int *currentIndex(AudioMsgId::Type type) const;

	int _audioCurrent = 0;
	AudioMsg _audioData[AudioSimultaneousLimit];

	int _songCurrent = 0;
	AudioMsg _songData[AudioSimultaneousLimit];

	AudioMsg _videoData;
	uint64 _lastVideoPlayId = 0;
	TimeMs _lastVideoPlaybackWhen = 0;
	TimeMs _lastVideoPlaybackCorrectedMs = 0;
	QMutex _lastVideoMutex;

	QMutex _mutex;

	friend class AudioPlayerFader;
	friend class AudioPlayerLoaders;

	QThread _faderThread, _loaderThread;
	AudioPlayerFader *_fader;
	AudioPlayerLoaders *_loader;

};

namespace internal {

QMutex *audioPlayerMutex();
float64 audioSuppressGain();
float64 audioSuppressSongGain();
bool audioCheckError();

} // namespace internal

class AudioCaptureInner;

class AudioCapture : public QObject {
	Q_OBJECT

public:
	AudioCapture();

	bool check();

	~AudioCapture();

signals:
	void start();
	void stop(bool needResult);

	void done(QByteArray data, VoiceWaveform waveform, qint32 samples);
	void updated(quint16 level, qint32 samples);
	void error();

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
	void playPositionUpdated(const AudioMsgId &audio);
	void audioStopped(const AudioMsgId &audio);
	void needToPreload(const AudioMsgId &audio);

	void stopPauseDevice();

public slots:
	void onInit();
	void onTimer();
	void onPauseTimer();
	void onPauseTimerStop();

	void onSuppressSong();
	void onUnsuppressSong();
	void onSuppressAll();
	void onSongVolumeChanged();
	void onVideoVolumeChanged();

private:
	enum {
		EmitError = 0x01,
		EmitStopped = 0x02,
		EmitPositionUpdated = 0x04,
		EmitNeedToPreload = 0x08,
	};
	int32 updateOnePlayback(AudioPlayer::AudioMsg *m, bool &hasPlaying, bool &hasFading, float64 suppressGain, bool suppressGainChanged);
	void setStoppedState(AudioPlayer::AudioMsg *m, AudioPlayerState state = AudioPlayerStopped);

	QTimer _timer, _pauseTimer;
	QMutex _pauseMutex;
	bool _pauseFlag = false;
	bool _paused = true;

	bool _suppressAll = false;
	bool _suppressAllAnim = false;
	bool _suppressSong = false;
	bool _suppressSongAnim = false;
	bool _songVolumeChanged, _videoVolumeChanged;
	anim::value _suppressAllGain, _suppressSongGain;
	TimeMs _suppressAllStart = 0;
	TimeMs _suppressSongStart = 0;

};

struct AudioCapturePrivate;
struct AVFrame;

class AudioCaptureInner : public QObject {
	Q_OBJECT

public:
	AudioCaptureInner(QThread *thread);
	~AudioCaptureInner();

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

	AudioCapturePrivate *d;
	QTimer _timer;
	QByteArray _captured;

};

MTPDocumentAttribute audioReadSongAttributes(const QString &fname, const QByteArray &data, QImage &cover, QByteArray &coverBytes, QByteArray &coverFormat);
VoiceWaveform audioCountWaveform(const FileLocation &file, const QByteArray &data);
