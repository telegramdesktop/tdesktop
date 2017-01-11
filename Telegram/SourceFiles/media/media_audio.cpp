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
	ALCdevice *audioDevice = 0;
	ALCcontext *audioContext = 0;
	ALuint notifySource = 0;
	ALuint notifyBuffer = 0;

	TimeMs notifyLengthMs = 0;

	QMutex playerMutex;
	AudioPlayer *player = 0;

	float64 suppressAllGain = 1., suppressSongGain = 1.;

	AudioCapture *capture = 0;
}

bool _checkALCError() {
	ALenum errCode;
	if ((errCode = alcGetError(audioDevice)) != ALC_NO_ERROR) {
		LOG(("Audio Error: (alc) %1, %2").arg(errCode).arg((const char *)alcGetString(audioDevice, errCode)));
		return false;
	}
	return true;
}

bool _checkCaptureError(ALCdevice *device) {
	ALenum errCode;
	if ((errCode = alcGetError(device)) != ALC_NO_ERROR) {
		LOG(("Audio Error: (capture) %1, %2").arg(errCode).arg((const char *)alcGetString(audioDevice, errCode)));
		return false;
	}
	return true;
}

bool _checkALError() {
	ALenum errCode;
	if ((errCode = alGetError()) != AL_NO_ERROR) {
		LOG(("Audio Error: (al) %1, %2").arg(errCode).arg((const char *)alGetString(errCode)));
		return false;
	}
	return true;
}

Q_DECLARE_METATYPE(AudioMsgId);
Q_DECLARE_METATYPE(VoiceWaveform);
void audioInit() {
	if (!capture) {
		capture = new AudioCapture();
		cSetHasAudioCapture(capture->check());
	}

	if (audioDevice) return;

	audioDevice = alcOpenDevice(0);
	if (!audioDevice) {
		LOG(("Audio Error: default sound device not present."));
		return;
	}

	ALCint attributes[] = { ALC_STEREO_SOURCES, 8, 0 };
	audioContext = alcCreateContext(audioDevice, attributes);
	alcMakeContextCurrent(audioContext);
	if (!_checkALCError()) return audioFinish();

	ALfloat v[] = { 0.f, 0.f, -1.f, 0.f, 1.f, 0.f };
	alListener3f(AL_POSITION, 0.f, 0.f, 0.f);
	alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f);
	alListenerfv(AL_ORIENTATION, v);

	alDistanceModel(AL_NONE);

	alGenSources(1, &notifySource);
	alSourcef(notifySource, AL_PITCH, 1.f);
	alSourcef(notifySource, AL_GAIN, 1.f);
	alSource3f(notifySource, AL_POSITION, 0, 0, 0);
	alSource3f(notifySource, AL_VELOCITY, 0, 0, 0);
	alSourcei(notifySource, AL_LOOPING, 0);

	alGenBuffers(1, &notifyBuffer);
	if (!_checkALError()) return audioFinish();

	QFile notify(":/gui/art/newmsg.wav");
	if (!notify.open(QIODevice::ReadOnly)) return audioFinish();

	QByteArray blob = notify.readAll();
	const char *data = blob.constData();
	if (blob.size() < 44) return audioFinish();

	if (*((const uint32*)(data + 0)) != 0x46464952) return audioFinish(); // ChunkID - "RIFF"
	if (*((const uint32*)(data + 4)) != uint32(blob.size() - 8)) return audioFinish(); // ChunkSize
	if (*((const uint32*)(data + 8)) != 0x45564157) return audioFinish(); // Format - "WAVE"
	if (*((const uint32*)(data + 12)) != 0x20746d66) return audioFinish(); // Subchunk1ID - "fmt "
	uint32 subchunk1Size = *((const uint32*)(data + 16)), extra = subchunk1Size - 16;
	if (subchunk1Size < 16 || (extra && extra < 2)) return audioFinish();
	if (*((const uint16*)(data + 20)) != 1) return audioFinish(); // AudioFormat - PCM (1)

	uint16 numChannels = *((const uint16*)(data + 22));
	if (numChannels != 1 && numChannels != 2) return audioFinish();

	uint32 sampleRate = *((const uint32*)(data + 24));
	uint32 byteRate = *((const uint32*)(data + 28));

	uint16 blockAlign = *((const uint16*)(data + 32));
	uint16 bitsPerSample = *((const uint16*)(data + 34));
	if (bitsPerSample % 8) return audioFinish();
	uint16 bytesPerSample = bitsPerSample / 8;
	if (bytesPerSample != 1 && bytesPerSample != 2) return audioFinish();

	if (blockAlign != numChannels * bytesPerSample) return audioFinish();
	if (byteRate != sampleRate * blockAlign) return audioFinish();

	if (extra) {
		uint16 extraSize = *((const uint16*)(data + 36));
        if (uint32(extraSize + 2) != extra) return audioFinish();
		if (uint32(blob.size()) < 44 + extra) return audioFinish();
	}

	if (*((const uint32*)(data + extra + 36)) != 0x61746164) return audioFinish(); // Subchunk2ID - "data"
	uint32 subchunk2Size = *((const uint32*)(data + extra + 40));
	if (subchunk2Size % (numChannels * bytesPerSample)) return audioFinish();
	uint32 numSamples = subchunk2Size / (numChannels * bytesPerSample);

	if (uint32(blob.size()) < 44 + extra + subchunk2Size) return audioFinish();
	data += 44 + extra;

	ALenum format = 0;
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
	if (!format) return audioFinish();

	int32 addBytes = (sampleRate * 15 / 100) * bytesPerSample * numChannels; // add 150ms of silence
	QByteArray fullData(addBytes + subchunk2Size, (bytesPerSample == 1) ? 128 : 0);
	memcpy(fullData.data() + addBytes, data, subchunk2Size);
	alBufferData(notifyBuffer, format, fullData.constData(), fullData.size(), sampleRate);
	alSourcei(notifySource, AL_BUFFER, notifyBuffer);

	notifyLengthMs = (numSamples * 1000ULL / sampleRate);

	if (!_checkALError()) return audioFinish();

	qRegisterMetaType<AudioMsgId>();
	qRegisterMetaType<VoiceWaveform>();

	player = new AudioPlayer();
	alcDevicePauseSOFT(audioDevice);

	cSetHasAudioPlayer(true);
}

void audioPlayNotify() {
	if (!audioPlayer()) return;

	audioPlayer()->resumeDevice();
	alSourcePlay(notifySource);
	emit audioPlayer()->suppressAll();
	emit audioPlayer()->faderOnTimer();
}

// can be called at any moment when audio error
void audioFinish() {
	if (player) {
		delete player;
		player = nullptr;
	}
	if (capture) {
		delete capture;
		capture = nullptr;
	}

	alSourceStop(notifySource);
	if (alIsBuffer(notifyBuffer)) {
		alDeleteBuffers(1, &notifyBuffer);
		notifyBuffer = 0;
	}
	if (alIsSource(notifySource)) {
		alDeleteSources(1, &notifySource);
		notifySource = 0;
	}

	if (audioContext) {
		alcMakeContextCurrent(nullptr);
		alcDestroyContext(audioContext);
		audioContext = nullptr;
	}

	if (audioDevice) {
		alcCloseDevice(audioDevice);
		audioDevice = nullptr;
	}

	cSetHasAudioCapture(false);
	cSetHasAudioPlayer(false);
}

void AudioPlayer::AudioMsg::clear() {
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

AudioPlayer::AudioPlayer()
: _fader(new AudioPlayerFader(&_faderThread))
, _loader(new AudioPlayerLoaders(&_loaderThread)) {
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
	connect(this, SIGNAL(loaderOnStart(const AudioMsgId&,qint64)), _loader, SLOT(onStart(const AudioMsgId&,qint64)));
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

AudioPlayer::~AudioPlayer() {
	{
		QMutexLocker lock(&playerMutex);
		player = nullptr;
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

void AudioPlayer::onUpdated(const AudioMsgId &audio) {
	if (audio.type() == AudioMsgId::Type::Video) {
		videoSoundProgress(audio);
	}
	notify(audio);
}

void AudioPlayer::onError(const AudioMsgId &audio) {
	emit stoppedOnError(audio);
	if (audio.type() == AudioMsgId::Type::Voice) {
		emit unsuppressSong();
	}
}

void AudioPlayer::onStopped(const AudioMsgId &audio) {
	emit updated(audio);
	if (audio.type() == AudioMsgId::Type::Voice) {
		emit unsuppressSong();
	}
}

AudioPlayer::AudioMsg *AudioPlayer::dataForType(AudioMsgId::Type type, int index) {
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

const AudioPlayer::AudioMsg *AudioPlayer::dataForType(AudioMsgId::Type type, int index) const {
	return const_cast<AudioPlayer*>(this)->dataForType(type, index);
}

int *AudioPlayer::currentIndex(AudioMsgId::Type type) {
	switch (type) {
	case AudioMsgId::Type::Voice: return &_audioCurrent;
	case AudioMsgId::Type::Song: return &_songCurrent;
	case AudioMsgId::Type::Video: { static int videoIndex = 0; return &videoIndex; }
	}
	return nullptr;
}

const int *AudioPlayer::currentIndex(AudioMsgId::Type type) const {
	return const_cast<AudioPlayer*>(this)->currentIndex(type);
}

bool AudioPlayer::updateCurrentStarted(AudioMsgId::Type type, int32 pos) {
	auto data = dataForType(type);
	if (!data) return false;

	if (pos < 0) {
		if (alIsSource(data->source)) {
			alGetSourcei(data->source, AL_SAMPLE_OFFSET, &pos);
		} else {
			pos = 0;
		}
		if (!_checkALError()) {
			setStoppedState(data, AudioPlayerStoppedAtError);
			onError(data->audio);
			return false;
		}
	}
	data->started = data->playbackState.position = pos + data->skipStart;
	return true;
}

bool AudioPlayer::fadedStop(AudioMsgId::Type type, bool *fadedStart) {
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

void AudioPlayer::play(const AudioMsgId &audio, int64 position) {
	auto type = audio.type();
	AudioMsgId stopped;
	auto notLoadedYet = false;
	{
		QMutexLocker lock(&playerMutex);

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

void AudioPlayer::initFromVideo(uint64 videoPlayId, std_::unique_ptr<VideoSoundData> &&data, int64 position) {
	AudioMsgId stopped;
	{
		QMutexLocker lock(&playerMutex);

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

void AudioPlayer::stopFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&playerMutex);
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

void AudioPlayer::pauseFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&playerMutex);
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

void AudioPlayer::resumeFromVideo(uint64 videoPlayId) {
	AudioMsgId current;
	{
		QMutexLocker lock(&playerMutex);
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
				audioPlayer()->resumeDevice();

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

void AudioPlayer::feedFromVideo(VideoSoundPart &&part) {
	_loader->feedFromVideo(std_::move(part));
}

TimeMs AudioPlayer::getVideoCorrectedTime(uint64 playId, TimeMs frameMs, TimeMs systemMs) {
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

void AudioPlayer::videoSoundProgress(const AudioMsgId &audio) {
	auto type = audio.type();
	t_assert(type == AudioMsgId::Type::Video);

	QMutexLocker lock(&playerMutex);
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

bool AudioPlayer::checkCurrentALError(AudioMsgId::Type type) {
	if (_checkALError()) return true;

	auto data = dataForType(type);
	if (!data) {
		setStoppedState(data, AudioPlayerStoppedAtError);
		onError(data->audio);
	}
	return false;
}

void AudioPlayer::pauseresume(AudioMsgId::Type type, bool fast) {
	QMutexLocker lock(&playerMutex);

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
			audioPlayer()->resumeDevice();

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

void AudioPlayer::seek(int64 position) {
	QMutexLocker lock(&playerMutex);

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

void AudioPlayer::stop(AudioMsgId::Type type) {
	AudioMsgId current;
	{
		QMutexLocker lock(&playerMutex);
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

void AudioPlayer::stopAndClear() {
	AudioMsg *current_audio = nullptr, *current_song = nullptr;
	{
		QMutexLocker lock(&playerMutex);
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
		QMutexLocker lock(&playerMutex);
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

AudioPlaybackState AudioPlayer::currentVideoState(uint64 videoPlayId) {
	QMutexLocker lock(&playerMutex);
	auto current = dataForType(AudioMsgId::Type::Video);
	if (!current || current->videoPlayId != videoPlayId) return AudioPlaybackState();

	return current->playbackState;
}

AudioPlaybackState AudioPlayer::currentState(AudioMsgId *audio, AudioMsgId::Type type) {
	QMutexLocker lock(&playerMutex);
	auto current = dataForType(type);
	if (!current) return AudioPlaybackState();

	if (audio) *audio = current->audio;
	return current->playbackState;
}

void AudioPlayer::setStoppedState(AudioMsg *current, AudioPlayerState state) {
	current->playbackState.state = state;
	current->playbackState.position = 0;
}

void AudioPlayer::clearStoppedAtStart(const AudioMsgId &audio) {
	QMutexLocker lock(&playerMutex);
	auto data = dataForType(audio.type());
	if (data && data->audio == audio && data->playbackState.state == AudioPlayerStoppedAtStart) {
		setStoppedState(data);
	}
}

void AudioPlayer::resumeDevice() {
	_fader->resumeDevice();
}


namespace internal {

QMutex *audioPlayerMutex() {
	return &playerMutex;
}

float64 audioSuppressGain() {
	return suppressAllGain;
}

float64 audioSuppressSongGain() {
	return suppressSongGain;
}

bool audioCheckError() {
	return _checkALError();
}

} // namespace internal

AudioCapture::AudioCapture() : _capture(new AudioCaptureInner(&_captureThread)) {
	connect(this, SIGNAL(start()), _capture, SLOT(onStart()));
	connect(this, SIGNAL(stop(bool)), _capture, SLOT(onStop(bool)));
	connect(_capture, SIGNAL(done(QByteArray,VoiceWaveform,qint32)), this, SIGNAL(done(QByteArray,VoiceWaveform,qint32)));
	connect(_capture, SIGNAL(updated(quint16,qint32)), this, SIGNAL(updated(quint16,qint32)));
	connect(_capture, SIGNAL(error()), this, SIGNAL(error()));
	connect(&_captureThread, SIGNAL(started()), _capture, SLOT(onInit()));
	connect(&_captureThread, SIGNAL(finished()), _capture, SLOT(deleteLater()));
	_captureThread.start();
}

bool AudioCapture::check() {
	if (auto defaultDevice = alcGetString(0, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER)) {
		if (auto device = alcCaptureOpenDevice(defaultDevice, AudioVoiceMsgFrequency, AL_FORMAT_MONO16, AudioVoiceMsgFrequency / 5)) {
			alcCaptureCloseDevice(device);
			return _checkALCError();
		}
	}
	return false;
}

AudioCapture::~AudioCapture() {
	capture = nullptr;
	_captureThread.quit();
	_captureThread.wait();
}

AudioPlayer *audioPlayer() {
	return player;
}

AudioCapture *audioCapture() {
	return capture;
}

AudioPlayerFader::AudioPlayerFader(QThread *thread) : QObject()
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

void AudioPlayerFader::onInit() {
}

void AudioPlayerFader::onTimer() {
	QMutexLocker lock(&playerMutex);
	AudioPlayer *voice = audioPlayer();
	if (!voice) return;

	bool suppressAudioChanged = false, suppressSongChanged = false;
	if (_suppressAll || _suppressSongAnim) {
		auto ms = getms();
		float64 wasSong = suppressSongGain;
		if (_suppressAll) {
			float64 wasAudio = suppressAllGain;
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

	auto updatePlayback = [this, voice, &hasPlaying, &hasFading](AudioMsgId::Type type, int index, float64 suppressGain, bool suppressGainChanged) {
		auto data = voice->dataForType(type, index);
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

	if (!hasFading) {
		if (!hasPlaying) {
			ALint state = AL_INITIAL;
			alGetSourcei(notifySource, AL_SOURCE_STATE, &state);
			if (_checkALError() && state == AL_PLAYING) {
				hasPlaying = true;
			}
		}
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

int32 AudioPlayerFader::updateOnePlayback(AudioPlayer::AudioMsg *m, bool &hasPlaying, bool &hasFading, float64 suppressGain, bool suppressGainChanged) {
	bool playing = false, fading = false;

	ALint pos = 0;
	ALint state = AL_INITIAL;
	alGetSourcei(m->source, AL_SAMPLE_OFFSET, &pos);
	if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
	alGetSourcei(m->source, AL_SOURCE_STATE, &state);
	if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }

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
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
				alSourcef(m->source, AL_GAIN, 1);
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
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
			if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			switch (m->playbackState.state) {
			case AudioPlayerFinishing:
				alSourceStop(m->source);
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
				setStoppedState(m);
				state = AL_STOPPED;
				break;
			case AudioPlayerPausing:
				alSourcePause(m->source);
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
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
			if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
		}
	} else if (playing && (state == AL_PLAYING || !m->loading)) {
		if (state != AL_PLAYING) {
			playing = false;
			if (m->source) {
				alSourceStop(m->source);
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
				alSourcef(m->source, AL_GAIN, 1);
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			}
			setStoppedState(m, AudioPlayerStoppedAtEnd);
			emitSignals |= EmitStopped;
		} else if (suppressGainChanged) {
			alSourcef(m->source, AL_GAIN, suppressGain);
			if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
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

void AudioPlayerFader::setStoppedState(AudioPlayer::AudioMsg *m, AudioPlayerState state) {
	m->playbackState.state = state;
	m->playbackState.position = 0;
}

void AudioPlayerFader::onPauseTimer() {
	QMutexLocker lock(&_pauseMutex);
	if (_pauseFlag) {
		_paused = true;
		alcDevicePauseSOFT(audioDevice);
	}
}

void AudioPlayerFader::onPauseTimerStop() {
	if (_pauseTimer.isActive()) _pauseTimer.stop();
}

void AudioPlayerFader::onSuppressSong() {
	if (!_suppressSong) {
		_suppressSong = true;
		_suppressSongAnim = true;
		_suppressSongStart = getms();
		_suppressSongGain.start(st::suppressSong);
		onTimer();
	}
}

void AudioPlayerFader::onUnsuppressSong() {
	if (_suppressSong) {
		_suppressSong = false;
		_suppressSongAnim = true;
		_suppressSongStart = getms();
		_suppressSongGain.start(1.);
		onTimer();
	}
}

void AudioPlayerFader::onSuppressAll() {
	_suppressAll = true;
	_suppressAllStart = getms();
	_suppressAllGain.start(st::suppressAll);
	onTimer();
}

void AudioPlayerFader::onSongVolumeChanged() {
	_songVolumeChanged = true;
	onTimer();
}

void AudioPlayerFader::onVideoVolumeChanged() {
	_videoVolumeChanged = true;
	onTimer();
}

void AudioPlayerFader::resumeDevice() {
	QMutexLocker lock(&_pauseMutex);
	_pauseFlag = false;
	emit stopPauseDevice();
	if (_paused) {
		_paused = false;
		alcDeviceResumeSOFT(audioDevice);
	}
}

struct AudioCapturePrivate {
	AudioCapturePrivate()
		: device(0)
		, fmt(0)
		, ioBuffer(0)
		, ioContext(0)
		, fmtContext(0)
		, stream(0)
		, codec(0)
		, codecContext(0)
		, opened(false)
		, srcSamples(0)
		, dstSamples(0)
		, maxDstSamples(0)
		, dstSamplesSize(0)
		, fullSamples(0)
		, srcSamplesData(0)
		, dstSamplesData(0)
		, swrContext(0)
		, lastUpdate(0)
		, levelMax(0)
		, dataPos(0)
		, waveformMod(0)
		, waveformEach(AudioVoiceMsgFrequency / 100)
		, waveformPeak(0) {
	}
	ALCdevice *device;
	AVOutputFormat *fmt;
	uchar *ioBuffer;
	AVIOContext *ioContext;
	AVFormatContext *fmtContext;
	AVStream *stream;
	AVCodec *codec;
	AVCodecContext *codecContext;
	bool opened;

	int32 srcSamples, dstSamples, maxDstSamples, dstSamplesSize, fullSamples;
	uint8_t **srcSamplesData, **dstSamplesData;
	SwrContext *swrContext;

	int32 lastUpdate;
	uint16 levelMax;

	QByteArray data;
	int32 dataPos;

	int64 waveformMod, waveformEach;
	uint16 waveformPeak;
	QVector<uchar> waveform;

	static int _read_data(void *opaque, uint8_t *buf, int buf_size) {
		AudioCapturePrivate *l = reinterpret_cast<AudioCapturePrivate*>(opaque);

		int32 nbytes = qMin(l->data.size() - l->dataPos, int32(buf_size));
		if (nbytes <= 0) {
			return 0;
		}

		memcpy(buf, l->data.constData() + l->dataPos, nbytes);
		l->dataPos += nbytes;
		return nbytes;
	}

	static int _write_data(void *opaque, uint8_t *buf, int buf_size) {
		AudioCapturePrivate *l = reinterpret_cast<AudioCapturePrivate*>(opaque);

		if (buf_size <= 0) return 0;
		if (l->dataPos + buf_size > l->data.size()) l->data.resize(l->dataPos + buf_size);
		memcpy(l->data.data() + l->dataPos, buf, buf_size);
		l->dataPos += buf_size;
		return buf_size;
	}

	static int64_t _seek_data(void *opaque, int64_t offset, int whence) {
		AudioCapturePrivate *l = reinterpret_cast<AudioCapturePrivate*>(opaque);

		int32 newPos = -1;
		switch (whence) {
		case SEEK_SET: newPos = offset; break;
		case SEEK_CUR: newPos = l->dataPos + offset; break;
		case SEEK_END: newPos = l->data.size() + offset; break;
		}
		if (newPos < 0) {
			return -1;
		}
		l->dataPos = newPos;
		return l->dataPos;
	}
};

AudioCaptureInner::AudioCaptureInner(QThread *thread) : d(new AudioCapturePrivate()) {
	moveToThread(thread);
	_timer.moveToThread(thread);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(onTimeout()));
}

AudioCaptureInner::~AudioCaptureInner() {
	onStop(false);
	delete d;
}

void AudioCaptureInner::onInit() {
}

void AudioCaptureInner::onStart() {

	// Start OpenAL Capture
    const ALCchar *dName = alcGetString(0, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
    DEBUG_LOG(("Audio Info: Capture device name '%1'").arg(dName));
    d->device = alcCaptureOpenDevice(dName, AudioVoiceMsgFrequency, AL_FORMAT_MONO16, AudioVoiceMsgFrequency / 5);
	if (!d->device) {
		LOG(("Audio Error: capture device not present!"));
		emit error();
		return;
	}
	alcCaptureStart(d->device);
	if (!_checkCaptureError(d->device)) {
		alcCaptureCloseDevice(d->device);
		d->device = 0;
		emit error();
		return;
	}

	// Create encoding context

	d->ioBuffer = (uchar*)av_malloc(AVBlockSize);

	d->ioContext = avio_alloc_context(d->ioBuffer, AVBlockSize, 1, static_cast<void*>(d), &AudioCapturePrivate::_read_data, &AudioCapturePrivate::_write_data, &AudioCapturePrivate::_seek_data);
	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
	AVOutputFormat *fmt = 0;
	while ((fmt = av_oformat_next(fmt))) {
		if (fmt->name == qstr("opus")) {
			break;
		}
	}
	if (!fmt) {
		LOG(("Audio Error: Unable to find opus AVOutputFormat for capture"));
		onStop(false);
		emit error();
		return;
	}

	if ((res = avformat_alloc_output_context2(&d->fmtContext, fmt, 0, 0)) < 0) {
		LOG(("Audio Error: Unable to avformat_alloc_output_context2 for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}
	d->fmtContext->pb = d->ioContext;
	d->fmtContext->flags |= AVFMT_FLAG_CUSTOM_IO;
	d->opened = true;

	// Add audio stream
	d->codec = avcodec_find_encoder(fmt->audio_codec);
	if (!d->codec) {
		LOG(("Audio Error: Unable to avcodec_find_encoder for capture"));
		onStop(false);
		emit error();
		return;
	}
    d->stream = avformat_new_stream(d->fmtContext, d->codec);
	if (!d->stream) {
		LOG(("Audio Error: Unable to avformat_new_stream for capture"));
		onStop(false);
		emit error();
		return;
	}
	d->stream->id = d->fmtContext->nb_streams - 1;
	d->codecContext = avcodec_alloc_context3(d->codec);
	if (!d->codecContext) {
		LOG(("Audio Error: Unable to avcodec_alloc_context3 for capture"));
		onStop(false);
		emit error();
		return;
	}

	av_opt_set_int(d->codecContext, "refcounted_frames", 1, 0);

	d->codecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    d->codecContext->bit_rate = 64000;
    d->codecContext->channel_layout = AV_CH_LAYOUT_MONO;
	d->codecContext->sample_rate = AudioVoiceMsgFrequency;
	d->codecContext->channels = 1;

	if (d->fmtContext->oformat->flags & AVFMT_GLOBALHEADER) {
		d->codecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}

	// Open audio stream
	if ((res = avcodec_open2(d->codecContext, d->codec, nullptr)) < 0) {
		LOG(("Audio Error: Unable to avcodec_open2 for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	// Alloc source samples

	d->srcSamples = (d->codecContext->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE) ? 10000 : d->codecContext->frame_size;
	//if ((res = av_samples_alloc_array_and_samples(&d->srcSamplesData, 0, d->codecContext->channels, d->srcSamples, d->codecContext->sample_fmt, 0)) < 0) {
	//	LOG(("Audio Error: Unable to av_samples_alloc_array_and_samples for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
	//	onStop(false);
	//	emit error();
	//	return;
	//}
	// Using _captured directly

	// Prepare resampling
	d->swrContext = swr_alloc();
	if (!d->swrContext) {
		fprintf(stderr, "Could not allocate resampler context\n");
		exit(1);
	}

	av_opt_set_int(d->swrContext, "in_channel_count", d->codecContext->channels, 0);
	av_opt_set_int(d->swrContext, "in_sample_rate", d->codecContext->sample_rate, 0);
	av_opt_set_sample_fmt(d->swrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
	av_opt_set_int(d->swrContext, "out_channel_count", d->codecContext->channels, 0);
	av_opt_set_int(d->swrContext, "out_sample_rate", d->codecContext->sample_rate, 0);
	av_opt_set_sample_fmt(d->swrContext, "out_sample_fmt", d->codecContext->sample_fmt, 0);

	if ((res = swr_init(d->swrContext)) < 0) {
		LOG(("Audio Error: Unable to swr_init for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	d->maxDstSamples = d->srcSamples;
	if ((res = av_samples_alloc_array_and_samples(&d->dstSamplesData, 0, d->codecContext->channels, d->maxDstSamples, d->codecContext->sample_fmt, 0)) < 0) {
		LOG(("Audio Error: Unable to av_samples_alloc_array_and_samples for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}
	d->dstSamplesSize = av_samples_get_buffer_size(0, d->codecContext->channels, d->maxDstSamples, d->codecContext->sample_fmt, 0);

	if ((res = avcodec_parameters_from_context(d->stream->codecpar, d->codecContext)) < 0) {
		LOG(("Audio Error: Unable to avcodec_parameters_from_context for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	// Write file header
	if ((res = avformat_write_header(d->fmtContext, 0)) < 0) {
		LOG(("Audio Error: Unable to avformat_write_header for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	_timer.start(50);
	_captured.clear();
	_captured.reserve(AudioVoiceMsgBufferSize);
	DEBUG_LOG(("Audio Capture: started!"));
}

void AudioCaptureInner::onStop(bool needResult) {
	if (!_timer.isActive()) return; // in onStop() already
	_timer.stop();

    if (d->device) {
        alcCaptureStop(d->device);
        onTimeout(); // get last data
    }

	// Write what is left
	if (!_captured.isEmpty()) {
		int32 fadeSamples = AudioVoiceMsgFade * AudioVoiceMsgFrequency / 1000, capturedSamples = _captured.size() / sizeof(short);
		if ((_captured.size() % sizeof(short)) || (d->fullSamples + capturedSamples < AudioVoiceMsgFrequency) || (capturedSamples < fadeSamples)) {
			d->fullSamples = 0;
			d->dataPos = 0;
			d->data.clear();
			d->waveformMod = 0;
			d->waveformPeak = 0;
			d->waveform.clear();
		} else {
			float64 coef = 1. / fadeSamples, fadedFrom = 0;
			for (short *ptr = ((short*)_captured.data()) + capturedSamples, *end = ptr - fadeSamples; ptr != end; ++fadedFrom) {
				--ptr;
				*ptr = qRound(fadedFrom * coef * *ptr);
			}
			if (capturedSamples % d->srcSamples) {
				int32 s = _captured.size();
				_captured.resize(s + (d->srcSamples - (capturedSamples % d->srcSamples)) * sizeof(short));
				memset(_captured.data() + s, 0, _captured.size() - s);
			}

			int32 framesize = d->srcSamples * d->codecContext->channels * sizeof(short), encoded = 0;
			while (_captured.size() >= encoded + framesize) {
				processFrame(encoded, framesize);
				encoded += framesize;
			}
			writeFrame(nullptr); // drain the codec
			if (encoded != _captured.size()) {
				d->fullSamples = 0;
				d->dataPos = 0;
				d->data.clear();
				d->waveformMod = 0;
				d->waveformPeak = 0;
				d->waveform.clear();
			}
		}
	}
	DEBUG_LOG(("Audio Capture: stopping (need result: %1), size: %2, samples: %3").arg(Logs::b(needResult)).arg(d->data.size()).arg(d->fullSamples));
	_captured = QByteArray();

	// Finish stream
	if (d->device) {
		av_write_trailer(d->fmtContext);
	}

	QByteArray result = d->fullSamples ? d->data : QByteArray();
	VoiceWaveform waveform;
	qint32 samples = d->fullSamples;
	if (samples && !d->waveform.isEmpty()) {
		int64 count = d->waveform.size(), sum = 0;
		if (count >= WaveformSamplesCount) {
			QVector<uint16> peaks;
			peaks.reserve(WaveformSamplesCount);

			uint16 peak = 0;
			for (int32 i = 0; i < count; ++i) {
				uint16 sample = uint16(d->waveform.at(i)) * 256;
				if (peak < sample) {
					peak = sample;
				}
				sum += WaveformSamplesCount;
				if (sum >= count) {
					sum -= count;
					peaks.push_back(peak);
					peak = 0;
				}
			}

			int64 sum = std::accumulate(peaks.cbegin(), peaks.cend(), 0ULL);
			peak = qMax(int32(sum * 1.8 / peaks.size()), 2500);

			waveform.resize(peaks.size());
			for (int32 i = 0, l = peaks.size(); i != l; ++i) {
				waveform[i] = char(qMin(31U, uint32(qMin(peaks.at(i), peak)) * 31 / peak));
			}
		}
	}
	if (d->device) {
		alcCaptureStop(d->device);
		alcCaptureCloseDevice(d->device);
		d->device = nullptr;

		if (d->codecContext) {
			avcodec_free_context(&d->codecContext);
			d->codecContext = nullptr;
		}
		if (d->srcSamplesData) {
			if (d->srcSamplesData[0]) {
				av_freep(&d->srcSamplesData[0]);
			}
			av_freep(&d->srcSamplesData);
		}
		if (d->dstSamplesData) {
			if (d->dstSamplesData[0]) {
				av_freep(&d->dstSamplesData[0]);
			}
			av_freep(&d->dstSamplesData);
		}
		d->fullSamples = 0;
		if (d->swrContext) {
			swr_free(&d->swrContext);
			d->swrContext = nullptr;
		}
		if (d->opened) {
			avformat_close_input(&d->fmtContext);
			d->opened = false;
		}
		if (d->ioContext) {
			av_freep(&d->ioContext->buffer);
			av_freep(&d->ioContext);
			d->ioBuffer = nullptr;
		} else if (d->ioBuffer) {
			av_freep(&d->ioBuffer);
		}
		if (d->fmtContext) {
			avformat_free_context(d->fmtContext);
			d->fmtContext = nullptr;
		}
		d->fmt = nullptr;
		d->stream = nullptr;
		d->codec = nullptr;

		d->lastUpdate = 0;
		d->levelMax = 0;

		d->dataPos = 0;
		d->data.clear();

		d->waveformMod = 0;
		d->waveformPeak = 0;
		d->waveform.clear();
	}
	if (needResult) emit done(result, waveform, samples);
}

void AudioCaptureInner::onTimeout() {
	if (!d->device) {
		_timer.stop();
		return;
	}
	ALint samples;
	alcGetIntegerv(d->device, ALC_CAPTURE_SAMPLES, sizeof(samples), &samples);
	if (!_checkCaptureError(d->device)) {
		onStop(false);
		emit error();
		return;
	}
	if (samples > 0) {
		// Get samples from OpenAL
		int32 s = _captured.size(), news = s + samples * sizeof(short);
		if (news / AudioVoiceMsgBufferSize > s / AudioVoiceMsgBufferSize) {
			_captured.reserve(((news / AudioVoiceMsgBufferSize) + 1) * AudioVoiceMsgBufferSize);
		}
		_captured.resize(news);
		alcCaptureSamples(d->device, (ALCvoid *)(_captured.data() + s), samples);
		if (!_checkCaptureError(d->device)) {
			onStop(false);
			emit error();
			return;
		}

		// Count new recording level and update view
		int32 skipSamples = AudioVoiceMsgSkip * AudioVoiceMsgFrequency / 1000, fadeSamples = AudioVoiceMsgFade * AudioVoiceMsgFrequency / 1000;
		int32 levelindex = d->fullSamples + (s / sizeof(short));
		for (const short *ptr = (const short*)(_captured.constData() + s), *end = (const short*)(_captured.constData() + news); ptr < end; ++ptr, ++levelindex) {
			if (levelindex > skipSamples) {
				uint16 value = qAbs(*ptr);
				if (levelindex < skipSamples + fadeSamples) {
					value = qRound(value * float64(levelindex - skipSamples) / fadeSamples);
				}
				if (d->levelMax < value) {
					d->levelMax = value;
				}
			}
		}
		qint32 samplesFull = d->fullSamples + _captured.size() / sizeof(short), samplesSinceUpdate = samplesFull - d->lastUpdate;
		if (samplesSinceUpdate > AudioVoiceMsgUpdateView * AudioVoiceMsgFrequency / 1000) {
			emit updated(d->levelMax, samplesFull);
			d->lastUpdate = samplesFull;
			d->levelMax = 0;
		}
		// Write frames
		int32 framesize = d->srcSamples * d->codecContext->channels * sizeof(short), encoded = 0;
		while (uint32(_captured.size()) >= encoded + framesize + fadeSamples * sizeof(short)) {
			processFrame(encoded, framesize);
			encoded += framesize;
		}

		// Collapse the buffer
		if (encoded > 0) {
			int32 goodSize = _captured.size() - encoded;
			memmove(_captured.data(), _captured.constData() + encoded, goodSize);
			_captured.resize(goodSize);
		}
	} else {
		DEBUG_LOG(("Audio Capture: no samples to capture."));
	}
}

void AudioCaptureInner::processFrame(int32 offset, int32 framesize) {
	// Prepare audio frame

	if (framesize % sizeof(short)) { // in the middle of a sample
		LOG(("Audio Error: Bad framesize in writeFrame() for capture, framesize %1, %2").arg(framesize));
		onStop(false);
		emit error();
		return;
	}
	int32 samplesCnt = framesize / sizeof(short);

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	auto srcSamplesDataChannel = (short*)(_captured.data() + offset);
	auto srcSamplesData = &srcSamplesDataChannel;

//	memcpy(d->srcSamplesData[0], _captured.constData() + offset, framesize);
	int32 skipSamples = AudioVoiceMsgSkip * AudioVoiceMsgFrequency / 1000, fadeSamples = AudioVoiceMsgFade * AudioVoiceMsgFrequency / 1000;
	if (d->fullSamples < skipSamples + fadeSamples) {
		int32 fadedCnt = qMin(samplesCnt, skipSamples + fadeSamples - d->fullSamples);
		float64 coef = 1. / fadeSamples, fadedFrom = d->fullSamples - skipSamples;
		short *ptr = srcSamplesDataChannel, *zeroEnd = ptr + qMin(samplesCnt, qMax(0, skipSamples - d->fullSamples)), *end = ptr + fadedCnt;
		for (; ptr != zeroEnd; ++ptr, ++fadedFrom) {
			*ptr = 0;
		}
		for (; ptr != end; ++ptr, ++fadedFrom) {
			*ptr = qRound(fadedFrom * coef * *ptr);
		}
	}

	d->waveform.reserve(d->waveform.size() + (samplesCnt / d->waveformEach) + 1);
	for (short *ptr = srcSamplesDataChannel, *end = ptr + samplesCnt; ptr != end; ++ptr) {
		uint16 value = qAbs(*ptr);
		if (d->waveformPeak < value) {
			d->waveformPeak = value;
		}
		if (++d->waveformMod == d->waveformEach) {
			d->waveformMod -= d->waveformEach;
			d->waveform.push_back(uchar(d->waveformPeak / 256));
			d->waveformPeak = 0;
		}
	}

	// Convert to final format

	d->dstSamples = av_rescale_rnd(swr_get_delay(d->swrContext, d->codecContext->sample_rate) + d->srcSamples, d->codecContext->sample_rate, d->codecContext->sample_rate, AV_ROUND_UP);
	if (d->dstSamples > d->maxDstSamples) {
		d->maxDstSamples = d->dstSamples;
		av_freep(&d->dstSamplesData[0]);
		if ((res = av_samples_alloc(d->dstSamplesData, 0, d->codecContext->channels, d->dstSamples, d->codecContext->sample_fmt, 1)) < 0) {
			LOG(("Audio Error: Unable to av_samples_alloc for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			onStop(false);
			emit error();
			return;
		}
		d->dstSamplesSize = av_samples_get_buffer_size(0, d->codecContext->channels, d->maxDstSamples, d->codecContext->sample_fmt, 0);
	}

	if ((res = swr_convert(d->swrContext, d->dstSamplesData, d->dstSamples, (const uint8_t **)srcSamplesData, d->srcSamples)) < 0) {
		LOG(("Audio Error: Unable to swr_convert for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	// Write audio frame

	AVFrame *frame = av_frame_alloc();

	frame->nb_samples = d->dstSamples;
	frame->pts = av_rescale_q(d->fullSamples, AVRational{1, d->codecContext->sample_rate}, d->codecContext->time_base);

	avcodec_fill_audio_frame(frame, d->codecContext->channels, d->codecContext->sample_fmt, d->dstSamplesData[0], d->dstSamplesSize, 0);

	writeFrame(frame);

	d->fullSamples += samplesCnt;

	av_frame_free(&frame);
}

void AudioCaptureInner::writeFrame(AVFrame *frame) {
	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	res = avcodec_send_frame(d->codecContext, frame);
	if (res == AVERROR(EAGAIN)) {
		int packetsWritten = writePackets();
		if (packetsWritten < 0) {
			if (frame && packetsWritten == AVERROR_EOF) {
				LOG(("Audio Error: EOF in packets received when EAGAIN was got in avcodec_send_frame()"));
				onStop(false);
				emit error();
			}
			return;
		} else if (!packetsWritten) {
			LOG(("Audio Error: No packets received when EAGAIN was got in avcodec_send_frame()"));
			onStop(false);
			emit error();
			return;
		}
		res = avcodec_send_frame(d->codecContext, frame);
	}
	if (res < 0) {
		LOG(("Audio Error: Unable to avcodec_send_frame for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	if (!frame) { // drain
		if ((res = writePackets()) != AVERROR_EOF) {
			LOG(("Audio Error: not EOF in packets received when draining the codec, result %1").arg(res));
			onStop(false);
			emit error();
		}
	}
}

int AudioCaptureInner::writePackets() {
	AVPacket pkt;
	memset(&pkt, 0, sizeof(pkt)); // data and size must be 0;

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	int written = 0;
	do {
		av_init_packet(&pkt);
		if ((res = avcodec_receive_packet(d->codecContext, &pkt)) < 0) {
			if (res == AVERROR(EAGAIN)) {
				return written;
			} else if (res == AVERROR_EOF) {
				return res;
			}
			LOG(("Audio Error: Unable to avcodec_receive_packet for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			onStop(false);
			emit error();
			return res;
		}

		av_packet_rescale_ts(&pkt, d->codecContext->time_base, d->stream->time_base);
		pkt.stream_index = d->stream->index;
		if ((res = av_interleaved_write_frame(d->fmtContext, &pkt)) < 0) {
			LOG(("Audio Error: Unable to av_interleaved_write_frame for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			onStop(false);
			emit error();
			return -1;
		}

		++written;
		av_packet_unref(&pkt);
	} while (true);
	return written;
}

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
