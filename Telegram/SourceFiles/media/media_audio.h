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

#include "storage/localimageloader.h"

struct VideoSoundData;
struct VideoSoundPart;

namespace Media {
namespace Player {

constexpr auto kDefaultFrequency = 48000; // 48 kHz
constexpr auto kTogetherLimit = 4;
constexpr auto kWaveformSamplesCount = 100;

class Fader;
class Loaders;

void InitAudio();
void DeInitAudio();

base::Observable<AudioMsgId> &Updated();
void DetachFromDeviceByTimer();

void PlayNotify();

float64 ComputeVolume(AudioMsgId::Type type);

enum class State {
	Stopped = 0x01,
	StoppedAtEnd = 0x02,
	StoppedAtError = 0x03,
	StoppedAtStart = 0x04,

	Starting = 0x08,
	Playing = 0x10,
	Finishing = 0x18,
	Pausing = 0x20,
	Paused = 0x28,
	PausedAtEnd = 0x30,
	Resuming = 0x38,
};

inline bool IsStopped(State state) {
	return (state == State::Stopped)
		|| (state == State::StoppedAtEnd)
		|| (state == State::StoppedAtError)
		|| (state == State::StoppedAtStart);
}

inline bool IsPaused(State state) {
	return (state == State::Paused)
		|| (state == State::PausedAtEnd);
}

inline bool IsFading(State state) {
	return (state == State::Starting)
		|| (state == State::Finishing)
		|| (state == State::Pausing)
		|| (state == State::Resuming);
}

inline bool IsActive(State state) {
	return !IsStopped(state) && !IsPaused(state);
}

struct TrackState {
	AudioMsgId id;
	State state = State::Stopped;
	int64 position = 0;
	TimeMs duration = 0;
	int frequency = kDefaultFrequency;
};

class Mixer : public QObject, private base::Subscriber {
	Q_OBJECT

public:
	Mixer();

	void play(const AudioMsgId &audio, int64 position = 0);
	void pauseresume(AudioMsgId::Type type, bool fast = false);
	void seek(AudioMsgId::Type type, int64 position); // type == AudioMsgId::Type::Song
	void stop(AudioMsgId::Type type);

	// Video player audio stream interface.
	void initFromVideo(uint64 videoPlayId, std::unique_ptr<VideoSoundData> &&data, int64 position);
	void feedFromVideo(VideoSoundPart &&part);
	int64 getVideoCorrectedTime(uint64 playId, TimeMs frameMs, TimeMs systemMs);
	TrackState currentVideoState(uint64 videoPlayId);
	void stopFromVideo(uint64 videoPlayId);
	void pauseFromVideo(uint64 videoPlayId);
	void resumeFromVideo(uint64 videoPlayId);

	void stopAndClear();

	TrackState currentState(AudioMsgId::Type type);

	void clearStoppedAtStart(const AudioMsgId &audio);

	void detachFromDeviceByTimer();
	void detachTracks();
	void reattachIfNeeded();
	void reattachTracks();

	~Mixer();

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
	void resetFadeStartPosition(AudioMsgId::Type type, int positionInBuffered = -1);
	bool checkCurrentALError(AudioMsgId::Type type);

	void videoSoundProgress(const AudioMsgId &audio);

	class Track {
	public:
		static constexpr int kBuffersCount = 3;

		void reattach(AudioMsgId::Type type);
		void detach();
		void clear();
		void started();

		bool isStreamCreated() const;
		void ensureStreamCreated();

		int getNotQueuedBufferIndex();

		TrackState state;

		FileLocation file;
		QByteArray data;
		int64 bufferedPosition = 0;
		int64 bufferedLength = 0;
		bool loading = false;
		bool loaded = false;
		int64 fadeStartPosition = 0;

		int32 format = 0;
		int32 frequency = kDefaultFrequency;
		int samplesCount[kBuffersCount] = { 0 };
		QByteArray bufferSamples[kBuffersCount];

		struct Stream {
			uint32 source = 0;
			uint32 buffers[kBuffersCount] = { 0 };
		};
		Stream stream;

		uint64 videoPlayId = 0;
		std::unique_ptr<VideoSoundData> videoData;

	private:
		void createStream();
		void destroyStream();
		void resetStream();

	};

	void setStoppedState(Track *current, State state = State::Stopped);

	Track *trackForType(AudioMsgId::Type type, int index = -1); // -1 uses currentIndex(type)
	const Track *trackForType(AudioMsgId::Type type, int index = -1) const;
	int *currentIndex(AudioMsgId::Type type);
	const int *currentIndex(AudioMsgId::Type type) const;

	int _audioCurrent = 0;
	Track _audioTracks[kTogetherLimit];

	int _songCurrent = 0;
	Track _songTracks[kTogetherLimit];

	Track _videoTrack;
	uint64 _lastVideoPlayId = 0;
	TimeMs _lastVideoPlaybackWhen = 0;
	TimeMs _lastVideoPlaybackCorrectedMs = 0;
	QMutex _lastVideoMutex;

	QMutex _mutex;

	friend class Fader;
	friend class Loaders;

	QThread _faderThread, _loaderThread;
	Fader *_fader;
	Loaders *_loader;

};

Mixer *mixer();

class Fader : public QObject {
	Q_OBJECT

public:
	Fader(QThread *thread);
	void keepAttachedToDevice();

signals:
	void error(const AudioMsgId &audio);
	void playPositionUpdated(const AudioMsgId &audio);
	void audioStopped(const AudioMsgId &audio);
	void needToPreload(const AudioMsgId &audio);

public slots:
	void onDetachFromDeviceByTimer(bool force);

	void onInit();
	void onTimer();
	void onDetachFromDeviceTimer();

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
	int32 updateOnePlayback(Mixer::Track *track, bool &hasPlaying, bool &hasFading, float64 suppressGain, bool suppressGainChanged);
	void setStoppedState(Mixer::Track *track, State state = State::Stopped);

	QTimer _timer;

	bool _suppressAll = false;
	bool _suppressAllAnim = false;
	bool _suppressSong = false;
	bool _suppressSongAnim = false;
	bool _songVolumeChanged = false;
	bool _videoVolumeChanged = false;
	anim::value _suppressAllGain, _suppressSongGain;
	TimeMs _suppressAllStart = 0;
	TimeMs _suppressSongStart = 0;

	QTimer _detachFromDeviceTimer;
	QMutex _detachFromDeviceMutex;
	bool _detachFromDeviceForce = false;

};

FileLoadTask::Song PrepareForSending(const QString &fname, const QByteArray &data);

} // namespace Player
} // namespace Media

namespace internal {

QMutex *audioPlayerMutex();
bool audioCheckError();

// AudioMutex must be locked.
bool CheckAudioDeviceConnected();

} // namespace internal

VoiceWaveform audioCountWaveform(const FileLocation &file, const QByteArray &data);
