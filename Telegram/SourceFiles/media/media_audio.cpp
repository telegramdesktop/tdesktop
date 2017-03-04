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
#include "media/media_audio.h"

#include "media/media_audio_ffmpeg_loader.h"
#include "media/media_child_ffmpeg_loader.h"
#include "media/media_audio_loaders.h"
#include "platform/platform_audio.h"

#include <AL/al.h>
#include <AL/alc.h>

#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>

#include <numeric>

Q_DECLARE_METATYPE(AudioMsgId);
Q_DECLARE_METATYPE(VoiceWaveform);

extern "C" {
#ifdef Q_OS_MAC
#include <iconv.h>

#undef iconv_open
#undef iconv
#undef iconv_close

iconv_t iconv_open(const char* tocode, const char* fromcode) {
	return libiconv_open(tocode, fromcode);
}
size_t iconv(iconv_t cd, char** inbuf, size_t *inbytesleft, char** outbuf, size_t *outbytesleft) {
	return libiconv(cd, inbuf, inbytesleft, outbuf, outbytesleft);
}
int iconv_close(iconv_t cd) {
	return libiconv_close(cd);
}
#endif // Q_OS_MAC

} // extern "C"

namespace {

QMutex AudioMutex;
ALCdevice *AudioDevice = nullptr;
ALCcontext *AudioContext = nullptr;

auto suppressAllGain = 1.;
auto suppressSongGain = 1.;

} // namespace

namespace Media {
namespace Player {
namespace {

constexpr auto kPreloadSamples = 2LL * 48000; // preload next part if less than 2 seconds remains
constexpr auto kFadeDuration = TimeMs(500);
constexpr auto kCheckPlaybackPositionTimeout = TimeMs(100); // 100ms per check audio position
constexpr auto kCheckPlaybackPositionDelta = 2400LL; // update position called each 2400 samples
constexpr auto kCheckFadingTimeout = TimeMs(7); // 7ms
constexpr auto kDetachDeviceTimeout = TimeMs(500); // destroy the audio device after 500ms of silence

struct NotifySound {
	QByteArray data;
	TimeMs lengthMs = 0;
	int sampleRate = 0;

	ALenum alFormat = 0;

	ALuint source = 0;
	ALuint buffer = 0;
};
NotifySound DefaultNotify;

void PrepareNotifySound() {
	auto content = ([] {
		QFile soundFile(":/gui/art/newmsg.wav");
		soundFile.open(QIODevice::ReadOnly);
		return soundFile.readAll();
	})();
	auto data = content.constData();
	auto size = content.size();
	t_assert(size >= 44);

	t_assert(*((const uint32*)(data + 0)) == 0x46464952); // ChunkID - "RIFF"
	t_assert(*((const uint32*)(data + 4)) == uint32(size - 8)); // ChunkSize
	t_assert(*((const uint32*)(data + 8)) == 0x45564157); // Format - "WAVE"
	t_assert(*((const uint32*)(data + 12)) == 0x20746d66); // Subchunk1ID - "fmt "
	auto subchunk1Size = *((const uint32*)(data + 16));
	auto extra = subchunk1Size - 16;
	t_assert(subchunk1Size >= 16 && (!extra || extra >= 2));
	t_assert(*((const uint16*)(data + 20)) == 1); // AudioFormat - PCM (1)

	auto numChannels = *((const uint16*)(data + 22));
	t_assert(numChannels == 1 || numChannels == 2);

	auto sampleRate = *((const uint32*)(data + 24));
	auto byteRate = *((const uint32*)(data + 28));

	auto blockAlign = *((const uint16*)(data + 32));
	auto bitsPerSample = *((const uint16*)(data + 34));
	t_assert(!(bitsPerSample % 8));

	auto bytesPerSample = bitsPerSample / 8;
	t_assert(bytesPerSample == 1 || bytesPerSample == 2);

	t_assert(blockAlign == numChannels * bytesPerSample);
	t_assert(byteRate == sampleRate * blockAlign);

	if (extra) {
		auto extraSize = *((const uint16*)(data + 36));
		t_assert(uint32(extraSize + 2) == extra);
		t_assert(uint32(size) >= 44 + extra);
	}

	t_assert(*((const uint32*)(data + extra + 36)) == 0x61746164); // Subchunk2ID - "data"
	auto subchunk2Size = *((const uint32*)(data + extra + 40));

	t_assert(!(subchunk2Size % (numChannels * bytesPerSample)));
	auto numSamples = subchunk2Size / (numChannels * bytesPerSample);

	t_assert(uint32(size) >= 44 + extra + subchunk2Size);
	data += 44 + extra;

	auto format = ALenum(0);
	switch (bytesPerSample) {
	case 1:
		switch (numChannels) {
		case 1: format = AL_FORMAT_MONO8; break;
		case 2: format = AL_FORMAT_STEREO8; break;
		}
	break;

	case 2:
		switch (numChannels) {
		case 1: format = AL_FORMAT_MONO16; break;
		case 2: format = AL_FORMAT_STEREO16; break;
		}
	break;
	}
	t_assert(format != 0);

	DefaultNotify.alFormat = format;
	DefaultNotify.sampleRate = sampleRate;
	auto addBytes = (sampleRate * 15 / 100) * bytesPerSample * numChannels; // add 150ms of silence
	DefaultNotify.data = QByteArray(addBytes + subchunk2Size, (bytesPerSample == 1) ? 128 : 0);
	memcpy(DefaultNotify.data.data() + addBytes, data, subchunk2Size);
	DefaultNotify.lengthMs = (numSamples * 1000LL / sampleRate);
}

base::Observable<AudioMsgId> UpdatedObservable;

Mixer *MixerInstance = nullptr;

bool ContextErrorHappened() {
	ALenum errCode;
	if ((errCode = alcGetError(AudioDevice)) != ALC_NO_ERROR) {
		LOG(("Audio Context Error: %1, %2").arg(errCode).arg((const char *)alcGetString(AudioDevice, errCode)));
		return true;
	}
	return false;
}

bool PlaybackErrorHappened() {
	ALenum errCode;
	if ((errCode = alGetError()) != AL_NO_ERROR) {
		LOG(("Audio Playback Error: %1, %2").arg(errCode).arg((const char *)alGetString(errCode)));
		return true;
	}
	return false;
}

void EnumeratePlaybackDevices() {
	auto deviceNames = QStringList();
	auto devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
	t_assert(devices != nullptr);
	while (*devices != 0) {
		auto deviceName8Bit = QByteArray(devices);
		auto deviceName = QString::fromLocal8Bit(deviceName8Bit);
		deviceNames.append(deviceName);
		devices += deviceName8Bit.size() + 1;
	}
	LOG(("Audio Playback Devices: %1").arg(deviceNames.join(';')));

	if (auto device = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER)) {
		LOG(("Audio Playback Default Device: %1").arg(QString::fromLocal8Bit(device)));
	} else {
		LOG(("Audio Playback Default Device: (null)"));
	}
}

void EnumerateCaptureDevices() {
	auto deviceNames = QStringList();
	auto devices = alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER);
	t_assert(devices != nullptr);
	while (*devices != 0) {
		auto deviceName8Bit = QByteArray(devices);
		auto deviceName = QString::fromLocal8Bit(deviceName8Bit);
		deviceNames.append(deviceName);
		devices += deviceName8Bit.size() + 1;
	}
	LOG(("Audio Capture Devices: %1").arg(deviceNames.join(';')));

	if (auto device = alcGetString(nullptr, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER)) {
		LOG(("Audio Capture Default Device: %1").arg(QString::fromLocal8Bit(device)));
	} else {
		LOG(("Audio Capture Default Device: (null)"));
	}
}

ALuint CreateSource() {
	auto source = ALuint(0);
	alGenSources(1, &source);
	alSourcef(source, AL_PITCH, 1.f);
	alSourcef(source, AL_GAIN, 1.f);
	alSource3f(source, AL_POSITION, 0, 0, 0);
	alSource3f(source, AL_VELOCITY, 0, 0, 0);
	alSourcei(source, AL_LOOPING, 0);
	return source;
}

ALuint CreateBuffer() {
	auto buffer = ALuint(0);
	alGenBuffers(1, &buffer);
	return buffer;
}

void CreateDefaultNotify() {
	if (alIsSource(DefaultNotify.source)) {
		return;
	}

	DefaultNotify.source = CreateSource();
	DefaultNotify.buffer = CreateBuffer();

	alBufferData(DefaultNotify.buffer, DefaultNotify.alFormat, DefaultNotify.data.constData(), DefaultNotify.data.size(), DefaultNotify.sampleRate);
	alSourcei(DefaultNotify.source, AL_BUFFER, DefaultNotify.buffer);
}

// can be called at any moment when audio error
void CloseAudioPlaybackDevice() {
	if (!AudioDevice) return;

	LOG(("Audio Info: closing audio playback device"));
	if (alIsSource(DefaultNotify.source)) {
		alSourceStop(DefaultNotify.source);
		alSourcei(DefaultNotify.source, AL_BUFFER, AL_NONE);
		alDeleteBuffers(1, &DefaultNotify.buffer);
		alDeleteSources(1, &DefaultNotify.source);
	}
	DefaultNotify.buffer = 0;
	DefaultNotify.source = 0;

	if (mixer()) {
		mixer()->detachTracks();
	}

	if (AudioContext) {
		alcMakeContextCurrent(nullptr);
		alcDestroyContext(AudioContext);
		AudioContext = nullptr;
	}

	if (AudioDevice) {
		alcCloseDevice(AudioDevice);
		AudioDevice = nullptr;
	}
}

} // namespace

void InitAudio() {
	t_assert(AudioDevice == nullptr);

	qRegisterMetaType<AudioMsgId>();
	qRegisterMetaType<VoiceWaveform>();

	PrepareNotifySound();

	auto loglevel = getenv("ALSOFT_LOGLEVEL");
	LOG(("OpenAL Logging Level: %1").arg(loglevel ? loglevel : "(not set)"));

	EnumeratePlaybackDevices();
	EnumerateCaptureDevices();

	MixerInstance = new Mixer();

	Platform::Audio::Init();
}

void DeInitAudio() {
	Platform::Audio::DeInit();

	delete base::take(MixerInstance);
	CloseAudioPlaybackDevice();
}

base::Observable<AudioMsgId> &Updated() {
	return UpdatedObservable;
}

bool CreateAudioPlaybackDevice() {
	if (AudioDevice) return true;

	AudioDevice = alcOpenDevice(nullptr);
	if (!AudioDevice) {
		LOG(("Audio Error: Could not create default playback device, enumerating.."));
		EnumeratePlaybackDevices();
		return false;
	}

	ALCint attributes[] = { ALC_STEREO_SOURCES, 8, 0 };
	AudioContext = alcCreateContext(AudioDevice, attributes);
	alcMakeContextCurrent(AudioContext);
	if (ContextErrorHappened()) {
		CloseAudioPlaybackDevice();
		return false;
	}

	ALfloat v[] = { 0.f, 0.f, -1.f, 0.f, 1.f, 0.f };
	alListener3f(AL_POSITION, 0.f, 0.f, 0.f);
	alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f);
	alListenerfv(AL_ORIENTATION, v);

	alDistanceModel(AL_NONE);

	return true;
}

void DetachFromDeviceByTimer() {
	QMutexLocker lock(&AudioMutex);
	if (mixer()) {
		mixer()->detachFromDeviceByTimer();
	}
}

void DetachFromDevice() {
	QMutexLocker lock(&AudioMutex);
	CloseAudioPlaybackDevice();
	if (mixer()) {
		mixer()->reattachIfNeeded();
	}
}

void PlayNotify() {
	QMutexLocker lock(&AudioMutex);
	if (!mixer()) return;

	mixer()->reattachTracks();
	if (!AudioDevice) return;

	CreateDefaultNotify();
	alSourcePlay(DefaultNotify.source);
	if (PlaybackErrorHappened()) {
		CloseAudioPlaybackDevice();
		return;
	}

	emit mixer()->suppressAll();
	emit mixer()->faderOnTimer();
}

bool NotifyIsPlaying() {
	if (alIsSource(DefaultNotify.source)) {
		ALint state = AL_INITIAL;
		alGetSourcei(DefaultNotify.source, AL_SOURCE_STATE, &state);
		if (!PlaybackErrorHappened() && state == AL_PLAYING) {
			return true;
		}
	}
	return false;
}

float64 ComputeVolume(AudioMsgId::Type type) {
	switch (type) {
	case AudioMsgId::Type::Voice: return suppressAllGain;
	case AudioMsgId::Type::Song: return suppressSongGain * Global::SongVolume();
	case AudioMsgId::Type::Video: return suppressSongGain * Global::VideoVolume();
	}
	return 1.;
}

Mixer *mixer() {
	return MixerInstance;
}

void Mixer::Track::createStream() {
	alGenSources(1, &stream.source);
	alSourcef(stream.source, AL_PITCH, 1.f);
	alSource3f(stream.source, AL_POSITION, 0, 0, 0);
	alSource3f(stream.source, AL_VELOCITY, 0, 0, 0);
	alSourcei(stream.source, AL_LOOPING, 0);
	alGenBuffers(3, stream.buffers);
}

void Mixer::Track::destroyStream() {
	if (isStreamCreated()) {
		alDeleteBuffers(3, stream.buffers);
		alDeleteSources(1, &stream.source);
	}
	stream.source = 0;
	for (auto i = 0; i != 3; ++i) {
		stream.buffers[i] = 0;
	}
}

void Mixer::Track::reattach(AudioMsgId::Type type) {
	if (isStreamCreated() || !samplesCount[0]) {
		return;
	}

	createStream();
	for (auto i = 0; i != kBuffersCount; ++i) {
		if (!samplesCount[i]) {
			break;
		}
		alBufferData(stream.buffers[i], format, bufferSamples[i].constData(), bufferSamples[i].size(), frequency);
		alSourceQueueBuffers(stream.source, 1, stream.buffers + i);
	}

	alSourcei(stream.source, AL_SAMPLE_OFFSET, qMax(state.position - bufferedPosition, 0LL));
	if (IsActive(state.state)) {
		alSourcef(stream.source, AL_GAIN, ComputeVolume(type));
		alSourcePlay(stream.source);
	}
}

void Mixer::Track::detach() {
	resetStream();
	destroyStream();
}

void Mixer::Track::clear() {
	detach();

	state = TrackState();
	file = FileLocation();
	data = QByteArray();
	bufferedPosition = 0;
	bufferedLength = 0;
	loading = false;
	loaded = false;
	fadeStartPosition = 0;

	format = 0;
	frequency = kDefaultFrequency;
	for (int i = 0; i != kBuffersCount; ++i) {
		samplesCount[i] = 0;
		bufferSamples[i] = QByteArray();
	}

	videoData = nullptr;
	videoPlayId = 0;
}

void Mixer::Track::started() {
	resetStream();

	bufferedPosition = 0;
	bufferedLength = 0;
	loaded = false;
	fadeStartPosition = 0;

	format = 0;
	frequency = kDefaultFrequency;
	for (auto i = 0; i != kBuffersCount; ++i) {
		samplesCount[i] = 0;
		bufferSamples[i] = QByteArray();
	}
}

bool Mixer::Track::isStreamCreated() const {
	return alIsSource(stream.source);
}

void Mixer::Track::ensureStreamCreated() {
	if (!isStreamCreated()) {
		createStream();
	}
}

int Mixer::Track::getNotQueuedBufferIndex() {
	// See if there are no free buffers right now.
	while (samplesCount[kBuffersCount - 1] != 0) {
		// Try to unqueue some buffer.
		ALint processed = 0;
		alGetSourcei(stream.source, AL_BUFFERS_PROCESSED, &processed);
		if (processed < 1) { // No processed buffers, wait.
			return -1;
		}

		// Unqueue some processed buffer.
		ALuint buffer = 0;
		alSourceUnqueueBuffers(stream.source, 1, &buffer);

		// Find it in the list and clear it.
		bool found = false;
		for (auto i = 0; i != kBuffersCount; ++i) {
			if (stream.buffers[i] == buffer) {
				auto samplesInBuffer = samplesCount[i];
				bufferedPosition += samplesInBuffer;
				bufferedLength -= samplesInBuffer;
				for (auto j = i + 1; j != kBuffersCount; ++j) {
					samplesCount[j - 1] = samplesCount[j];
					stream.buffers[j - 1] = stream.buffers[j];
					bufferSamples[j - 1] = bufferSamples[j];
				}
				samplesCount[kBuffersCount - 1] = 0;
				stream.buffers[kBuffersCount - 1] = buffer;
				bufferSamples[kBuffersCount - 1] = QByteArray();
				found = true;
				break;
			}
		}
		if (!found) {
			LOG(("Audio Error: Could not find the unqueued buffer! Buffer %1 in source %2 with processed count %3").arg(buffer).arg(stream.source).arg(processed));
			return -1;
		}
	}

	for (auto i = 0; i != kBuffersCount; ++i) {
		if (!samplesCount[i]) {
			return i;
		}
	}
	return -1;
}

void Mixer::Track::resetStream() {
	if (isStreamCreated()) {
		alSourceStop(stream.source);
		alSourcei(stream.source, AL_BUFFER, AL_NONE);
	}
}

Mixer::Mixer()
: _fader(new Fader(&_faderThread))
, _loader(new Loaders(&_loaderThread)) {
	connect(this, SIGNAL(faderOnTimer()), _fader, SLOT(onTimer()), Qt::QueuedConnection);
	connect(this, SIGNAL(suppressSong()), _fader, SLOT(onSuppressSong()));
	connect(this, SIGNAL(unsuppressSong()), _fader, SLOT(onUnsuppressSong()));
	connect(this, SIGNAL(suppressAll()), _fader, SLOT(onSuppressAll()));
	subscribe(Global::RefSongVolumeChanged(), [this] {
		QMetaObject::invokeMethod(_fader, "onSongVolumeChanged");
	});
	subscribe(Global::RefVideoVolumeChanged(), [this] {
		QMetaObject::invokeMethod(_fader, "onVideoVolumeChanged");
	});
	connect(this, SIGNAL(loaderOnStart(const AudioMsgId&, qint64)), _loader, SLOT(onStart(const AudioMsgId&, qint64)));
	connect(this, SIGNAL(loaderOnCancel(const AudioMsgId&)), _loader, SLOT(onCancel(const AudioMsgId&)));
	connect(_loader, SIGNAL(needToCheck()), _fader, SLOT(onTimer()));
	connect(_loader, SIGNAL(error(const AudioMsgId&)), this, SLOT(onError(const AudioMsgId&)));
	connect(_fader, SIGNAL(needToPreload(const AudioMsgId&)), _loader, SLOT(onLoad(const AudioMsgId&)));
	connect(_fader, SIGNAL(playPositionUpdated(const AudioMsgId&)), this, SIGNAL(updated(const AudioMsgId&)));
	connect(_fader, SIGNAL(audioStopped(const AudioMsgId&)), this, SLOT(onStopped(const AudioMsgId&)));
	connect(_fader, SIGNAL(error(const AudioMsgId&)), this, SLOT(onError(const AudioMsgId&)));
	connect(this, SIGNAL(stoppedOnError(const AudioMsgId&)), this, SIGNAL(updated(const AudioMsgId&)), Qt::QueuedConnection);
	connect(this, SIGNAL(updated(const AudioMsgId&)), this, SLOT(onUpdated(const AudioMsgId&)));

	_loaderThread.start();
	_faderThread.start();
}

Mixer::~Mixer() {
	{
		QMutexLocker lock(&AudioMutex);

		for (auto i = 0; i != kTogetherLimit; ++i) {
			trackForType(AudioMsgId::Type::Voice, i)->clear();
			trackForType(AudioMsgId::Type::Song, i)->clear();
		}
		_videoTrack.clear();

		CloseAudioPlaybackDevice();
		MixerInstance = nullptr;
	}

	_faderThread.quit();
	_loaderThread.quit();
	_faderThread.wait();
	_loaderThread.wait();
}

void Mixer::onUpdated(const AudioMsgId &audio) {
	if (audio.type() == AudioMsgId::Type::Video) {
		videoSoundProgress(audio);
	}
	Media::Player::Updated().notify(audio);
}

void Mixer::onError(const AudioMsgId &audio) {
	emit stoppedOnError(audio);
	if (audio.type() == AudioMsgId::Type::Voice) {
		emit unsuppressSong();
	}
}

void Mixer::onStopped(const AudioMsgId &audio) {
	emit updated(audio);
	if (audio.type() == AudioMsgId::Type::Voice) {
		emit unsuppressSong();
	}
}

Mixer::Track *Mixer::trackForType(AudioMsgId::Type type, int index) {
	if (index < 0) {
		if (auto indexPtr = currentIndex(type)) {
			index = *indexPtr;
		} else {
			return nullptr;
		}
	}
	switch (type) {
	case AudioMsgId::Type::Voice: return &_audioTracks[index];
	case AudioMsgId::Type::Song: return &_songTracks[index];
	case AudioMsgId::Type::Video: return &_videoTrack;
	}
	return nullptr;
}

const Mixer::Track *Mixer::trackForType(AudioMsgId::Type type, int index) const {
	return const_cast<Mixer*>(this)->trackForType(type, index);
}

int *Mixer::currentIndex(AudioMsgId::Type type) {
	switch (type) {
	case AudioMsgId::Type::Voice: return &_audioCurrent;
	case AudioMsgId::Type::Song: return &_songCurrent;
	case AudioMsgId::Type::Video: { static int videoIndex = 0; return &videoIndex; }
	}
	return nullptr;
}

const int *Mixer::currentIndex(AudioMsgId::Type type) const {
	return const_cast<Mixer*>(this)->currentIndex(type);
}

void Mixer::resetFadeStartPosition(AudioMsgId::Type type, int positionInBuffered) {
	auto track = trackForType(type);
	if (!track) return;

	if (positionInBuffered < 0) {
		reattachTracks();
		if (track->isStreamCreated()) {
			ALint currentPosition = 0;
			alGetSourcei(track->stream.source, AL_SAMPLE_OFFSET, &currentPosition);

			if (Media::Player::PlaybackErrorHappened()) {
				setStoppedState(track, State::StoppedAtError);
				onError(track->state.id);
				return;
			}

			if (currentPosition == 0 && !internal::CheckAudioDeviceConnected()) {
				track->fadeStartPosition = track->state.position;
				return;
			}

			positionInBuffered = currentPosition;
		} else {
			positionInBuffered = 0;
		}
	}
	auto fullPosition = track->bufferedPosition + positionInBuffered;
	track->state.position = fullPosition;
	track->fadeStartPosition = fullPosition;
}

bool Mixer::fadedStop(AudioMsgId::Type type, bool *fadedStart) {
	auto current = trackForType(type);
	if (!current) return false;

	switch (current->state.state) {
	case State::Starting:
	case State::Resuming:
	case State::Playing: {
		current->state.state = State::Finishing;
		resetFadeStartPosition(type);
		if (fadedStart) *fadedStart = true;
	} break;
	case State::Pausing: {
		current->state.state = State::Finishing;
		if (fadedStart) *fadedStart = true;
	} break;
	case State::Paused:
	case State::PausedAtEnd: {
		setStoppedState(current);
	} return true;
	}
	return false;
}

void Mixer::play(const AudioMsgId &audio, int64 position) {
	auto type = audio.type();
	AudioMsgId stopped;
	auto notLoadedYet = false;
	{
		QMutexLocker lock(&AudioMutex);
		reattachTracks();
		if (!AudioDevice) return;

		bool fadedStart = false;
		auto current = trackForType(type);
		if (!current) return;

		if (current->state.id != audio) {
			if (fadedStop(type, &fadedStart)) {
				stopped = current->state.id;
			}
			if (current->state.id) {
				emit loaderOnCancel(current->state.id);
				emit faderOnTimer();
			}

			auto foundCurrent = currentIndex(type);
			auto index = 0;
			for (; index != kTogetherLimit; ++index) {
				if (trackForType(type, index)->state.id == audio) {
					*foundCurrent = index;
					break;
				}
			}
			if (index == kTogetherLimit && ++*foundCurrent >= kTogetherLimit) {
				*foundCurrent -= kTogetherLimit;
			}
			current = trackForType(type);
		}
		current->state.id = audio;
		current->file = audio.audio()->location(true);
		current->data = audio.audio()->data();
		if (current->file.isEmpty() && current->data.isEmpty()) {
			notLoadedYet = true;
			if (audio.type() == AudioMsgId::Type::Song) {
				setStoppedState(current);
			} else {
				setStoppedState(current, State::StoppedAtError);
			}
		} else {
			current->state.position = position;
			current->state.state = fadedStart ? State::Starting : State::Playing;
			current->loading = true;
			emit loaderOnStart(audio, position);
			if (type == AudioMsgId::Type::Voice) {
				emit suppressSong();
			}
		}
	}
	if (notLoadedYet) {
		if (audio.type() == AudioMsgId::Type::Song) {
			DocumentOpenClickHandler::doOpen(audio.audio(), App::histItemById(audio.contextId()));
		} else {
			onError(audio);
		}
	}
	if (stopped) {
		emit updated(stopped);
	}
}

void Mixer::initFromVideo(uint64 videoPlayId, std::unique_ptr<VideoSoundData> &&data, int64 position) {
	AudioMsgId stopped;
	{
		QMutexLocker lock(&AudioMutex);

		// Pause current song.
		auto songType = AudioMsgId::Type::Song;
		auto currentSong = trackForType(songType);

		switch (currentSong->state.state) {
		case State::Starting:
		case State::Resuming:
		case State::Playing: {
			currentSong->state.state = State::Pausing;
			resetFadeStartPosition(songType);
		} break;
		case State::Finishing: {
			currentSong->state.state = State::Pausing;
		} break;
		}

		auto type = AudioMsgId::Type::Video;
		auto current = trackForType(type);
		t_assert(current != nullptr);

		if (current->state.id) {
			fadedStop(type);
			stopped = current->state.id;
			emit loaderOnCancel(current->state.id);
		}
		emit faderOnTimer();
		current->clear();
		current->state.id = AudioMsgId(AudioMsgId::Type::Video);
		current->videoPlayId = videoPlayId;
		current->videoData = std::move(data);
		{
			QMutexLocker videoLock(&_lastVideoMutex);
			_lastVideoPlayId = current->videoPlayId;
			_lastVideoPlaybackWhen = 0;
			_lastVideoPlaybackCorrectedMs = 0;
		}
		_loader->startFromVideo(current->videoPlayId);

		current->state.state = State::Paused;
		current->loading = true;
		emit loaderOnStart(current->state.id, position);
	}
	if (stopped) emit updated(stopped);
}

void Mixer::stopFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto track = trackForType(AudioMsgId::Type::Video);
		t_assert(track != nullptr);

		if (track->videoPlayId != videoPlayId) {
			return;
		}

		current = track->state.id;
		fadedStop(AudioMsgId::Type::Video);
		track->clear();
	}
	if (current) emit updated(current);
}

void Mixer::pauseFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto type = AudioMsgId::Type::Video;
		auto track = trackForType(type);
		t_assert(track != nullptr);

		if (track->videoPlayId != videoPlayId) {
			return;
		}

		current = track->state.id;
		switch (track->state.state) {
		case State::Starting:
		case State::Resuming:
		case State::Playing: {
			track->state.state = State::Paused;
			resetFadeStartPosition(type);

			if (track->isStreamCreated()) {
				ALint state = AL_INITIAL;
				alGetSourcei(track->stream.source, AL_SOURCE_STATE, &state);
				if (!checkCurrentALError(type)) return;

				if (state == AL_PLAYING) {
					alSourcePause(track->stream.source);
					if (!checkCurrentALError(type)) return;
				}
			}
		} break;
		}
		emit faderOnTimer();

		QMutexLocker videoLock(&_lastVideoMutex);
		if (_lastVideoPlayId == videoPlayId) {
			_lastVideoPlaybackWhen = 0;
			_lastVideoPlaybackCorrectedMs = 0;
		}
	}
	if (current) emit updated(current);
}

void Mixer::resumeFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto type = AudioMsgId::Type::Video;
		auto track = trackForType(type);
		t_assert(track != nullptr);

		if (track->videoPlayId != videoPlayId) {
			return;
		}

		current = track->state.id;
		switch (track->state.state) {
		case State::Pausing:
		case State::Paused:
		case State::PausedAtEnd: {
			reattachTracks();
			if (track->state.state == State::Paused) {
				// This calls reattachTracks().
				resetFadeStartPosition(type);
			} else {
				reattachTracks();
				if (track->state.state == State::PausedAtEnd) {
					if (track->isStreamCreated()) {
						alSourcei(track->stream.source, AL_SAMPLE_OFFSET, qMax(track->state.position - track->bufferedPosition, 0LL));
						if (!checkCurrentALError(type)) return;
					}
				}
			}
			track->state.state = State::Playing;

			if (track->isStreamCreated()) {
				// When starting the video audio is in paused state and
				// gets resumed before the stream is created with any data.
				ALint state = AL_INITIAL;
				alGetSourcei(track->stream.source, AL_SOURCE_STATE, &state);
				if (!checkCurrentALError(type)) return;

				if (state != AL_PLAYING) {
					if (state == AL_STOPPED && !internal::CheckAudioDeviceConnected()) {
						return;
					}

					alSourcef(track->stream.source, AL_GAIN, ComputeVolume(type));
					if (!checkCurrentALError(type)) return;

					alSourcePlay(track->stream.source);
					if (!checkCurrentALError(type)) return;
				}
			}
		} break;
		}
		emit faderOnTimer();
	}
	if (current) emit updated(current);
}

void Mixer::feedFromVideo(VideoSoundPart &&part) {
	_loader->feedFromVideo(std::move(part));
}

TimeMs Mixer::getVideoCorrectedTime(uint64 playId, TimeMs frameMs, TimeMs systemMs) {
	auto result = frameMs;

	QMutexLocker videoLock(&_lastVideoMutex);
	if (_lastVideoPlayId == playId && _lastVideoPlaybackWhen > 0) {
		result = static_cast<TimeMs>(_lastVideoPlaybackCorrectedMs);
		if (systemMs > _lastVideoPlaybackWhen) {
			result += (systemMs - _lastVideoPlaybackWhen);
		}
	}

	return result;
}

void Mixer::videoSoundProgress(const AudioMsgId &audio) {
	auto type = audio.type();
	t_assert(type == AudioMsgId::Type::Video);

	QMutexLocker lock(&AudioMutex);
	QMutexLocker videoLock(&_lastVideoMutex);

	auto current = trackForType(type);
	t_assert(current != nullptr);

	if (current->videoPlayId == _lastVideoPlayId && current->state.duration && current->state.frequency) {
		if (current->state.state == State::Playing) {
			_lastVideoPlaybackWhen = getms();
			_lastVideoPlaybackCorrectedMs = (current->state.position * 1000ULL) / current->state.frequency;
		}
	}
}

bool Mixer::checkCurrentALError(AudioMsgId::Type type) {
	if (!Media::Player::PlaybackErrorHappened()) return true;

	auto data = trackForType(type);
	if (!data) {
		setStoppedState(data, State::StoppedAtError);
		onError(data->state.id);
	}
	return false;
}

void Mixer::pauseresume(AudioMsgId::Type type, bool fast) {
	QMutexLocker lock(&AudioMutex);

	auto current = trackForType(type);

	switch (current->state.state) {
	case State::Pausing:
	case State::Paused:
	case State::PausedAtEnd: {
		reattachTracks();
		if (current->state.state == State::Paused) {
			resetFadeStartPosition(type);
		} else if (current->state.state == State::PausedAtEnd) {
			if (current->isStreamCreated()) {
				alSourcei(current->stream.source, AL_SAMPLE_OFFSET, qMax(current->state.position - current->bufferedPosition, 0LL));
				if (!checkCurrentALError(type)) return;
			}
		}
		current->state.state = fast ? State::Playing : State::Resuming;

		ALint state = AL_INITIAL;
		alGetSourcei(current->stream.source, AL_SOURCE_STATE, &state);
		if (!checkCurrentALError(type)) return;

		if (state != AL_PLAYING) {
			if (state == AL_STOPPED && !internal::CheckAudioDeviceConnected()) {
				return;
			}

			alSourcef(current->stream.source, AL_GAIN, ComputeVolume(type));
			if (!checkCurrentALError(type)) return;

			alSourcePlay(current->stream.source);
			if (!checkCurrentALError(type)) return;
		}
		if (type == AudioMsgId::Type::Voice) emit suppressSong();
	} break;
	case State::Starting:
	case State::Resuming:
	case State::Playing: {
		current->state.state = State::Pausing;
		resetFadeStartPosition(type);
		if (type == AudioMsgId::Type::Voice) emit unsuppressSong();
	} break;
	case State::Finishing: {
		current->state.state = State::Pausing;
	} break;
	}
	emit faderOnTimer();
}

void Mixer::seek(AudioMsgId::Type type, int64 position) {
	QMutexLocker lock(&AudioMutex);

	auto current = trackForType(type);
	auto audio = current->state.id;

	reattachTracks();
	auto streamCreated = current->isStreamCreated();
	auto fastSeek = (position >= current->bufferedPosition && position < current->bufferedPosition + current->bufferedLength - (current->loaded ? 0 : kDefaultFrequency));
	if (!streamCreated) {
		fastSeek = false;
	} else if (IsStopped(current->state.state) || (current->state.state == State::Finishing)) {
		fastSeek = false;
	}
	if (fastSeek) {
		alSourcei(current->stream.source, AL_SAMPLE_OFFSET, position - current->bufferedPosition);
		if (!checkCurrentALError(type)) return;

		alSourcef(current->stream.source, AL_GAIN, ComputeVolume(type));
		if (!checkCurrentALError(type)) return;

		resetFadeStartPosition(type, position - current->bufferedPosition);
	} else {
		setStoppedState(current);
		if (streamCreated) alSourceStop(current->stream.source);
	}
	switch (current->state.state) {
	case State::Pausing:
	case State::Paused:
	case State::PausedAtEnd: {
		if (current->state.state == State::PausedAtEnd) {
			current->state.state = State::Paused;
		}
		lock.unlock();
		return pauseresume(type, true);
	} break;
	case State::Starting:
	case State::Resuming:
	case State::Playing: {
		current->state.state = State::Pausing;
		resetFadeStartPosition(type);
		if (type == AudioMsgId::Type::Voice) {
			emit unsuppressSong();
		}
	} break;
	case State::Finishing:
	case State::Stopped:
	case State::StoppedAtEnd:
	case State::StoppedAtError:
	case State::StoppedAtStart: {
		lock.unlock();
	} return play(audio, position);
	}
	emit faderOnTimer();
}

void Mixer::stop(AudioMsgId::Type type) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto track = trackForType(type);
		t_assert(track != nullptr);

		current = track->state.id;
		fadedStop(type);
		if (type == AudioMsgId::Type::Video) {
			track->clear();
		}
	}
	if (current) emit updated(current);
}

void Mixer::stopAndClear() {
	Track *current_audio = nullptr, *current_song = nullptr;
	{
		QMutexLocker lock(&AudioMutex);
		if ((current_audio = trackForType(AudioMsgId::Type::Voice))) {
			setStoppedState(current_audio);
		}
		if ((current_song = trackForType(AudioMsgId::Type::Song))) {
			setStoppedState(current_song);
		}
	}
	if (current_song) {
		emit updated(current_song->state.id);
	}
	if (current_audio) {
		emit updated(current_audio->state.id);
	}
	{
		QMutexLocker lock(&AudioMutex);
		auto clearAndCancel = [this](AudioMsgId::Type type, int index) {
			auto track = trackForType(type, index);
			if (track->state.id) {
				emit loaderOnCancel(track->state.id);
			}
			track->clear();
		};
		for (auto index = 0; index != kTogetherLimit; ++index) {
			clearAndCancel(AudioMsgId::Type::Voice, index);
			clearAndCancel(AudioMsgId::Type::Song, index);
		}
		_videoTrack.clear();
		_loader->stopFromVideo();
	}
}

TrackState Mixer::currentVideoState(uint64 videoPlayId) {
	QMutexLocker lock(&AudioMutex);
	auto current = trackForType(AudioMsgId::Type::Video);
	if (!current || current->videoPlayId != videoPlayId) {
		return TrackState();
	}
	return current->state;
}

TrackState Mixer::currentState(AudioMsgId::Type type) {
	QMutexLocker lock(&AudioMutex);
	auto current = trackForType(type);
	if (!current) {
		return TrackState();
	}
	return current->state;
}

void Mixer::setStoppedState(Track *current, State state) {
	current->state.state = state;
	current->state.position = 0;
}

void Mixer::clearStoppedAtStart(const AudioMsgId &audio) {
	QMutexLocker lock(&AudioMutex);
	auto track = trackForType(audio.type());
	if (track && track->state.id == audio && track->state.state == State::StoppedAtStart) {
		setStoppedState(track);
	}
}

void Mixer::detachFromDeviceByTimer() {
	QMetaObject::invokeMethod(_fader, "onDetachFromDeviceByTimer", Qt::QueuedConnection, Q_ARG(bool, true));
}

void Mixer::detachTracks() {
	for (auto i = 0; i != kTogetherLimit; ++i) {
		trackForType(AudioMsgId::Type::Voice, i)->detach();
		trackForType(AudioMsgId::Type::Song, i)->detach();
	}
	_videoTrack.detach();
}

void Mixer::reattachIfNeeded() {
	_fader->keepAttachedToDevice();

	auto reattachNeeded = [this] {
		auto isPlayingState = [](const Track &track) {
			auto state = track.state.state;
			return (state == State::Starting)
				|| (state == State::Playing)
				|| (state == State::Finishing)
				|| (state == State::Pausing)
				|| (state == State::Resuming);
		};
		for (auto i = 0; i != kTogetherLimit; ++i) {
			if (isPlayingState(*trackForType(AudioMsgId::Type::Voice, i))
				|| isPlayingState(*trackForType(AudioMsgId::Type::Song, i))) {
				return true;
			}
		}
		return isPlayingState(_videoTrack);
	};

	if (reattachNeeded()) {
		reattachTracks();
	}
}

void Mixer::reattachTracks() {
	if (!AudioDevice) {
		LOG(("Audio Info: recreating audio device and reattaching the tracks"));

		CreateAudioPlaybackDevice();
		for (auto i = 0; i != kTogetherLimit; ++i) {
			trackForType(AudioMsgId::Type::Voice, i)->reattach(AudioMsgId::Type::Voice);
			trackForType(AudioMsgId::Type::Song, i)->reattach(AudioMsgId::Type::Song);
		}
		_videoTrack.reattach(AudioMsgId::Type::Video);

		emit faderOnTimer();
	}
}

Fader::Fader(QThread *thread) : QObject()
, _timer(this)
, _suppressAllGain(1., 1.)
, _suppressSongGain(1., 1.) {
	moveToThread(thread);
	_timer.moveToThread(thread);
	_detachFromDeviceTimer.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(onInit()));
	connect(thread, SIGNAL(finished()), this, SLOT(deleteLater()));

	_timer.setSingleShot(true);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(onTimer()));

	_detachFromDeviceTimer.setSingleShot(true);
	connect(&_detachFromDeviceTimer, SIGNAL(timeout()), this, SLOT(onDetachFromDeviceTimer()));
}

void Fader::onInit() {
}

void Fader::onTimer() {
	QMutexLocker lock(&AudioMutex);
	if (!mixer()) return;

	bool suppressAudioChanged = false, suppressSongChanged = false;
	if (_suppressAll || _suppressSongAnim) {
		auto ms = getms();
		auto wasSong = suppressSongGain;
		if (_suppressAll) {
			auto notifyLengthMs = Media::Player::DefaultNotify.lengthMs;
			auto wasAudio = suppressAllGain;
			if (ms >= _suppressAllStart + notifyLengthMs || ms < _suppressAllStart) {
				_suppressAll = _suppressAllAnim = false;
				_suppressAllGain = anim::value(1., 1.);
			} else if (ms > _suppressAllStart + notifyLengthMs - kFadeDuration) {
				if (_suppressAllGain.to() != 1.) _suppressAllGain.start(1.);
				_suppressAllGain.update(1. - ((_suppressAllStart + notifyLengthMs - ms) / float64(kFadeDuration)), anim::linear);
			} else if (ms >= _suppressAllStart + st::mediaPlayerSuppressDuration) {
				if (_suppressAllAnim) {
					_suppressAllGain.finish();
					_suppressAllAnim = false;
				}
			} else if (ms > _suppressAllStart) {
				_suppressAllGain.update((ms - _suppressAllStart) / float64(st::mediaPlayerSuppressDuration), anim::linear);
			}
			suppressAllGain = _suppressAllGain.current();
			suppressAudioChanged = (suppressAllGain != wasAudio);
		}
		if (_suppressSongAnim) {
			if (ms >= _suppressSongStart + kFadeDuration) {
				_suppressSongGain.finish();
				_suppressSongAnim = false;
			} else {
				_suppressSongGain.update((ms - _suppressSongStart) / float64(kFadeDuration), anim::linear);
			}
		}
		suppressSongGain = qMin(suppressAllGain, _suppressSongGain.current());
		suppressSongChanged = (suppressSongGain != wasSong);
	}
	bool hasFading = (_suppressAll || _suppressSongAnim);
	bool hasPlaying = false;

	auto updatePlayback = [this, &hasPlaying, &hasFading](AudioMsgId::Type type, int index, float64 suppressGain, bool suppressGainChanged) {
		auto track = mixer()->trackForType(type, index);
		if (IsStopped(track->state.state) || track->state.state == State::Paused || !track->isStreamCreated()) return;

		int32 emitSignals = updateOnePlayback(track, hasPlaying, hasFading, suppressGain, suppressGainChanged);
		if (emitSignals & EmitError) emit error(track->state.id);
		if (emitSignals & EmitStopped) emit audioStopped(track->state.id);
		if (emitSignals & EmitPositionUpdated) emit playPositionUpdated(track->state.id);
		if (emitSignals & EmitNeedToPreload) emit needToPreload(track->state.id);
	};
	auto suppressGainForMusic = suppressSongGain * Global::SongVolume();
	auto suppressGainForMusicChanged = suppressSongChanged || _songVolumeChanged;
	for (auto i = 0; i != kTogetherLimit; ++i) {
		updatePlayback(AudioMsgId::Type::Voice, i, suppressAllGain, suppressAudioChanged);
		updatePlayback(AudioMsgId::Type::Song, i, suppressGainForMusic, suppressGainForMusicChanged);
	}
	auto suppressGainForVideo = suppressSongGain * Global::VideoVolume();
	auto suppressGainForVideoChanged = suppressSongChanged || _videoVolumeChanged;
	updatePlayback(AudioMsgId::Type::Video, 0, suppressGainForVideo, suppressGainForVideoChanged);

	_songVolumeChanged = _videoVolumeChanged = false;

	if (!hasFading && !hasPlaying && Media::Player::NotifyIsPlaying()) {
		hasPlaying = true;
	}
	if (hasFading) {
		_timer.start(kCheckFadingTimeout);
		keepAttachedToDevice();
	} else if (hasPlaying) {
		_timer.start(kCheckPlaybackPositionTimeout);
		keepAttachedToDevice();
	} else {
		onDetachFromDeviceByTimer(false);
	}
}

int32 Fader::updateOnePlayback(Mixer::Track *track, bool &hasPlaying, bool &hasFading, float64 suppressGain, bool suppressGainChanged) {
	bool playing = false, fading = false;

	auto errorHappened = [this, track] {
		if (PlaybackErrorHappened()) {
			setStoppedState(track, State::StoppedAtError);
			return true;
		}
		return false;
	};

	ALint positionInBuffered = 0;
	ALint state = AL_INITIAL;
	alGetSourcei(track->stream.source, AL_SAMPLE_OFFSET, &positionInBuffered);
	alGetSourcei(track->stream.source, AL_SOURCE_STATE, &state);
	if (errorHappened()) return EmitError;

	int32 emitSignals = 0;

	if (state == AL_STOPPED && positionInBuffered == 0 && !internal::CheckAudioDeviceConnected()) {
		return emitSignals;
	}

	switch (track->state.state) {
	case State::Finishing:
	case State::Pausing:
	case State::Starting:
	case State::Resuming: {
		fading = true;
	} break;
	case State::Playing: {
		playing = true;
	} break;
	}

	auto fullPosition = track->bufferedPosition + positionInBuffered;
	if (fading && (state == AL_PLAYING || !track->loading)) {
		auto fadingForSamplesCount = (fullPosition - track->fadeStartPosition);

		if (state != AL_PLAYING) {
			fading = false;
			if (track->stream.source) {
				alSourceStop(track->stream.source);
				alSourcef(track->stream.source, AL_GAIN, 1);
				if (errorHappened()) return EmitError;
			}
			if (track->state.state == State::Pausing) {
				track->state.state = State::PausedAtEnd;
			} else {
				setStoppedState(track, State::StoppedAtEnd);
			}
			emitSignals |= EmitStopped;
		} else if (TimeMs(1000) * fadingForSamplesCount >= kFadeDuration * track->state.frequency) {
			fading = false;
			alSourcef(track->stream.source, AL_GAIN, 1. * suppressGain);
			if (errorHappened()) return EmitError;

			switch (track->state.state) {
			case State::Finishing: {
				alSourceStop(track->stream.source);
				if (errorHappened()) return EmitError;

				setStoppedState(track);
				state = AL_STOPPED;
			} break;
			case State::Pausing: {
				alSourcePause(track->stream.source);
				if (errorHappened()) return EmitError;

				track->state.state = State::Paused;
			} break;
			case State::Starting:
			case State::Resuming: {
				track->state.state = State::Playing;
				playing = true;
			} break;
			}
		} else {
			auto newGain = TimeMs(1000) * fadingForSamplesCount / float64(kFadeDuration * track->state.frequency);
			if (track->state.state == State::Pausing || track->state.state == State::Finishing) {
				newGain = 1. - newGain;
			}
			alSourcef(track->stream.source, AL_GAIN, newGain * suppressGain);
			if (errorHappened()) return EmitError;
		}
	} else if (playing && (state == AL_PLAYING || !track->loading)) {
		if (state != AL_PLAYING) {
			playing = false;
			if (track->isStreamCreated()) {
				alSourceStop(track->stream.source);
				alSourcef(track->stream.source, AL_GAIN, 1);
				if (errorHappened()) return EmitError;
			}
			setStoppedState(track, State::StoppedAtEnd);
			emitSignals |= EmitStopped;
		} else if (suppressGainChanged) {
			alSourcef(track->stream.source, AL_GAIN, suppressGain);
			if (errorHappened()) return EmitError;
		}
	}
	if (state == AL_PLAYING && fullPosition >= track->state.position + kCheckPlaybackPositionDelta) {
		track->state.position = fullPosition;
		emitSignals |= EmitPositionUpdated;
	}
	if (playing || track->state.state == State::Starting || track->state.state == State::Resuming) {
		if (!track->loaded && !track->loading) {
			auto needPreload = (track->state.position + kPreloadSamples > track->bufferedPosition + track->bufferedLength);
			if (needPreload) {
				track->loading = true;
				emitSignals |= EmitNeedToPreload;
			}
		}
	}
	if (playing) hasPlaying = true;
	if (fading) hasFading = true;

	return emitSignals;
}

void Fader::setStoppedState(Mixer::Track *track, State state) {
	track->state.state = state;
	track->state.position = 0;
}

void Fader::onDetachFromDeviceTimer() {
	QMutexLocker lock(&_detachFromDeviceMutex);
	_detachFromDeviceForce = false;
	lock.unlock();

	DetachFromDevice();
}

void Fader::onSuppressSong() {
	if (!_suppressSong) {
		_suppressSong = true;
		_suppressSongAnim = true;
		_suppressSongStart = getms();
		_suppressSongGain.start(st::suppressSong);
		onTimer();
	}
}

void Fader::onUnsuppressSong() {
	if (_suppressSong) {
		_suppressSong = false;
		_suppressSongAnim = true;
		_suppressSongStart = getms();
		_suppressSongGain.start(1.);
		onTimer();
	}
}

void Fader::onSuppressAll() {
	_suppressAll = true;
	_suppressAllStart = getms();
	_suppressAllGain.start(st::suppressAll);
	onTimer();
}

void Fader::onSongVolumeChanged() {
	_songVolumeChanged = true;
	onTimer();
}

void Fader::onVideoVolumeChanged() {
	_videoVolumeChanged = true;
	onTimer();
}

void Fader::keepAttachedToDevice() {
	QMutexLocker lock(&_detachFromDeviceMutex);
	if (!_detachFromDeviceForce) {
		_detachFromDeviceTimer.stop();
	}
}

void Fader::onDetachFromDeviceByTimer(bool force) {
	QMutexLocker lock(&_detachFromDeviceMutex);
	if (force) {
		_detachFromDeviceForce = true;
	}
	if (!_detachFromDeviceTimer.isActive()) {
		_detachFromDeviceTimer.start(kDetachDeviceTimeout);
	}
}

} // namespace Player
} // namespace Media

namespace internal {

QMutex *audioPlayerMutex() {
	return &AudioMutex;
}

bool audioCheckError() {
	return !Media::Player::PlaybackErrorHappened();
}

bool audioDeviceIsConnected() {
	if (!AudioDevice) {
		return false;
	}
	ALint connected = 0;
	alcGetIntegerv(AudioDevice, ALC_CONNECTED, 1, &connected);
	if (Media::Player::ContextErrorHappened()) {
		return false;
	}
	return (connected != 0);
}

bool CheckAudioDeviceConnected() {
	if (audioDeviceIsConnected()) {
		return true;
	}
	if (auto mixer = Media::Player::mixer()) {
		mixer->detachFromDeviceByTimer();
	}
	return false;
}

} // namespace internal

class FFMpegAttributesReader : public AbstractFFMpegLoader {
public:

	FFMpegAttributesReader(const FileLocation &file, const QByteArray &data) : AbstractFFMpegLoader(file, data) {
	}

	bool open(qint64 &position) override {
		if (!AbstractFFMpegLoader::open(position)) {
			return false;
		}

		int res = 0;
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

		int videoStreamId = av_find_best_stream(fmtContext, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
		if (videoStreamId >= 0) {
			DEBUG_LOG(("Audio Read Error: Found video stream in file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(videoStreamId).arg(av_make_error_string(err, sizeof(err), streamId)));
			return false;
		}

		for (int32 i = 0, l = fmtContext->nb_streams; i < l; ++i) {
			AVStream *stream = fmtContext->streams[i];
			if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
				const AVPacket &packet(stream->attached_pic);
				if (packet.size) {
					bool animated = false;
					QByteArray cover((const char*)packet.data, packet.size), format;
					_cover = App::readImage(cover, &format, true, &animated);
					if (!_cover.isNull()) {
						_coverBytes = cover;
						_coverFormat = format;
						break;
					}
				}
			}
		}

		extractMetaData(fmtContext->streams[streamId]->metadata);
		extractMetaData(fmtContext->metadata);

		return true;
	}

	void trySet(QString &to, AVDictionary *dict, const char *key) {
		if (!to.isEmpty()) return;
		if (AVDictionaryEntry* tag = av_dict_get(dict, key, 0, 0)) {
			to = QString::fromUtf8(tag->value);
		}
	}
	void extractMetaData(AVDictionary *dict) {
		trySet(_title, dict, "title");
		trySet(_performer, dict, "artist");
		trySet(_performer, dict, "performer");
		trySet(_performer, dict, "album_artist");
		//for (AVDictionaryEntry *tag = av_dict_get(dict, "", 0, AV_DICT_IGNORE_SUFFIX); tag; tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX)) {
		//	const char *key = tag->key;
		//	const char *value = tag->value;
		//	QString tmp = QString::fromUtf8(value);
		//}
	}

	int32 format() override {
		return 0;
	}

	QString title() {
		return _title;
	}

	QString performer() {
		return _performer;
	}

	QImage cover() {
		return _cover;
	}

	QByteArray coverBytes() {
		return _coverBytes;
	}

	QByteArray coverFormat() {
		return _coverFormat;
	}

	ReadResult readMore(QByteArray &result, int64 &samplesAdded) override {
		DEBUG_LOG(("Audio Read Error: should not call this"));
		return ReadResult::Error;
	}

	~FFMpegAttributesReader() {
	}

private:

	QString _title, _performer;
	QImage _cover;
	QByteArray _coverBytes, _coverFormat;

};

MTPDocumentAttribute audioReadSongAttributes(const QString &fname, const QByteArray &data, QImage &cover, QByteArray &coverBytes, QByteArray &coverFormat) {
	FFMpegAttributesReader reader(FileLocation(StorageFilePartial, fname), data);
	qint64 position = 0;
	if (reader.open(position)) {
		int32 duration = reader.duration() / reader.frequency();
		if (reader.duration() > 0) {
			cover = reader.cover();
			coverBytes = reader.coverBytes();
			coverFormat = reader.coverFormat();
			return MTP_documentAttributeAudio(MTP_flags(MTPDdocumentAttributeAudio::Flag::f_title | MTPDdocumentAttributeAudio::Flag::f_performer), MTP_int(duration), MTP_string(reader.title()), MTP_string(reader.performer()), MTPstring());
		}
	}
	return MTP_documentAttributeFilename(MTP_string(fname));
}

class FFMpegWaveformCounter : public FFMpegLoader {
public:

	FFMpegWaveformCounter(const FileLocation &file, const QByteArray &data) : FFMpegLoader(file, data) {
	}

	bool open(qint64 &position) override {
		if (!FFMpegLoader::open(position)) {
			return false;
		}

		QByteArray buffer;
		buffer.reserve(AudioVoiceMsgBufferSize);
		int64 countbytes = sampleSize * duration(), processed = 0, sumbytes = 0;
		if (duration() < Media::Player::kWaveformSamplesCount) {
			return false;
		}

		QVector<uint16> peaks;
		peaks.reserve(Media::Player::kWaveformSamplesCount);

		int32 fmt = format();
		uint16 peak = 0;
		while (processed < countbytes) {
			buffer.resize(0);

			int64 samples = 0;
			auto res = readMore(buffer, samples);
			if (res == ReadResult::Error || res == ReadResult::EndOfFile) {
				break;
			}
			if (buffer.isEmpty()) {
				continue;
			}

			const char *data = buffer.data();
			if (fmt == AL_FORMAT_MONO8 || fmt == AL_FORMAT_STEREO8) {
				for (int32 i = 0, l = buffer.size(); i + int32(sizeof(uchar)) <= l;) {
					uint16 sample = qAbs((int32(*(uchar*)(data + i)) - 128) * 256);
					if (peak < sample) {
						peak = sample;
					}

					i += sizeof(uchar);
					sumbytes += Media::Player::kWaveformSamplesCount;
					if (sumbytes >= countbytes) {
						sumbytes -= countbytes;
						peaks.push_back(peak);
						peak = 0;
					}
				}
			} else if (fmt == AL_FORMAT_MONO16 || fmt == AL_FORMAT_STEREO16) {
				for (int32 i = 0, l = buffer.size(); i + int32(sizeof(uint16)) <= l;) {
					uint16 sample = qAbs(int32(*(int16*)(data + i)));
					if (peak < sample) {
						peak = sample;
					}

					i += sizeof(uint16);
					sumbytes += sizeof(uint16) * Media::Player::kWaveformSamplesCount;
					if (sumbytes >= countbytes) {
						sumbytes -= countbytes;
						peaks.push_back(peak);
						peak = 0;
					}
				}
			}
			processed += sampleSize * samples;
		}
		if (sumbytes > 0 && peaks.size() < Media::Player::kWaveformSamplesCount) {
			peaks.push_back(peak);
		}

		if (peaks.isEmpty()) {
			return false;
		}

		auto sum = std::accumulate(peaks.cbegin(), peaks.cend(), 0LL);
		peak = qMax(int32(sum * 1.8 / peaks.size()), 2500);

		result.resize(peaks.size());
		for (int32 i = 0, l = peaks.size(); i != l; ++i) {
			result[i] = char(qMin(31U, uint32(qMin(peaks.at(i), peak)) * 31 / peak));
		}

		return true;
	}

	const VoiceWaveform &waveform() const {
		return result;
	}

	~FFMpegWaveformCounter() {
	}

private:
	VoiceWaveform result;

};

VoiceWaveform audioCountWaveform(const FileLocation &file, const QByteArray &data) {
	FFMpegWaveformCounter counter(file, data);
	qint64 position = 0;
	if (counter.open(position)) {
		return counter.waveform();
	}
	return VoiceWaveform();
}
