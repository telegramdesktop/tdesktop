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
#include "stdafx.h"
#include "media/media_audio.h"

#include "media/media_audio_ffmpeg_loader.h"
#include "media/media_child_ffmpeg_loader.h"
#include "media/media_audio_loaders.h"

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

float64 suppressAllGain = 1., suppressSongGain = 1.;

} // namespace

namespace Media {
namespace Player {
namespace {

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

	delete base::take(MixerInstance);

	if (alIsSource(DefaultNotify.source)) {
		alSourceStop(DefaultNotify.source);
	}
	if (alIsBuffer(DefaultNotify.buffer)) {
		alDeleteBuffers(1, &DefaultNotify.buffer);
		DefaultNotify.buffer = 0;
	}
	if (alIsSource(DefaultNotify.source)) {
		alDeleteSources(1, &DefaultNotify.source);
		DefaultNotify.source = 0;
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

	EnumeratePlaybackDevices();
	EnumerateCaptureDevices();
}

void DeInitAudio() {
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

	MixerInstance = new Mixer();

	return true;
}

void PlayNotify() {
	if (!mixer()) return;
	if (!CreateAudioPlaybackDevice()) return;

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

Mixer *mixer() {
	return MixerInstance;
}

void Mixer::AudioMsg::clear() {
	audio = AudioMsgId();
	file = FileLocation();
	data = QByteArray();
	playbackState = defaultState();
	skipStart = skipEnd = 0;
	loading = false;
	started = 0;
	if (alIsSource(source)) {
		alSourceStop(source);
	}
	for (int i = 0; i < 3; ++i) {
		if (samplesCount[i]) {
			ALuint buffer = 0;
			// This cleans some random queued buffer, not exactly the buffers[i].
			alSourceUnqueueBuffers(source, 1, &buffer);
			samplesCount[i] = 0;
		}
	}
	nextBuffer = 0;

	videoData = nullptr;
	videoPlayId = 0;
}

Mixer::Mixer()
: _fader(new Fader(&_faderThread))
, _loader(new Loaders(&_loaderThread)) {
	connect(this, SIGNAL(faderOnTimer()), _fader, SLOT(onTimer()));
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
		MixerInstance = nullptr;
	}

	auto clearAudioMsg = [](AudioMsg *msg) {
		alSourceStop(msg->source);
		if (alIsBuffer(msg->buffers[0])) {
			alDeleteBuffers(3, msg->buffers);
			for (int j = 0; j < 3; ++j) {
				msg->buffers[j] = msg->samplesCount[j] = 0;
			}
		}
		if (alIsSource(msg->source)) {
			alDeleteSources(1, &msg->source);
			msg->source = 0;
		}
	};

	for (int i = 0; i < AudioSimultaneousLimit; ++i) {
		clearAudioMsg(dataForType(AudioMsgId::Type::Voice, i));
		clearAudioMsg(dataForType(AudioMsgId::Type::Song, i));
	}
	clearAudioMsg(&_videoData);

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

Mixer::AudioMsg *Mixer::dataForType(AudioMsgId::Type type, int index) {
	if (index < 0) {
		if (auto indexPtr = currentIndex(type)) {
			index = *indexPtr;
		} else {
			return nullptr;
		}
	}
	switch (type) {
	case AudioMsgId::Type::Voice: return &_audioData[index];
	case AudioMsgId::Type::Song: return &_songData[index];
	case AudioMsgId::Type::Video: return &_videoData;
	}
	return nullptr;
}

const Mixer::AudioMsg *Mixer::dataForType(AudioMsgId::Type type, int index) const {
	return const_cast<Mixer*>(this)->dataForType(type, index);
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

bool Mixer::updateCurrentStarted(AudioMsgId::Type type, int32 pos) {
	auto data = dataForType(type);
	if (!data) return false;

	if (pos < 0) {
		if (alIsSource(data->source)) {
			alGetSourcei(data->source, AL_SAMPLE_OFFSET, &pos);
		} else {
			pos = 0;
		}
		if (Media::Player::PlaybackErrorHappened()) {
			setStoppedState(data, AudioPlayerStoppedAtError);
			onError(data->audio);
			return false;
		}
	}
	data->started = data->playbackState.position = pos + data->skipStart;
	return true;
}

bool Mixer::fadedStop(AudioMsgId::Type type, bool *fadedStart) {
	auto current = dataForType(type);
	if (!current) return false;

	switch (current->playbackState.state) {
	case AudioPlayerStarting:
	case AudioPlayerResuming:
	case AudioPlayerPlaying:
	current->playbackState.state = AudioPlayerFinishing;
	updateCurrentStarted(type);
	if (fadedStart) *fadedStart = true;
	break;
	case AudioPlayerPausing:
	current->playbackState.state = AudioPlayerFinishing;
	if (fadedStart) *fadedStart = true;
	break;
	case AudioPlayerPaused:
	case AudioPlayerPausedAtEnd:
	setStoppedState(current);
	return true;
	}
	return false;
}

void Mixer::play(const AudioMsgId &audio, int64 position) {
	if (!Media::Player::CreateAudioPlaybackDevice()) return;

	auto type = audio.type();
	AudioMsgId stopped;
	auto notLoadedYet = false;
	{
		QMutexLocker lock(&AudioMutex);

		bool fadedStart = false;
		auto current = dataForType(type);
		if (!current) return;

		if (current->audio != audio) {
			if (fadedStop(type, &fadedStart)) {
				stopped = current->audio;
			}
			if (current->audio) {
				emit loaderOnCancel(current->audio);
				emit faderOnTimer();
			}

			auto foundCurrent = currentIndex(type);
			int index = 0;
			for (; index < AudioSimultaneousLimit; ++index) {
				if (dataForType(type, index)->audio == audio) {
					*foundCurrent = index;
					break;
				}
			}
			if (index == AudioSimultaneousLimit && ++*foundCurrent >= AudioSimultaneousLimit) {
				*foundCurrent -= AudioSimultaneousLimit;
			}
			current = dataForType(type);
		}
		current->audio = audio;
		current->file = audio.audio()->location(true);
		current->data = audio.audio()->data();
		if (current->file.isEmpty() && current->data.isEmpty()) {
			notLoadedYet = true;
			if (audio.type() == AudioMsgId::Type::Song) {
				setStoppedState(current);
			} else {
				setStoppedState(current, AudioPlayerStoppedAtError);
			}
		} else {
			current->playbackState.position = position;
			current->playbackState.state = fadedStart ? AudioPlayerStarting : AudioPlayerPlaying;
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

void Mixer::initFromVideo(uint64 videoPlayId, std_::unique_ptr<VideoSoundData> &&data, int64 position) {
	AudioMsgId stopped;
	{
		QMutexLocker lock(&AudioMutex);

		// Pause current song.
		auto currentSong = dataForType(AudioMsgId::Type::Song);
		float64 suppressGain = suppressSongGain * Global::SongVolume();

		switch (currentSong->playbackState.state) {
		case AudioPlayerStarting:
		case AudioPlayerResuming:
		case AudioPlayerPlaying:
		currentSong->playbackState.state = AudioPlayerPausing;
		updateCurrentStarted(AudioMsgId::Type::Song);
		break;
		case AudioPlayerFinishing: currentSong->playbackState.state = AudioPlayerPausing; break;
		}

		auto type = AudioMsgId::Type::Video;
		auto current = dataForType(type);
		t_assert(current != nullptr);

		if (current->audio) {
			fadedStop(type);
			stopped = current->audio;
			emit loaderOnCancel(current->audio);
		}
		emit faderOnTimer();
		current->clear();
		current->audio = AudioMsgId(AudioMsgId::Type::Video);
		current->videoPlayId = videoPlayId;
		current->videoData = std_::move(data);
		{
			QMutexLocker videoLock(&_lastVideoMutex);
			_lastVideoPlayId = current->videoPlayId;
			_lastVideoPlaybackWhen = 0;
			_lastVideoPlaybackCorrectedMs = 0;
		}
		_loader->startFromVideo(current->videoPlayId);

		current->playbackState.state = AudioPlayerPaused;
		current->loading = true;
		emit loaderOnStart(current->audio, position);
	}
	if (stopped) emit updated(stopped);
}

void Mixer::stopFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto data = dataForType(AudioMsgId::Type::Video);
		t_assert(data != nullptr);

		if (data->videoPlayId != videoPlayId) {
			return;
		}

		current = data->audio;
		fadedStop(AudioMsgId::Type::Video);
		data->clear();
	}
	if (current) emit updated(current);
}

void Mixer::pauseFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto type = AudioMsgId::Type::Video;
		auto data = dataForType(type);
		t_assert(data != nullptr);

		if (data->videoPlayId != videoPlayId) {
			return;
		}

		current = data->audio;
		switch (data->playbackState.state) {
		case AudioPlayerStarting:
		case AudioPlayerResuming:
		case AudioPlayerPlaying: {
			data->playbackState.state = AudioPlayerPaused;
			updateCurrentStarted(type);

			ALint state = AL_INITIAL;
			alGetSourcei(data->source, AL_SOURCE_STATE, &state);
			if (!checkCurrentALError(type)) return;

			if (state == AL_PLAYING) {
				alSourcePause(data->source);
				if (!checkCurrentALError(type)) return;
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
		auto data = dataForType(type);
		t_assert(data != nullptr);

		if (data->videoPlayId != videoPlayId) {
			return;
		}

		float64 suppressGain = suppressSongGain * Global::VideoVolume();

		current = data->audio;
		switch (data->playbackState.state) {
		case AudioPlayerPausing:
		case AudioPlayerPaused:
		case AudioPlayerPausedAtEnd: {
			if (data->playbackState.state == AudioPlayerPaused) {
				updateCurrentStarted(type);
			} else if (data->playbackState.state == AudioPlayerPausedAtEnd) {
				if (alIsSource(data->source)) {
					alSourcei(data->source, AL_SAMPLE_OFFSET, qMax(data->playbackState.position - data->skipStart, 0LL));
					if (!checkCurrentALError(type)) return;
				}
			}
			data->playbackState.state = AudioPlayerPlaying;

			ALint state = AL_INITIAL;
			alGetSourcei(data->source, AL_SOURCE_STATE, &state);
			if (!checkCurrentALError(type)) return;

			if (state != AL_PLAYING) {
				alSourcef(data->source, AL_GAIN, suppressGain);
				if (!checkCurrentALError(type)) return;

				alSourcePlay(data->source);
				if (!checkCurrentALError(type)) return;
			}
		} break;
		}
		emit faderOnTimer();
	}
	if (current) emit updated(current);
}

void Mixer::feedFromVideo(VideoSoundPart &&part) {
	_loader->feedFromVideo(std_::move(part));
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

	auto current = dataForType(type);
	t_assert(current != nullptr);

	if (current->videoPlayId == _lastVideoPlayId && current->playbackState.duration && current->playbackState.frequency) {
		if (current->playbackState.state == AudioPlayerPlaying) {
			_lastVideoPlaybackWhen = getms();
			_lastVideoPlaybackCorrectedMs = (current->playbackState.position * 1000ULL) / current->playbackState.frequency;
		}
	}
}

bool Mixer::checkCurrentALError(AudioMsgId::Type type) {
	if (!Media::Player::PlaybackErrorHappened()) return true;

	auto data = dataForType(type);
	if (!data) {
		setStoppedState(data, AudioPlayerStoppedAtError);
		onError(data->audio);
	}
	return false;
}

void Mixer::pauseresume(AudioMsgId::Type type, bool fast) {
	QMutexLocker lock(&AudioMutex);

	auto current = dataForType(type);
	float64 suppressGain = 1.;
	switch (type) {
	case AudioMsgId::Type::Voice: suppressGain = suppressAllGain; break;
	case AudioMsgId::Type::Song: suppressGain = suppressSongGain * Global::SongVolume(); break;
	case AudioMsgId::Type::Video: suppressGain = suppressSongGain * Global::VideoVolume(); break;
	}

	switch (current->playbackState.state) {
	case AudioPlayerPausing:
	case AudioPlayerPaused:
	case AudioPlayerPausedAtEnd: {
		if (current->playbackState.state == AudioPlayerPaused) {
			updateCurrentStarted(type);
		} else if (current->playbackState.state == AudioPlayerPausedAtEnd) {
			if (alIsSource(current->source)) {
				alSourcei(current->source, AL_SAMPLE_OFFSET, qMax(current->playbackState.position - current->skipStart, 0LL));
				if (!checkCurrentALError(type)) return;
			}
		}
		current->playbackState.state = fast ? AudioPlayerPlaying : AudioPlayerResuming;

		ALint state = AL_INITIAL;
		alGetSourcei(current->source, AL_SOURCE_STATE, &state);
		if (!checkCurrentALError(type)) return;

		if (state != AL_PLAYING) {
			alSourcef(current->source, AL_GAIN, suppressGain);
			if (!checkCurrentALError(type)) return;

			alSourcePlay(current->source);
			if (!checkCurrentALError(type)) return;
		}
		if (type == AudioMsgId::Type::Voice) emit suppressSong();
	} break;
	case AudioPlayerStarting:
	case AudioPlayerResuming:
	case AudioPlayerPlaying:
	current->playbackState.state = AudioPlayerPausing;
	updateCurrentStarted(type);
	if (type == AudioMsgId::Type::Voice) emit unsuppressSong();
	break;
	case AudioPlayerFinishing: current->playbackState.state = AudioPlayerPausing; break;
	}
	emit faderOnTimer();
}

void Mixer::seek(int64 position) {
	QMutexLocker lock(&AudioMutex);

	auto type = AudioMsgId::Type::Song;
	auto current = dataForType(type);
	float64 suppressGain = 1.;
	switch (type) {
	case AudioMsgId::Type::Voice: suppressGain = suppressAllGain; break;
	case AudioMsgId::Type::Song: suppressGain = suppressSongGain * Global::SongVolume(); break;
	}
	auto audio = current->audio;

	bool isSource = alIsSource(current->source);
	bool fastSeek = (position >= current->skipStart && position < current->playbackState.duration - current->skipEnd - (current->skipEnd ? AudioVoiceMsgFrequency : 0));
	if (fastSeek && isSource) {
		alSourcei(current->source, AL_SAMPLE_OFFSET, position - current->skipStart);
		if (!checkCurrentALError(type)) return;
		alSourcef(current->source, AL_GAIN, 1. * suppressGain);
		if (!checkCurrentALError(type)) return;
		updateCurrentStarted(type, position - current->skipStart);
	} else {
		setStoppedState(current);
		if (isSource) alSourceStop(current->source);
	}
	switch (current->playbackState.state) {
	case AudioPlayerPausing:
	case AudioPlayerPaused:
	case AudioPlayerPausedAtEnd: {
		if (current->playbackState.state == AudioPlayerPausedAtEnd) {
			current->playbackState.state = AudioPlayerPaused;
		}
		lock.unlock();
		return pauseresume(type, true);
	} break;
	case AudioPlayerStarting:
	case AudioPlayerResuming:
	case AudioPlayerPlaying:
	current->playbackState.state = AudioPlayerPausing;
	updateCurrentStarted(type);
	if (type == AudioMsgId::Type::Voice) emit unsuppressSong();
	break;
	case AudioPlayerFinishing:
	case AudioPlayerStopped:
	case AudioPlayerStoppedAtEnd:
	case AudioPlayerStoppedAtError:
	case AudioPlayerStoppedAtStart:
	lock.unlock();
	return play(audio, position);
	}
	emit faderOnTimer();
}

void Mixer::stop(AudioMsgId::Type type) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto data = dataForType(type);
		t_assert(data != nullptr);

		current = data->audio;
		fadedStop(type);
		if (type == AudioMsgId::Type::Video) {
			data->clear();
		}
	}
	if (current) emit updated(current);
}

void Mixer::stopAndClear() {
	AudioMsg *current_audio = nullptr, *current_song = nullptr;
	{
		QMutexLocker lock(&AudioMutex);
		if ((current_audio = dataForType(AudioMsgId::Type::Voice))) {
			setStoppedState(current_audio);
		}
		if ((current_song = dataForType(AudioMsgId::Type::Song))) {
			setStoppedState(current_song);
		}
	}
	if (current_song) {
		emit updated(current_song->audio);
	}
	if (current_audio) {
		emit updated(current_audio->audio);
	}
	{
		QMutexLocker lock(&AudioMutex);
		auto clearAndCancel = [this](AudioMsgId::Type type, int index) {
			auto data = dataForType(type, index);
			if (data->audio) {
				emit loaderOnCancel(data->audio);
			}
			data->clear();
		};
		for (int index = 0; index < AudioSimultaneousLimit; ++index) {
			clearAndCancel(AudioMsgId::Type::Voice, index);
			clearAndCancel(AudioMsgId::Type::Song, index);
		}
		_videoData.clear();
		_loader->stopFromVideo();
	}
}

AudioPlaybackState Mixer::currentVideoState(uint64 videoPlayId) {
	QMutexLocker lock(&AudioMutex);
	auto current = dataForType(AudioMsgId::Type::Video);
	if (!current || current->videoPlayId != videoPlayId) return AudioPlaybackState();

	return current->playbackState;
}

AudioPlaybackState Mixer::currentState(AudioMsgId *audio, AudioMsgId::Type type) {
	QMutexLocker lock(&AudioMutex);
	auto current = dataForType(type);
	if (!current) return AudioPlaybackState();

	if (audio) *audio = current->audio;
	return current->playbackState;
}

void Mixer::setStoppedState(AudioMsg *current, AudioPlayerState state) {
	current->playbackState.state = state;
	current->playbackState.position = 0;
}

void Mixer::clearStoppedAtStart(const AudioMsgId &audio) {
	QMutexLocker lock(&AudioMutex);
	auto data = dataForType(audio.type());
	if (data && data->audio == audio && data->playbackState.state == AudioPlayerStoppedAtStart) {
		setStoppedState(data);
	}
}

Fader::Fader(QThread *thread) : QObject()
, _timer(this)
, _suppressAllGain(1., 1.)
, _suppressSongGain(1., 1.) {
	moveToThread(thread);
	_timer.moveToThread(thread);
	_pauseTimer.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(onInit()));
	connect(thread, SIGNAL(finished()), this, SLOT(deleteLater()));

	_timer.setSingleShot(true);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(onTimer()));

	_pauseTimer.setSingleShot(true);
	connect(&_pauseTimer, SIGNAL(timeout()), this, SLOT(onPauseTimer()));
	connect(this, SIGNAL(stopPauseDevice()), this, SLOT(onPauseTimerStop()), Qt::QueuedConnection);
}

void Fader::onInit() {
}

void Fader::onTimer() {
	QMutexLocker lock(&AudioMutex);
	auto player = mixer();
	if (!player) return;

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
			} else if (ms > _suppressAllStart + notifyLengthMs - AudioFadeDuration) {
				if (_suppressAllGain.to() != 1.) _suppressAllGain.start(1.);
				_suppressAllGain.update(1. - ((_suppressAllStart + notifyLengthMs - ms) / float64(AudioFadeDuration)), anim::linear);
			} else if (ms >= _suppressAllStart + st::mediaPlayerSuppressDuration) {
				if (_suppressAllAnim) {
					_suppressAllGain.finish();
					_suppressAllAnim = false;
				}
			} else if (ms > _suppressAllStart) {
				_suppressAllGain.update((ms - _suppressAllStart) / st::mediaPlayerSuppressDuration, anim::linear);
			}
			suppressAllGain = _suppressAllGain.current();
			suppressAudioChanged = (suppressAllGain != wasAudio);
		}
		if (_suppressSongAnim) {
			if (ms >= _suppressSongStart + AudioFadeDuration) {
				_suppressSongGain.finish();
				_suppressSongAnim = false;
			} else {
				_suppressSongGain.update((ms - _suppressSongStart) / float64(AudioFadeDuration), anim::linear);
			}
		}
		suppressSongGain = qMin(suppressAllGain, _suppressSongGain.current());
		suppressSongChanged = (suppressSongGain != wasSong);
	}
	bool hasFading = (_suppressAll || _suppressSongAnim);
	bool hasPlaying = false;

	auto updatePlayback = [this, player, &hasPlaying, &hasFading](AudioMsgId::Type type, int index, float64 suppressGain, bool suppressGainChanged) {
		auto data = player->dataForType(type, index);
		if ((data->playbackState.state & AudioPlayerStoppedMask) || data->playbackState.state == AudioPlayerPaused || !data->source) return;

		int32 emitSignals = updateOnePlayback(data, hasPlaying, hasFading, suppressGain, suppressGainChanged);
		if (emitSignals & EmitError) emit error(data->audio);
		if (emitSignals & EmitStopped) emit audioStopped(data->audio);
		if (emitSignals & EmitPositionUpdated) emit playPositionUpdated(data->audio);
		if (emitSignals & EmitNeedToPreload) emit needToPreload(data->audio);
	};
	auto suppressGainForMusic = suppressSongGain * Global::SongVolume();
	auto suppressGainForMusicChanged = suppressSongChanged || _songVolumeChanged;
	for (int i = 0; i < AudioSimultaneousLimit; ++i) {
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
		_timer.start(AudioFadeTimeout);
		resumeDevice();
	} else if (hasPlaying) {
		_timer.start(AudioCheckPositionTimeout);
		resumeDevice();
	} else {
		QMutexLocker lock(&_pauseMutex);
		_pauseFlag = true;
		_pauseTimer.start(AudioPauseDeviceTimeout);
	}
}

int32 Fader::updateOnePlayback(Mixer::AudioMsg *m, bool &hasPlaying, bool &hasFading, float64 suppressGain, bool suppressGainChanged) {
	bool playing = false, fading = false;

	ALint pos = 0;
	ALint state = AL_INITIAL;
	alGetSourcei(m->source, AL_SAMPLE_OFFSET, &pos);
	if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
	alGetSourcei(m->source, AL_SOURCE_STATE, &state);
	if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }

	int32 emitSignals = 0;
	switch (m->playbackState.state) {
	case AudioPlayerFinishing:
	case AudioPlayerPausing:
	case AudioPlayerStarting:
	case AudioPlayerResuming:
	fading = true;
	break;
	case AudioPlayerPlaying:
	playing = true;
	break;
	}
	if (fading && (state == AL_PLAYING || !m->loading)) {
		if (state != AL_PLAYING) {
			fading = false;
			if (m->source) {
				alSourceStop(m->source);
				if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
				alSourcef(m->source, AL_GAIN, 1);
				if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			}
			if (m->playbackState.state == AudioPlayerPausing) {
				m->playbackState.state = AudioPlayerPausedAtEnd;
			} else {
				setStoppedState(m, AudioPlayerStoppedAtEnd);
			}
			emitSignals |= EmitStopped;
		} else if (1000 * (pos + m->skipStart - m->started) >= AudioFadeDuration * m->playbackState.frequency) {
			fading = false;
			alSourcef(m->source, AL_GAIN, 1. * suppressGain);
			if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			switch (m->playbackState.state) {
			case AudioPlayerFinishing:
			alSourceStop(m->source);
			if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			setStoppedState(m);
			state = AL_STOPPED;
			break;
			case AudioPlayerPausing:
			alSourcePause(m->source);
			if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			m->playbackState.state = AudioPlayerPaused;
			break;
			case AudioPlayerStarting:
			case AudioPlayerResuming:
			m->playbackState.state = AudioPlayerPlaying;
			playing = true;
			break;
			}
		} else {
			float64 newGain = 1000. * (pos + m->skipStart - m->started) / (AudioFadeDuration * m->playbackState.frequency);
			if (m->playbackState.state == AudioPlayerPausing || m->playbackState.state == AudioPlayerFinishing) {
				newGain = 1. - newGain;
			}
			alSourcef(m->source, AL_GAIN, newGain * suppressGain);
			if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
		}
	} else if (playing && (state == AL_PLAYING || !m->loading)) {
		if (state != AL_PLAYING) {
			playing = false;
			if (m->source) {
				alSourceStop(m->source);
				if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
				alSourcef(m->source, AL_GAIN, 1);
				if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			}
			setStoppedState(m, AudioPlayerStoppedAtEnd);
			emitSignals |= EmitStopped;
		} else if (suppressGainChanged) {
			alSourcef(m->source, AL_GAIN, suppressGain);
			if (Media::Player::PlaybackErrorHappened()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
		}
	}
	if (state == AL_PLAYING && pos + m->skipStart - m->playbackState.position >= AudioCheckPositionDelta) {
		m->playbackState.position = pos + m->skipStart;
		emitSignals |= EmitPositionUpdated;
	}
	if (playing || m->playbackState.state == AudioPlayerStarting || m->playbackState.state == AudioPlayerResuming) {
		if (!m->loading && m->skipEnd > 0 && m->playbackState.position + AudioPreloadSamples + m->skipEnd > m->playbackState.duration) {
			m->loading = true;
			emitSignals |= EmitNeedToPreload;
		}
	}
	if (playing) hasPlaying = true;
	if (fading) hasFading = true;

	return emitSignals;
}

void Fader::setStoppedState(Mixer::AudioMsg *m, AudioPlayerState state) {
	m->playbackState.state = state;
	m->playbackState.position = 0;
}

void Fader::onPauseTimer() {
	QMutexLocker lock(&_pauseMutex);
	if (_pauseFlag) {
		_paused = true;
		alcDevicePauseSOFT(AudioDevice);
	}
}

void Fader::onPauseTimerStop() {
	if (_pauseTimer.isActive()) _pauseTimer.stop();
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

void Fader::resumeDevice() {
	QMutexLocker lock(&_pauseMutex);
	_pauseFlag = false;
	emit stopPauseDevice();
	if (_paused) {
		_paused = false;
		alcDeviceResumeSOFT(AudioDevice);
	}
}

} // namespace Player
} // namespace Media

namespace internal {

QMutex *audioPlayerMutex() {
	return &AudioMutex;
}

float64 audioSuppressGain() {
	return suppressAllGain;
}

float64 audioSuppressSongGain() {
	return suppressSongGain;
}

bool audioCheckError() {
	return !Media::Player::PlaybackErrorHappened();
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
		if (duration() < WaveformSamplesCount) {
			return false;
		}

		QVector<uint16> peaks;
		peaks.reserve(WaveformSamplesCount);

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
					sumbytes += WaveformSamplesCount;
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
					sumbytes += sizeof(uint16) * WaveformSamplesCount;
					if (sumbytes >= countbytes) {
						sumbytes -= countbytes;
						peaks.push_back(peak);
						peak = 0;
					}
				}
			}
			processed += sampleSize * samples;
		}
		if (sumbytes > 0 && peaks.size() < WaveformSamplesCount) {
			peaks.push_back(peak);
		}

		if (peaks.isEmpty()) {
			return false;
		}

		int64 sum = std::accumulate(peaks.cbegin(), peaks.cend(), 0ULL);
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
