/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/localimageloader.h"
#include "base/bytes.h"

struct VideoSoundData;
struct VideoSoundPart;

namespace Media {
namespace Audio {

// Thread: Main.
void Start();
void Finish();

// Thread: Main. Locks: AudioMutex.
bool IsAttachedToDevice();

// Thread: Any. Must be locked: AudioMutex.
bool AttachToDevice();

// Thread: Any.
void ScheduleDetachFromDeviceSafe();
void ScheduleDetachIfNotUsedSafe();
void StopDetachIfNotUsedSafe();

template <typename Callback>
void IterateSamples();

} // namespace Audio

namespace Player {

constexpr auto kDefaultFrequency = 48000; // 48 kHz
constexpr auto kTogetherLimit = 4;
constexpr auto kWaveformSamplesCount = 100;

class Fader;
class Loaders;

base::Observable<AudioMsgId> &Updated();

float64 ComputeVolume(AudioMsgId::Type type);

enum class State {
	Stopped = 0x01,
	StoppedAtEnd = 0x02,
	StoppedAtError = 0x03,
	StoppedAtStart = 0x04,

	Starting = 0x08,
	Playing = 0x10,
	Stopping = 0x18,
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

inline bool IsStoppedOrStopping(State state) {
	return IsStopped(state) || (state == State::Stopping);
}

inline bool IsStoppedAtEnd(State state) {
	return (state == State::StoppedAtEnd);
}

inline bool IsPaused(State state) {
	return (state == State::Paused)
		|| (state == State::PausedAtEnd);
}

inline bool IsFading(State state) {
	return (state == State::Starting)
		|| (state == State::Stopping)
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
	int64 length = 0;
	int frequency = kDefaultFrequency;
};

class Mixer : public QObject, private base::Subscriber {
	Q_OBJECT

public:
	Mixer();

	void play(const AudioMsgId &audio, TimeMs positionMs = 0);
	void play(const AudioMsgId &audio, std::unique_ptr<VideoSoundData> videoData, TimeMs positionMs = 0);
	void pause(const AudioMsgId &audio, bool fast = false);
	void resume(const AudioMsgId &audio, bool fast = false);
	void seek(AudioMsgId::Type type, TimeMs positionMs); // type == AudioMsgId::Type::Song
	void stop(const AudioMsgId &audio);
	void stop(const AudioMsgId &audio, State state);

	// Video player audio stream interface.
	void feedFromVideo(VideoSoundPart &&part);
	int64 getVideoCorrectedTime(const AudioMsgId &id, TimeMs frameMs, TimeMs systemMs);

	void stopAndClear();

	TrackState currentState(AudioMsgId::Type type);

	void clearStoppedAtStart(const AudioMsgId &audio);

	// Thread: Main. Must be locked: AudioMutex.
	void detachTracks();

	// Thread: Main. Must be locked: AudioMutex.
	void reattachIfNeeded();

	// Thread: Any. Must be locked: AudioMutex.
	void reattachTracks();

	// Thread: Any.
	void setSongVolume(float64 volume);
	float64 getSongVolume() const;
	void setVideoVolume(float64 volume);
	float64 getVideoVolume() const;

	~Mixer();

private slots:
	void onError(const AudioMsgId &audio);
	void onStopped(const AudioMsgId &audio);

	void onUpdated(const AudioMsgId &audio);

signals:
	void updated(const AudioMsgId &audio);
	void stoppedOnError(const AudioMsgId &audio);
	void loaderOnStart(const AudioMsgId &audio, qint64 positionMs);
	void loaderOnCancel(const AudioMsgId &audio);

	void faderOnTimer();

	void suppressSong();
	void unsuppressSong();
	void suppressAll(qint64 duration);

private:
	bool fadedStop(AudioMsgId::Type type, bool *fadedStart = 0);
	void resetFadeStartPosition(AudioMsgId::Type type, int positionInBuffered = -1);
	bool checkCurrentALError(AudioMsgId::Type type);

	void videoSoundProgress(const AudioMsgId &audio);

	class Track {
	public:
		static constexpr int kBuffersCount = 3;

		// Thread: Any. Must be locked: AudioMutex.
		void reattach(AudioMsgId::Type type);

		void detach();
		void clear();
		void started();

		bool isStreamCreated() const;
		void ensureStreamCreated();

		int getNotQueuedBufferIndex();

		~Track();

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
		std::unique_ptr<VideoSoundData> videoData;

		TimeMs lastUpdateWhen = 0;
		TimeMs lastUpdateCorrectedMs = 0;

	private:
		void createStream();
		void destroyStream();
		void resetStream();

	};

	// Thread: Any. Must be locked: AudioMutex.
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

	QAtomicInt _volumeVideo;
	QAtomicInt _volumeSong;

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

signals:
	void error(const AudioMsgId &audio);
	void playPositionUpdated(const AudioMsgId &audio);
	void audioStopped(const AudioMsgId &audio);
	void needToPreload(const AudioMsgId &audio);

public slots:
	void onInit();
	void onTimer();

	void onSuppressSong();
	void onUnsuppressSong();
	void onSuppressAll(qint64 duration);
	void onSongVolumeChanged();
	void onVideoVolumeChanged();

private:
	enum {
		EmitError = 0x01,
		EmitStopped = 0x02,
		EmitPositionUpdated = 0x04,
		EmitNeedToPreload = 0x08,
	};
	int32 updateOnePlayback(Mixer::Track *track, bool &hasPlaying, bool &hasFading, float64 volumeMultiplier, bool volumeChanged);
	void setStoppedState(Mixer::Track *track, State state = State::Stopped);

	QTimer _timer;

	bool _volumeChangedSong = false;
	bool _volumeChangedVideo = false;

	bool _suppressAll = false;
	bool _suppressAllAnim = false;
	bool _suppressSong = false;
	bool _suppressSongAnim = false;
	anim::value _suppressVolumeAll;
	anim::value _suppressVolumeSong;
	TimeMs _suppressAllStart = 0;
	TimeMs _suppressAllEnd = 0;
	TimeMs _suppressSongStart = 0;

};

FileMediaInformation::Song PrepareForSending(const QString &fname, const QByteArray &data);

namespace internal {

// Thread: Any. Must be locked: AudioMutex.
bool CheckAudioDeviceConnected();

// Thread: Main. Locks: AudioMutex.
void DetachFromDevice();

// Thread: Any.
QMutex *audioPlayerMutex();

// Thread: Any.
bool audioCheckError();

} // namespace internal

} // namespace Player
} // namespace Media

VoiceWaveform audioCountWaveform(const FileLocation &file, const QByteArray &data);

namespace Media {
namespace Audio {

TG_FORCE_INLINE uint16 ReadOneSample(uchar data) {
	return qAbs((static_cast<int16>(data) - 0x80) * 0x100);
}

TG_FORCE_INLINE uint16 ReadOneSample(int16 data) {
	return qAbs(data);
}

template <typename SampleType, typename Callback>
void IterateSamples(bytes::const_span bytes, Callback &&callback) {
	auto samplesPointer = reinterpret_cast<const SampleType*>(bytes.data());
	auto samplesCount = bytes.size() / sizeof(SampleType);
	auto samplesData = gsl::make_span(samplesPointer, samplesCount);
	for (auto sampleData : samplesData) {
		callback(ReadOneSample(sampleData));
	}
}

} // namespace Audio
} // namespace Media
