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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "audio.h"

#include <AL/al.h>
#include <AL/alc.h>

#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>

#ifdef Q_OS_MAC

extern "C" {

#include <iconv.h>

#undef iconv_open
#undef iconv
#undef iconv_close

iconv_t iconv_open (const char* tocode, const char* fromcode) {
	return libiconv_open(tocode, fromcode);
}
size_t iconv (iconv_t cd,  char* * inbuf, size_t *inbytesleft, char* * outbuf, size_t *outbytesleft) {
	return libiconv(cd, inbuf, inbytesleft, outbuf, outbytesleft);
}
int iconv_close (iconv_t cd) {
	return libiconv_close(cd);
}

}

#endif

namespace {
	ALCdevice *audioDevice = 0;
	ALCcontext *audioContext = 0;
	ALuint notifySource = 0;
	ALuint notifyBuffer = 0;

	uint64 notifyLengthMs = 0;

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
Q_DECLARE_METATYPE(SongMsgId);
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

	QFile notify(st::newMsgSound);
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
	qRegisterMetaType<SongMsgId>();

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

void audioFinish() {
	if (player) {
		delete player;
	}
	if (capture) {
		delete capture;
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
		alcMakeContextCurrent(NULL);
		alcDestroyContext(audioContext);
		audioContext = 0;
	}

	if (audioDevice) {
		alcCloseDevice(audioDevice);
		audioDevice = 0;
	}

	cSetHasAudioCapture(false);
	cSetHasAudioPlayer(false);
}

void AudioPlayer::Msg::clearData() {
	file = FileLocation();
	data = QByteArray();
	position = duration = 0;
	frequency = AudioVoiceMsgFrequency;
	skipStart = skipEnd = 0;
	loading = false;
	started = 0;
	state = AudioPlayerStopped;
	if (alIsSource(source)) {
		alSourceStop(source);
	}
	for (int32 i = 0; i < 3; ++i) {
		if (samplesCount[i]) {
			alSourceUnqueueBuffers(source, 1, buffers + i);
			samplesCount[i] = 0;
		}
	}
	nextBuffer = 0;
}

AudioPlayer::AudioPlayer() : _audioCurrent(0), _songCurrent(0),
_fader(new AudioPlayerFader(&_faderThread)),
_loader(new AudioPlayerLoaders(&_loaderThread)) {
	connect(this, SIGNAL(faderOnTimer()), _fader, SLOT(onTimer()));
	connect(this, SIGNAL(suppressSong()), _fader, SLOT(onSuppressSong()));
	connect(this, SIGNAL(unsuppressSong()), _fader, SLOT(onUnsuppressSong()));
	connect(this, SIGNAL(suppressAll()), _fader, SLOT(onSuppressAll()));
	connect(this, SIGNAL(songVolumeChanged()), _fader, SLOT(onSongVolumeChanged()));
	connect(this, SIGNAL(loaderOnStart(const AudioMsgId&,qint64)), _loader, SLOT(onStart(const AudioMsgId&,qint64)));
	connect(this, SIGNAL(loaderOnStart(const SongMsgId&,qint64)), _loader, SLOT(onStart(const SongMsgId&,qint64)));
	connect(this, SIGNAL(loaderOnCancel(const AudioMsgId&)), _loader, SLOT(onCancel(const AudioMsgId&)));
	connect(this, SIGNAL(loaderOnCancel(const SongMsgId&)), _loader, SLOT(onCancel(const SongMsgId&)));
	connect(&_faderThread, SIGNAL(started()), _fader, SLOT(onInit()));
	connect(&_loaderThread, SIGNAL(started()), _loader, SLOT(onInit()));
	connect(&_faderThread, SIGNAL(finished()), _fader, SLOT(deleteLater()));
	connect(&_loaderThread, SIGNAL(finished()), _loader, SLOT(deleteLater()));
	connect(_loader, SIGNAL(needToCheck()), _fader, SLOT(onTimer()));
	connect(_loader, SIGNAL(error(const AudioMsgId&)), this, SLOT(onError(const AudioMsgId&)));
	connect(_loader, SIGNAL(error(const SongMsgId&)), this, SLOT(onError(const SongMsgId&)));
	connect(_fader, SIGNAL(needToPreload(const AudioMsgId&)), _loader, SLOT(onLoad(const AudioMsgId&)));
	connect(_fader, SIGNAL(needToPreload(const SongMsgId&)), _loader, SLOT(onLoad(const SongMsgId&)));
	connect(_fader, SIGNAL(playPositionUpdated(const AudioMsgId&)), this, SIGNAL(updated(const AudioMsgId&)));
	connect(_fader, SIGNAL(playPositionUpdated(const SongMsgId&)), this, SIGNAL(updated(const SongMsgId&)));
	connect(_fader, SIGNAL(audioStopped(const AudioMsgId&)), this, SLOT(onStopped(const AudioMsgId&)));
	connect(_fader, SIGNAL(audioStopped(const SongMsgId&)), this, SLOT(onStopped(const SongMsgId&)));
	connect(_fader, SIGNAL(error(const AudioMsgId&)), this, SLOT(onError(const AudioMsgId&)));
	connect(_fader, SIGNAL(error(const SongMsgId&)), this, SLOT(onError(const SongMsgId&)));
	connect(this, SIGNAL(stoppedOnError(const AudioMsgId&)), this, SIGNAL(stopped(const AudioMsgId&)), Qt::QueuedConnection);
	connect(this, SIGNAL(stoppedOnError(const SongMsgId&)), this, SIGNAL(stopped(const SongMsgId&)), Qt::QueuedConnection);
	_loaderThread.start();
	_faderThread.start();
}

AudioPlayer::~AudioPlayer() {
	{
		QMutexLocker lock(&playerMutex);
		player = 0;
	}

	for (int32 i = 0; i < AudioVoiceMsgSimultaneously; ++i) {
		alSourceStop(_audioData[i].source);
		if (alIsBuffer(_audioData[i].buffers[0])) {
			alDeleteBuffers(3, _audioData[i].buffers);
			for (int32 j = 0; j < 3; ++j) {
				_audioData[i].buffers[j] = _audioData[i].samplesCount[j] = 0;
			}
		}
		if (alIsSource(_audioData[i].source)) {
			alDeleteSources(1, &_audioData[i].source);
			_audioData[i].source = 0;
		}
	}
	for (int32 i = 0; i < AudioSongSimultaneously; ++i) {
		alSourceStop(_songData[i].source);
		if (alIsBuffer(_songData[i].buffers[0])) {
			alDeleteBuffers(3, _songData[i].buffers);
			for (int32 j = 0; j < 3; ++j) {
				_songData[i].buffers[j] = _songData[i].samplesCount[j] = 0;
			}
		}
		if (alIsSource(_songData[i].source)) {
			alDeleteSources(1, &_songData[i].source);
			_songData[i].source = 0;
		}
	}
	_faderThread.quit();
	_loaderThread.quit();
	_faderThread.wait();
	_loaderThread.wait();
}

void AudioPlayer::onError(const AudioMsgId &audio) {
	emit stoppedOnError(audio);
	emit unsuppressSong();
}

void AudioPlayer::onError(const SongMsgId &song) {
	emit stoppedOnError(song);
}

void AudioPlayer::onStopped(const AudioMsgId &audio) {
	emit stopped(audio);
	emit unsuppressSong();
}

void AudioPlayer::onStopped(const SongMsgId &song) {
	emit stopped(song);
}

bool AudioPlayer::updateCurrentStarted(MediaOverviewType type, int32 pos) {
	Msg *data = 0;
	switch (type) {
	case OverviewAudios: data = &_audioData[_audioCurrent]; break;
	case OverviewDocuments: data = &_songData[_songCurrent]; break;
	}
	if (!data) return false;

	if (pos < 0) {
		if (alIsSource(data->source)) {
			alGetSourcei(data->source, AL_SAMPLE_OFFSET, &pos);
		} else {
			pos = 0;
		}
		if (!_checkALError()) {
			setStoppedState(data, AudioPlayerStoppedAtError);
			switch (type) {
			case OverviewAudios: onError(_audioData[_audioCurrent].audio); break;
			case OverviewDocuments: onError(_songData[_songCurrent].song); break;
			}
			return false;
		}
	}
	data->started = data->position = pos + data->skipStart;
	return true;
}

bool AudioPlayer::fadedStop(MediaOverviewType type, bool *fadedStart) {
	Msg *current = 0;
	switch (type) {
	case OverviewAudios: current = &_audioData[_audioCurrent]; break;
	case OverviewDocuments: current = &_songData[_songCurrent]; break;
	}
	if (!current) return false;

	switch (current->state) {
	case AudioPlayerStarting:
	case AudioPlayerResuming:
	case AudioPlayerPlaying:
		current->state = AudioPlayerFinishing;
		updateCurrentStarted(type);
		if (fadedStart) *fadedStart = true;
		break;
	case AudioPlayerPausing:
		current->state = AudioPlayerFinishing;
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
	AudioMsgId stopped;
	{
		QMutexLocker lock(&playerMutex);

		bool fadedStart = false;
		AudioMsg *current = &_audioData[_audioCurrent];
		if (current->audio != audio) {
			if (fadedStop(OverviewAudios, &fadedStart)) {
				stopped = current->audio;
			}
			if (current->audio) {
				emit loaderOnCancel(current->audio);
				emit faderOnTimer();
			}

			int32 index = 0;
			for (; index < AudioVoiceMsgSimultaneously; ++index) {
				if (_audioData[index].audio == audio) {
					_audioCurrent = index;
					break;
				}
			}
			if (index == AudioVoiceMsgSimultaneously && ++_audioCurrent >= AudioVoiceMsgSimultaneously) {
				_audioCurrent -= AudioVoiceMsgSimultaneously;
			}
			current = &_audioData[_audioCurrent];
		}
		current->audio = audio;
		current->file = audio.audio->location(true);
		current->data = audio.audio->data();
		if (current->file.isEmpty() && current->data.isEmpty()) {
			setStoppedState(current, AudioPlayerStoppedAtError);
			onError(audio);
		} else {
			current->state = fadedStart ? AudioPlayerStarting : AudioPlayerPlaying;
			current->loading = true;
			emit loaderOnStart(audio, position);
			emit suppressSong();
		}
	}
	if (stopped) emit updated(stopped);
}

void AudioPlayer::play(const SongMsgId &song, int64 position) {
	SongMsgId stopped;
	{
		QMutexLocker lock(&playerMutex);

		bool fadedStart = false;
		SongMsg *current = &_songData[_songCurrent];
		if (current->song != song) {
			if (fadedStop(OverviewDocuments, &fadedStart)) {
				stopped = current->song;
			}
			if (current->song) {
				emit loaderOnCancel(current->song);
				emit faderOnTimer();
			}

			int32 index = 0;
			for (; index < AudioSongSimultaneously; ++index) {
				if (_songData[index].song == song) {
					_songCurrent = index;
					break;
				}
			}
			if (index == AudioSongSimultaneously && ++_songCurrent >= AudioSongSimultaneously) {
				_songCurrent -= AudioSongSimultaneously;
			}
			current = &_songData[_songCurrent];
		}
		current->song = song;
		current->file = song.song->location(true);
		current->data = song.song->data();
		if (current->file.isEmpty() && current->data.isEmpty()) {
			setStoppedState(current);
			if (!song.song->loading()) {
				DocumentOpenLink::doOpen(song.song);
			}
		} else {
			current->state = fadedStart ? AudioPlayerStarting : AudioPlayerPlaying;
			current->loading = true;
			emit loaderOnStart(song, position);
		}
	}
	if (stopped) emit updated(stopped);
}

bool AudioPlayer::checkCurrentALError(MediaOverviewType type) {
	if (_checkALError()) return true;

	switch (type) {
	case OverviewAudios:
		setStoppedState(&_audioData[_audioCurrent], AudioPlayerStoppedAtError);
		onError(_audioData[_audioCurrent].audio);
		break;
	case OverviewDocuments:
		setStoppedState(&_songData[_songCurrent], AudioPlayerStoppedAtError);
		onError(_songData[_songCurrent].song);
		break;
	}
	return false;
}

void AudioPlayer::pauseresume(MediaOverviewType type, bool fast) {
	QMutexLocker lock(&playerMutex);

	Msg *current = 0;
	float64 suppressGain = 1.;
	switch (type) {
	case OverviewAudios:
		current = &_audioData[_audioCurrent];
		suppressGain = suppressAllGain;
		break;
	case OverviewDocuments:
		current = &_songData[_songCurrent];
		suppressGain = suppressSongGain * cSongVolume();
		break;
	}
	switch (current->state) {
	case AudioPlayerPausing:
	case AudioPlayerPaused:
	case AudioPlayerPausedAtEnd: {
		if (current->state == AudioPlayerPaused) {
			updateCurrentStarted(type);
		} else if (current->state == AudioPlayerPausedAtEnd) {
			if (alIsSource(current->source)) {
				alSourcei(current->source, AL_SAMPLE_OFFSET, qMax(current->position - current->skipStart, 0LL));
				if (!checkCurrentALError(type)) return;
			}
		}
		current->state = fast ? AudioPlayerPlaying : AudioPlayerResuming;

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
		if (type == OverviewAudios) emit suppressSong();
	} break;
	case AudioPlayerStarting:
	case AudioPlayerResuming:
	case AudioPlayerPlaying:
		current->state = AudioPlayerPausing;
		updateCurrentStarted(type);
		if (type == OverviewAudios) emit unsuppressSong();
	break;
	case AudioPlayerFinishing: current->state = AudioPlayerPausing; break;
	}
	emit faderOnTimer();
}

void AudioPlayer::seek(int64 position) {
	QMutexLocker lock(&playerMutex);

	MediaOverviewType type = OverviewDocuments;
	Msg *current = 0;
	float64 suppressGain = 1.;
	AudioMsgId audio;
	SongMsgId song;
	switch (type) {
	case OverviewAudios:
		current = &_audioData[_audioCurrent];
		audio = _audioData[_audioCurrent].audio;
		suppressGain = suppressAllGain;
		break;
	case OverviewDocuments:
		current = &_songData[_songCurrent];
		song = _songData[_songCurrent].song;
		suppressGain = suppressSongGain * cSongVolume();
		break;
	}

	bool isSource = alIsSource(current->source);
	bool fastSeek = (position >= current->skipStart && position < current->duration - current->skipEnd - (current->skipEnd ? AudioVoiceMsgFrequency : 0));
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
	switch (current->state) {
	case AudioPlayerPausing:
	case AudioPlayerPaused:
	case AudioPlayerPausedAtEnd: {
		if (current->state == AudioPlayerPausedAtEnd) {
			current->state = AudioPlayerPaused;
		}
		lock.unlock();
		return pauseresume(type, true);
	} break;
	case AudioPlayerStarting:
	case AudioPlayerResuming:
	case AudioPlayerPlaying:
		current->state = AudioPlayerPausing;
		updateCurrentStarted(type);
		if (type == OverviewAudios) emit unsuppressSong();
		break;
	case AudioPlayerFinishing:
	case AudioPlayerStopped:
	case AudioPlayerStoppedAtEnd:
	case AudioPlayerStoppedAtError:
	case AudioPlayerStoppedAtStart:
		lock.unlock();
		switch (type) {
		case OverviewAudios: if (audio) return play(audio, position);
		case OverviewDocuments: if (song) return play(song, position);
		}
	}
	emit faderOnTimer();
}

void AudioPlayer::stop(MediaOverviewType type) {
	switch (type) {
	case OverviewAudios: {
		AudioMsgId current;
		{
			QMutexLocker lock(&playerMutex);
			current = _audioData[_audioCurrent].audio;
			fadedStop(type);
		}
		if (current) emit updated(current);
	} break;

	case OverviewDocuments: {
		SongMsgId current;
		{
			QMutexLocker lock(&playerMutex);
			current = _songData[_songCurrent].song;
			fadedStop(type);
		}
		if (current) emit updated(current);
	} break;
	}
}

void AudioPlayer::stopAndClear() {
	AudioMsg *current_audio = 0;
	{
		QMutexLocker lock(&playerMutex);
		current_audio = &_audioData[_audioCurrent];
		if (current_audio) {
			setStoppedState(current_audio);
		}
	}
	SongMsg *current_song = 0;
	{
		QMutexLocker lock(&playerMutex);
		current_song = &_songData[_songCurrent];
		if (current_song) {
			setStoppedState(current_song);
		}
	}
	if (current_song) {
		emit updated(current_song->song);
	}
	if (current_audio) {
		emit updated(current_audio->audio);
	}
	{
		QMutexLocker lock(&playerMutex);
		for (int32 index = 0; index < AudioVoiceMsgSimultaneously; ++index) {
			if (_audioData[index].audio) {
				emit loaderOnCancel(_audioData[index].audio);
			}
			_audioData[index].clear();
			if (_songData[index].song) {
				emit loaderOnCancel(_songData[index].song);
			}
			_songData[index].clear();
		}
	}
}

void AudioPlayer::currentState(AudioMsgId *audio, AudioPlayerState *state, int64 *position, int64 *duration, int32 *frequency) {
	QMutexLocker lock(&playerMutex);
	AudioMsg *current = &_audioData[_audioCurrent];
	if (audio) *audio = current->audio;
	return currentState(current, state, position, duration, frequency);
}

void AudioPlayer::currentState(SongMsgId *song, AudioPlayerState *state, int64 *position, int64 *duration, int32 *frequency) {
	QMutexLocker lock(&playerMutex);
	SongMsg *current = &_songData[_songCurrent];
	if (song) *song = current->song;
	return currentState(current, state, position, duration, frequency);
}

void AudioPlayer::currentState(Msg *current, AudioPlayerState *state, int64 *position, int64 *duration, int32 *frequency) {
	if (state) *state = current->state;
	if (position) *position = current->position;
	if (duration) *duration = current->duration;
	if (frequency) *frequency = current->frequency;
}

void AudioPlayer::setStoppedState(Msg *current, AudioPlayerState state) {
	current->state = state;
	current->position = 0;
}

void AudioPlayer::clearStoppedAtStart(const AudioMsgId &audio) {
	QMutexLocker lock(&playerMutex);
	if (_audioData[_audioCurrent].audio == audio && _audioData[_audioCurrent].state == AudioPlayerStoppedAtStart) {
		setStoppedState(&_audioData[_audioCurrent]);
	}
}

void AudioPlayer::clearStoppedAtStart(const SongMsgId &song) {
	QMutexLocker lock(&playerMutex);
	if (_songData[_songCurrent].song == song && _songData[_songCurrent].state == AudioPlayerStoppedAtStart) {
		setStoppedState(&_songData[_songCurrent]);
	}
}

void AudioPlayer::resumeDevice() {
	_fader->resumeDevice();
}

AudioCapture::AudioCapture() : _capture(new AudioCaptureInner(&_captureThread)) {
	connect(this, SIGNAL(captureOnStart()), _capture, SLOT(onStart()));
	connect(this, SIGNAL(captureOnStop(bool)), _capture, SLOT(onStop(bool)));
	connect(_capture, SIGNAL(done(QByteArray,qint32)), this, SIGNAL(onDone(QByteArray,qint32)));
	connect(_capture, SIGNAL(update(qint16,qint32)), this, SIGNAL(onUpdate(qint16,qint32)));
	connect(_capture, SIGNAL(error()), this, SIGNAL(onError()));
	connect(&_captureThread, SIGNAL(started()), _capture, SLOT(onInit()));
	connect(&_captureThread, SIGNAL(finished()), _capture, SLOT(deleteLater()));
	_captureThread.start();
}

void AudioCapture::start() {
	emit captureOnStart();
}

void AudioCapture::stop(bool needResult) {
	emit captureOnStop(needResult);
}

bool AudioCapture::check() {
	if (const ALCchar *def = alcGetString(0, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER)) {
		if (ALCdevice *dev = alcCaptureOpenDevice(def, AudioVoiceMsgFrequency, AL_FORMAT_MONO16, AudioVoiceMsgFrequency / 5)) {
			alcCaptureCloseDevice(dev);
			return _checkALCError();
		}
	}
	return false;
}

AudioCapture::~AudioCapture() {
	capture = 0;
	_captureThread.quit();
	_captureThread.wait();
}

AudioPlayer *audioPlayer() {
	return player;
}

AudioCapture *audioCapture() {
	return capture;
}

AudioPlayerFader::AudioPlayerFader(QThread *thread) : _timer(this), _pauseFlag(false), _paused(true),
_suppressAll(false), _suppressAllAnim(false), _suppressSong(false), _suppressSongAnim(false),
_suppressAllGain(1., 1.), _suppressSongGain(1., 1.),
_suppressAllStart(0), _suppressSongStart(0) {
	moveToThread(thread);
	_timer.moveToThread(thread);
	_pauseTimer.moveToThread(thread);

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
		uint64 ms = getms();
		float64 wasSong = suppressSongGain;
		if (_suppressAll) {
			float64 wasAudio = suppressAllGain;
			if (ms >= _suppressAllStart + notifyLengthMs || ms < _suppressAllStart) {
				_suppressAll = _suppressAllAnim = false;
				_suppressAllGain = anim::fvalue(1., 1.);
			} else if (ms > _suppressAllStart + notifyLengthMs - AudioFadeDuration) {
				if (_suppressAllGain.to() != 1.) _suppressAllGain.start(1.);
				_suppressAllGain.update(1. - ((_suppressAllStart + notifyLengthMs - ms) / float64(AudioFadeDuration)), anim::linear);
			} else if (ms >= _suppressAllStart + st::notifyFastAnim) {
				if (_suppressAllAnim) {
					_suppressAllGain.finish();
					_suppressAllAnim = false;
				}
			} else if (ms > _suppressAllStart) {
				_suppressAllGain.update((ms - _suppressAllStart) / st::notifyFastAnim, anim::linear);
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
	bool hasFading = (_suppressAll || _suppressSongAnim), hasPlaying = false;

	for (int32 i = 0; i < AudioVoiceMsgSimultaneously; ++i) {
		AudioPlayer::AudioMsg &m(voice->_audioData[i]);
		if ((m.state & AudioPlayerStoppedMask) || m.state == AudioPlayerPaused || !m.source) continue;

		int32 emitSignals = updateOnePlayback(&m, hasPlaying, hasFading, suppressAllGain, suppressAudioChanged);
		if (emitSignals & EmitError) emit error(m.audio);
		if (emitSignals & EmitStopped) emit audioStopped(m.audio);
		if (emitSignals & EmitPositionUpdated) emit playPositionUpdated(m.audio);
		if (emitSignals & EmitNeedToPreload) emit needToPreload(m.audio);
	}

	for (int32 i = 0; i < AudioSongSimultaneously; ++i) {
		AudioPlayer::SongMsg &m(voice->_songData[i]);
		if ((m.state & AudioPlayerStoppedMask) || m.state == AudioPlayerPaused || !m.source) continue;

		int32 emitSignals = updateOnePlayback(&m, hasPlaying, hasFading, suppressSongGain * cSongVolume(), suppressSongChanged || _songVolumeChanged);
		if (emitSignals & EmitError) emit error(m.song);
		if (emitSignals & EmitStopped) emit audioStopped(m.song);
		if (emitSignals & EmitPositionUpdated) emit playPositionUpdated(m.song);
		if (emitSignals & EmitNeedToPreload) emit needToPreload(m.song);
	}
	_songVolumeChanged = false;

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

int32 AudioPlayerFader::updateOnePlayback(AudioPlayer::Msg *m, bool &hasPlaying, bool &hasFading, float64 suppressGain, bool suppressGainChanged) {
	bool playing = false, fading = false;

	ALint pos = 0;
	ALint state = AL_INITIAL;
	alGetSourcei(m->source, AL_SAMPLE_OFFSET, &pos);
	if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
	alGetSourcei(m->source, AL_SOURCE_STATE, &state);
	if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }

	int32 emitSignals = 0;
	switch (m->state) {
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
			if (m->state == AudioPlayerPausing) {
				m->state = AudioPlayerPausedAtEnd;
			} else {
				setStoppedState(m, AudioPlayerStoppedAtEnd);
			}
			emitSignals |= EmitStopped;
		} else if (1000 * (pos + m->skipStart - m->started) >= AudioFadeDuration * m->frequency) {
			fading = false;
			alSourcef(m->source, AL_GAIN, 1. * suppressGain);
			if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
			switch (m->state) {
			case AudioPlayerFinishing:
				alSourceStop(m->source);
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
				setStoppedState(m);
				state = AL_STOPPED;
				break;
			case AudioPlayerPausing:
				alSourcePause(m->source);
				if (!_checkALError()) { setStoppedState(m, AudioPlayerStoppedAtError); return EmitError; }
				m->state = AudioPlayerPaused;
				break;
			case AudioPlayerStarting:
			case AudioPlayerResuming:
				m->state = AudioPlayerPlaying;
				playing = true;
				break;
			}
		} else {
			float64 newGain = 1000. * (pos + m->skipStart - m->started) / (AudioFadeDuration * m->frequency);
			if (m->state == AudioPlayerPausing || m->state == AudioPlayerFinishing) {
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
	if (state == AL_PLAYING && pos + m->skipStart - m->position >= AudioCheckPositionDelta) {
		m->position = pos + m->skipStart;
		emitSignals |= EmitPositionUpdated;
	}
	if (playing || m->state == AudioPlayerStarting || m->state == AudioPlayerResuming) {
		if (!m->loading && m->skipEnd > 0 && m->position + AudioPreloadSamples + m->skipEnd > m->duration) {
			m->loading = true;
			emitSignals |= EmitNeedToPreload;
		}
	}
	if (playing) hasPlaying = true;
	if (fading) hasFading = true;

	return emitSignals;
}

void AudioPlayerFader::setStoppedState(AudioPlayer::Msg *m, AudioPlayerState state) {
	m->state = state;
	m->position = 0;
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

void AudioPlayerFader::resumeDevice() {
	QMutexLocker lock(&_pauseMutex);
	_pauseFlag = false;
	emit stopPauseDevice();
	if (_paused) {
		_paused = false;
		alcDeviceResumeSOFT(audioDevice);
	}
}

class AudioPlayerLoader {
public:
	AudioPlayerLoader(const FileLocation &file, const QByteArray &data) : file(file), access(false), data(data), dataPos(0) {
	}
	virtual ~AudioPlayerLoader() {
		if (access) {
			file.accessDisable();
			access = false;
		}
	}

	bool check(const FileLocation &file, const QByteArray &data) {
		return this->file == file && this->data.size() == data.size();
	}

	virtual bool open(qint64 position = 0) = 0;
	virtual int64 duration() = 0;
	virtual int32 frequency() = 0;
	virtual int32 format() = 0;
	virtual int readMore(QByteArray &result, int64 &samplesAdded) = 0; // < 0 - error, 0 - nothing read, > 0 - read something

protected:

	FileLocation file;
	bool access;
	QByteArray data;

	QFile f;
	int32 dataPos;

	bool openFile() {
		if (data.isEmpty()) {
			if (f.isOpen()) f.close();
			if (!access) {
				if (!file.accessEnable()) {
					LOG(("Audio Error: could not open file access '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(f.error()).arg(f.errorString()));
					return false;
				}
				access = true;
			}
			f.setFileName(file.name());
			if (!f.open(QIODevice::ReadOnly)) {
				LOG(("Audio Error: could not open file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(f.error()).arg(f.errorString()));
				return false;
			}
		}
		dataPos = 0;
		return true;
	}

};

static const AVSampleFormat _toFormat = AV_SAMPLE_FMT_S16;
static const int64_t _toChannelLayout = AV_CH_LAYOUT_STEREO;
static const int32 _toChannels = 2;
class FFMpegLoader : public AudioPlayerLoader {
public:

	FFMpegLoader(const FileLocation &file, const QByteArray &data) : AudioPlayerLoader(file, data),
		freq(AudioVoiceMsgFrequency), fmt(AL_FORMAT_STEREO16),
		sampleSize(2 * sizeof(short)), srcRate(AudioVoiceMsgFrequency), dstRate(AudioVoiceMsgFrequency),
		maxResampleSamples(1024), dstSamplesData(0), len(0),
		ioBuffer(0), ioContext(0), fmtContext(0), codec(0), codecContext(0), streamId(0), frame(0), swrContext(0),
		_opened(false) {
		frame = av_frame_alloc();
	}

	bool open(qint64 position = 0) {
		if (!AudioPlayerLoader::openFile()) {
			return false;
		}

		ioBuffer = (uchar*)av_malloc(AVBlockSize);
		if (data.isEmpty()) {
			ioContext = avio_alloc_context(ioBuffer, AVBlockSize, 0, static_cast<void*>(this), &FFMpegLoader::_read_file, 0, &FFMpegLoader::_seek_file);
		} else {
			ioContext = avio_alloc_context(ioBuffer, AVBlockSize, 0, static_cast<void*>(this), &FFMpegLoader::_read_data, 0, &FFMpegLoader::_seek_data);
		}
		fmtContext = avformat_alloc_context();
		if (!fmtContext) {
			LOG(("Audio Error: Unable to avformat_alloc_context for file '%1', data size '%2'").arg(file.name()).arg(data.size()));
			return false;
		}
		fmtContext->pb = ioContext;

		int res = 0;
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		if ((res = avformat_open_input(&fmtContext, 0, 0, 0)) < 0) {
			ioBuffer = 0;

			LOG(("Audio Error: Unable to avformat_open_input for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}
		_opened = true;

		if ((res = avformat_find_stream_info(fmtContext, 0)) < 0) {
			LOG(("Audio Error: Unable to avformat_find_stream_info for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}

		streamId = av_find_best_stream(fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
		if (streamId < 0) {
			LOG(("Audio Error: Unable to av_find_best_stream for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(streamId).arg(av_make_error_string(err, sizeof(err), streamId)));
			return false;
		}

		// Get a pointer to the codec context for the audio stream
		codecContext = fmtContext->streams[streamId]->codec;
		av_opt_set_int(codecContext, "refcounted_frames", 1, 0);
		if ((res = avcodec_open2(codecContext, codec, 0)) < 0) {
			LOG(("Audio Error: Unable to avcodec_open2 for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}

		freq = codecContext->sample_rate;
		if (fmtContext->streams[streamId]->duration == AV_NOPTS_VALUE) {
			len = (fmtContext->duration * freq) / AV_TIME_BASE;
		} else {
			len = (fmtContext->streams[streamId]->duration * freq * fmtContext->streams[streamId]->time_base.num) / fmtContext->streams[streamId]->time_base.den;
		}
		uint64_t layout = codecContext->channel_layout;
		inputFormat = codecContext->sample_fmt;
		switch (layout) {
		case AV_CH_LAYOUT_MONO:
			switch (inputFormat) {
			case AV_SAMPLE_FMT_U8:
			case AV_SAMPLE_FMT_U8P: fmt = AL_FORMAT_MONO8; sampleSize = 1; break;
			case AV_SAMPLE_FMT_S16:
			case AV_SAMPLE_FMT_S16P: fmt = AL_FORMAT_MONO16; sampleSize = 2; break;
			default:
				sampleSize = -1; // convert needed
				break;
			}
			break;
		case AV_CH_LAYOUT_STEREO:
			switch (inputFormat) {
			case AV_SAMPLE_FMT_U8: fmt = AL_FORMAT_STEREO8; sampleSize = sizeof(short); break;
			case AV_SAMPLE_FMT_S16: fmt = AL_FORMAT_STEREO16; sampleSize = 2 * sizeof(short); break;
			default:
				sampleSize = -1; // convert needed
				break;
			}
			break;
		default:
			sampleSize = -1; // convert needed
			break;
		}
		if (freq != 44100 && freq != 48000) {
			sampleSize = -1; // convert needed
		}

		if (sampleSize < 0) {
			swrContext = swr_alloc();
			if (!swrContext) {
				LOG(("Audio Error: Unable to swr_alloc for file '%1', data size '%2'").arg(file.name()).arg(data.size()));
				return false;
			}
			int64_t src_ch_layout = layout, dst_ch_layout = _toChannelLayout;
			srcRate = freq;
			AVSampleFormat src_sample_fmt = inputFormat, dst_sample_fmt = _toFormat;
			dstRate = (freq != 44100 && freq != 48000) ? AudioVoiceMsgFrequency : freq;

			av_opt_set_int(swrContext, "in_channel_layout", src_ch_layout, 0);
			av_opt_set_int(swrContext, "in_sample_rate", srcRate, 0);
			av_opt_set_sample_fmt(swrContext, "in_sample_fmt", src_sample_fmt, 0);
			av_opt_set_int(swrContext, "out_channel_layout", dst_ch_layout, 0);
			av_opt_set_int(swrContext, "out_sample_rate", dstRate, 0);
			av_opt_set_sample_fmt(swrContext, "out_sample_fmt", dst_sample_fmt, 0);

			if ((res = swr_init(swrContext)) < 0) {
				LOG(("Audio Error: Unable to swr_init for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
				return false;
			}

			sampleSize = _toChannels * sizeof(short);
			freq = dstRate;
			len = av_rescale_rnd(len, dstRate, srcRate, AV_ROUND_UP);
			fmt = AL_FORMAT_STEREO16;

			maxResampleSamples = av_rescale_rnd(AVBlockSize / sampleSize, dstRate, srcRate, AV_ROUND_UP);
			if ((res = av_samples_alloc_array_and_samples(&dstSamplesData, 0, _toChannels, maxResampleSamples, _toFormat, 0)) < 0) {
				LOG(("Audio Error: Unable to av_samples_alloc for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
				return false;
			}
		}
		if (position) {
			int64 ts = (position * fmtContext->streams[streamId]->time_base.den) / (freq * fmtContext->streams[streamId]->time_base.num);
			if (av_seek_frame(fmtContext, streamId, ts, AVSEEK_FLAG_ANY) < 0) {
				if (av_seek_frame(fmtContext, streamId, ts, 0) < 0) {
				}
			}
			//if (dstSamplesData) {
			//	position = qRound(srcRate * (position / float64(dstRate)));
			//}
		}

		return true;
	}

	int64 duration() {
		return len;
	}

	int32 frequency() {
		return freq;
	}

	int32 format() {
		return fmt;
	}

	int readMore(QByteArray &result, int64 &samplesAdded) {
		int res;
		if ((res = av_read_frame(fmtContext, &avpkt)) < 0) {
			if (res != AVERROR_EOF) {
				char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
				LOG(("Audio Error: Unable to av_read_frame() file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			}
			return -1;
		}
		if (avpkt.stream_index == streamId) {
			av_frame_unref(frame);
			int got_frame = 0;
			if ((res = avcodec_decode_audio4(codecContext, frame, &got_frame, &avpkt)) < 0) {
				char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
				LOG(("Audio Error: Unable to avcodec_decode_audio4() file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));

				av_packet_unref(&avpkt);
				if (res == AVERROR_INVALIDDATA) return 0; // try to skip bad packet
				return -1;
			}

			if (got_frame) {
				if (dstSamplesData) { // convert needed
					int64_t dstSamples = av_rescale_rnd(swr_get_delay(swrContext, srcRate) + frame->nb_samples, dstRate, srcRate, AV_ROUND_UP);
					if (dstSamples > maxResampleSamples) {
						maxResampleSamples = dstSamples;
						av_free(dstSamplesData[0]);

						if ((res = av_samples_alloc(dstSamplesData, 0, _toChannels, maxResampleSamples, _toFormat, 1)) < 0) {
							dstSamplesData[0] = 0;
							char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
							LOG(("Audio Error: Unable to av_samples_alloc for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));

							av_packet_unref(&avpkt);
							return -1;
						}
					}
					if ((res = swr_convert(swrContext, dstSamplesData, dstSamples, (const uint8_t**)frame->extended_data, frame->nb_samples)) < 0) {
						char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
						LOG(("Audio Error: Unable to swr_convert for file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));

						av_packet_unref(&avpkt);
						return -1;
					}
					int32 resultLen = av_samples_get_buffer_size(0, _toChannels, res, _toFormat, 1);
					result.append((const char*)dstSamplesData[0], resultLen);
					samplesAdded += resultLen / sampleSize;
				} else {
					result.append((const char*)frame->extended_data[0], frame->nb_samples * sampleSize);
					samplesAdded += frame->nb_samples;
				}
			}
		}
		av_packet_unref(&avpkt);
		return 1;
	}

	~FFMpegLoader() {
		if (ioContext) av_free(ioContext);
		if (codecContext) avcodec_close(codecContext);
		if (swrContext) swr_free(&swrContext);
		if (dstSamplesData) {
			if (dstSamplesData[0]) {
				av_freep(&dstSamplesData[0]);
			}
			av_freep(&dstSamplesData);
		}
		if (_opened) {
			avformat_close_input(&fmtContext);
		} else if (ioBuffer) {
			av_free(ioBuffer);
		}
		if (fmtContext) avformat_free_context(fmtContext);
		av_frame_free(&frame);
	}

private:

	int32 freq, fmt;
	int32 sampleSize, srcRate, dstRate, maxResampleSamples;
	uint8_t **dstSamplesData;
	int64 len;

	uchar *ioBuffer;
	AVIOContext *ioContext;
	AVFormatContext *fmtContext;
	AVCodec *codec;
	AVCodecContext *codecContext;
	AVPacket avpkt;
	int32 streamId;
	AVSampleFormat inputFormat;
	AVFrame *frame;

	SwrContext *swrContext;

	bool _opened;

	static int _read_data(void *opaque, uint8_t *buf, int buf_size) {
		FFMpegLoader *l = reinterpret_cast<FFMpegLoader*>(opaque);

		int32 nbytes = qMin(l->data.size() - l->dataPos, int32(buf_size));
		if (nbytes <= 0) {
			return 0;
		}

		memcpy(buf, l->data.constData() + l->dataPos, nbytes);
		l->dataPos += nbytes;
		return nbytes;
	}

	static int64_t _seek_data(void *opaque, int64_t offset, int whence) {
		FFMpegLoader *l = reinterpret_cast<FFMpegLoader*>(opaque);

		int32 newPos = -1;
		switch (whence) {
		case SEEK_SET: newPos = offset; break;
		case SEEK_CUR: newPos = l->dataPos + offset; break;
		case SEEK_END: newPos = l->data.size() + offset; break;
		}
		if (newPos < 0 || newPos > l->data.size()) {
			return -1;
		}
		l->dataPos = newPos;
		return l->dataPos;
	}

	static int _read_file(void *opaque, uint8_t *buf, int buf_size) {
		FFMpegLoader *l = reinterpret_cast<FFMpegLoader*>(opaque);
		return int(l->f.read((char*)(buf), buf_size));
	}

	static int64_t _seek_file(void *opaque, int64_t offset, int whence) {
		FFMpegLoader *l = reinterpret_cast<FFMpegLoader*>(opaque);

		switch (whence) {
		case SEEK_SET: return l->f.seek(offset) ? l->f.pos() : -1;
		case SEEK_CUR: return l->f.seek(l->f.pos() + offset) ? l->f.pos() : -1;
		case SEEK_END: return l->f.seek(l->f.size() + offset) ? l->f.pos() : -1;
		}
		return -1;
	}
};

AudioPlayerLoaders::AudioPlayerLoaders(QThread *thread) : _audioLoader(0), _songLoader(0) {
	moveToThread(thread);
}

AudioPlayerLoaders::~AudioPlayerLoaders() {
	delete _audioLoader;
	delete _songLoader;
}

void AudioPlayerLoaders::onInit() {
}

void AudioPlayerLoaders::onStart(const AudioMsgId &audio, qint64 position) {
	_audio = AudioMsgId();
	delete _audioLoader;
	_audioLoader = 0;

	{
		QMutexLocker lock(&playerMutex);
		AudioPlayer *voice = audioPlayer();
		if (!voice) return;

		voice->_audioData[voice->_audioCurrent].loading = true;
	}

	loadData(OverviewAudios, static_cast<const void*>(&audio), position);
}

void AudioPlayerLoaders::onStart(const SongMsgId &song, qint64 position) {
	_song = SongMsgId();
	delete _songLoader;
	_songLoader = 0;

	{
		QMutexLocker lock(&playerMutex);
		AudioPlayer *voice = audioPlayer();
		if (!voice) return;

		voice->_songData[voice->_songCurrent].loading = true;
	}

	loadData(OverviewDocuments, static_cast<const void*>(&song), position);
}

void AudioPlayerLoaders::clear(MediaOverviewType type) {
	switch (type) {
	case OverviewAudios: clearAudio(); break;
	case OverviewDocuments: clearSong(); break;
	}
}

void AudioPlayerLoaders::setStoppedState(AudioPlayer::Msg *m, AudioPlayerState state) {
	m->state = state;
	m->position = 0;
}

void AudioPlayerLoaders::emitError(MediaOverviewType type) {
	switch (type) {
	case OverviewAudios: emit error(clearAudio()); break;
	case OverviewDocuments: emit error(clearSong()); break;
	}
}

AudioMsgId AudioPlayerLoaders::clearAudio() {
	AudioMsgId current = _audio;
	_audio = AudioMsgId();
	delete _audioLoader;
	_audioLoader = 0;
	return current;
}

SongMsgId AudioPlayerLoaders::clearSong() {
	SongMsgId current = _song;
	_song = SongMsgId();
	delete _songLoader;
	_songLoader = 0;
	return current;
}

void AudioPlayerLoaders::onLoad(const AudioMsgId &audio) {
	loadData(OverviewAudios, static_cast<const void*>(&audio), 0);
}

void AudioPlayerLoaders::onLoad(const SongMsgId &song) {
	loadData(OverviewDocuments, static_cast<const void*>(&song), 0);
}

void AudioPlayerLoaders::loadData(MediaOverviewType type, const void *objId, qint64 position) {
	SetupError err = SetupNoErrorStarted;
	AudioPlayerLoader *l = setupLoader(type, objId, err, position);
	if (!l) {
		if (err == SetupErrorAtStart) {
			emitError(type);
		}
		return;
	}

	bool started = (err == SetupNoErrorStarted), finished = false, errAtStart = started;

	QByteArray result;
	int64 samplesAdded = 0, frequency = l->frequency(), format = l->format();
	while (result.size() < AudioVoiceMsgBufferSize) {
		int res = l->readMore(result, samplesAdded);
		if (res < 0) {
			if (errAtStart) {
				{
					QMutexLocker lock(&playerMutex);
					AudioPlayer::Msg *m = checkLoader(type);
					if (m) m->state = AudioPlayerStoppedAtStart;
				}
				emitError(type);
				return;
			}
			finished = true;
			break;
		}
		if (res > 0) errAtStart = false;

		QMutexLocker lock(&playerMutex);
		if (!checkLoader(type)) {
			clear(type);
			return;
		}
	}

	QMutexLocker lock(&playerMutex);
	AudioPlayer::Msg *m = checkLoader(type);
	if (!m) {
		clear(type);
		return;
	}

	if (started) {
		if (m->source) {
			alSourceStop(m->source);
			for (int32 i = 0; i < 3; ++i) {
				if (m->samplesCount[i]) {
					alSourceUnqueueBuffers(m->source, 1, m->buffers + i);
					m->samplesCount[i] = 0;
				}
			}
			m->nextBuffer = 0;
		}
		m->skipStart = position;
		m->skipEnd = m->duration - position;
		m->position = 0;
		m->started = 0;
	}
	if (samplesAdded) {
		if (!m->source) {
			alGenSources(1, &m->source);
			alSourcef(m->source, AL_PITCH, 1.f);
			alSource3f(m->source, AL_POSITION, 0, 0, 0);
			alSource3f(m->source, AL_VELOCITY, 0, 0, 0);
			alSourcei(m->source, AL_LOOPING, 0);
		}
		if (!m->buffers[m->nextBuffer]) alGenBuffers(3, m->buffers);
		if (!_checkALError()) {
			setStoppedState(m, AudioPlayerStoppedAtError);
			emitError(type);
			return;
		}

		if (m->samplesCount[m->nextBuffer]) {
			alSourceUnqueueBuffers(m->source, 1, m->buffers + m->nextBuffer);
			m->skipStart += m->samplesCount[m->nextBuffer];
		}

		m->samplesCount[m->nextBuffer] = samplesAdded;
		alBufferData(m->buffers[m->nextBuffer], format, result.constData(), result.size(), frequency);
		alSourceQueueBuffers(m->source, 1, m->buffers + m->nextBuffer);
		m->skipEnd -= samplesAdded;

		m->nextBuffer = (m->nextBuffer + 1) % 3;

		if (!_checkALError()) {
			setStoppedState(m, AudioPlayerStoppedAtError);
			emitError(type);
			return;
		}
	} else {
		finished = true;
	}
	if (finished) {
		m->skipEnd = 0;
		m->duration = m->skipStart + m->samplesCount[0] + m->samplesCount[1] + m->samplesCount[2];
		clear(type);
	}
	m->loading = false;
	if (m->state == AudioPlayerResuming || m->state == AudioPlayerPlaying || m->state == AudioPlayerStarting) {
		ALint state = AL_INITIAL;
		alGetSourcei(m->source, AL_SOURCE_STATE, &state);
		if (_checkALError()) {
			if (state != AL_PLAYING) {
				audioPlayer()->resumeDevice();

				switch (type) {
				case OverviewAudios: alSourcef(m->source, AL_GAIN, suppressAllGain); break;
				case OverviewDocuments: alSourcef(m->source, AL_GAIN, suppressSongGain * cSongVolume()); break;
				}
				if (!_checkALError()) {
					setStoppedState(m, AudioPlayerStoppedAtError);
					emitError(type);
					return;
				}

				alSourcePlay(m->source);
				if (!_checkALError()) {
					setStoppedState(m, AudioPlayerStoppedAtError);
					emitError(type);
					return;
				}

				emit needToCheck();
			}
		} else {
			setStoppedState(m, AudioPlayerStoppedAtError);
			emitError(type);
		}
	}
}

AudioPlayerLoader *AudioPlayerLoaders::setupLoader(MediaOverviewType type, const void *objId, SetupError &err, qint64 position) {
	err = SetupErrorAtStart;
	QMutexLocker lock(&playerMutex);
	AudioPlayer *voice = audioPlayer();
	if (!voice) return 0;

	bool isGoodId = false;
	AudioPlayer::Msg *m = 0;
	AudioPlayerLoader **l = 0;
	switch (type) {
	case OverviewAudios: {
		AudioPlayer::AudioMsg &msg(voice->_audioData[voice->_audioCurrent]);
		const AudioMsgId &audio(*static_cast<const AudioMsgId*>(objId));
		if (msg.audio != audio || !msg.loading) {
			emit error(audio);
			break;
		}
		m = &msg;
		l = &_audioLoader;
		isGoodId = (_audio == audio);
	} break;
	case OverviewDocuments: {
		AudioPlayer::SongMsg &msg(voice->_songData[voice->_songCurrent]);
		const SongMsgId &song(*static_cast<const SongMsgId*>(objId));
		if (msg.song != song || !msg.loading) {
			emit error(song);
			break;
		}
		m = &msg;
		l = &_songLoader;
		isGoodId = (_song == song);
	} break;
	}
	if (!l || !m) {
		LOG(("Audio Error: trying to load part of audio, that is not current at the moment"));
		err = SetupErrorNotPlaying;
		return 0;
	}

	if (*l && (!isGoodId || !(*l)->check(m->file, m->data))) {
		delete *l;
		*l = 0;
		switch (type) {
		case OverviewAudios: _audio = AudioMsgId(); break;
		case OverviewDocuments: _song = SongMsgId(); break;
		}
	}

	if (!*l) {
		switch (type) {
		case OverviewAudios: _audio = *static_cast<const AudioMsgId*>(objId); break;
		case OverviewDocuments: _song = *static_cast<const SongMsgId*>(objId); break;
		}

//		QByteArray header = m->data.mid(0, 8);
//		if (header.isEmpty()) {
//			QFile f(m->fname);
//			if (!f.open(QIODevice::ReadOnly)) {
//				LOG(("Audio Error: could not open file '%1'").arg(m->fname));
//				m->state = AudioPlayerStoppedAtStart;
//				return 0;
//			}
//			header = f.read(8);
//		}
//		if (header.size() < 8) {
//			LOG(("Audio Error: could not read header from file '%1', data size %2").arg(m->fname).arg(m->data.isEmpty() ? QFileInfo(m->fname).size() : m->data.size()));
//			m->state = AudioPlayerStoppedAtStart;
//			return 0;
//		}

		*l = new FFMpegLoader(m->file, m->data);

		int ret;
		if (!(*l)->open(position)) {
			m->state = AudioPlayerStoppedAtStart;
			return 0;
		}
		int64 duration = (*l)->duration();
		if (duration <= 0) {
			m->state = AudioPlayerStoppedAtStart;
			return 0;
		}
		m->duration = duration;
		m->frequency = (*l)->frequency();
		if (!m->frequency) m->frequency = AudioVoiceMsgFrequency;
		err = SetupNoErrorStarted;
	} else {
		if (!m->skipEnd) {
			err = SetupErrorLoadedFull;
			LOG(("Audio Error: trying to load part of audio, that is already loaded to the end"));
			return 0;
		}
	}
	return *l;
}

AudioPlayer::Msg *AudioPlayerLoaders::checkLoader(MediaOverviewType type) {
	AudioPlayer *voice = audioPlayer();
	if (!voice) return 0;

	bool isGoodId = false;
	AudioPlayer::Msg *m = 0;
	AudioPlayerLoader **l = 0;
	switch (type) {
	case OverviewAudios: {
		AudioPlayer::AudioMsg &msg(voice->_audioData[voice->_audioCurrent]);
		isGoodId = (msg.audio == _audio);
		l = &_audioLoader;
		m = &msg;
	} break;
	case OverviewDocuments: {
		AudioPlayer::SongMsg &msg(voice->_songData[voice->_songCurrent]);
		isGoodId = (msg.song == _song);
		l = &_songLoader;
		m = &msg;
	} break;
	}
	if (!l || !m) return 0;

	if (!isGoodId || !m->loading || !(*l)->check(m->file, m->data)) {
		LOG(("Audio Error: playing changed while loading"));
		return 0;
	}

	return m;
}

void AudioPlayerLoaders::onCancel(const AudioMsgId &audio) {
	if (_audio == audio) {
		_audio = AudioMsgId();
		delete _audioLoader;
		_audioLoader = 0;
	}

	QMutexLocker lock(&playerMutex);
	AudioPlayer *voice = audioPlayer();
	if (!voice) return;

	for (int32 i = 0; i < AudioVoiceMsgSimultaneously; ++i) {
		AudioPlayer::AudioMsg &m(voice->_audioData[i]);
		if (m.audio == audio) {
			m.loading = false;
		}
	}
}

void AudioPlayerLoaders::onCancel(const SongMsgId &song) {
	if (_song == song) {
		_song = SongMsgId();
		delete _songLoader;
		_songLoader = 0;
	}

	QMutexLocker lock(&playerMutex);
	AudioPlayer *voice = audioPlayer();
	if (!voice) return;

	for (int32 i = 0; i < AudioSongSimultaneously; ++i) {
		AudioPlayer::SongMsg &m(voice->_songData[i]);
		if (m.song == song) {
			m.loading = false;
		}
	}
}

struct AudioCapturePrivate {
	AudioCapturePrivate() :
		device(0), fmt(0), ioBuffer(0), ioContext(0), fmtContext(0), stream(0), codec(0), codecContext(0), opened(false),
		srcSamples(0), dstSamples(0), maxDstSamples(0), dstSamplesSize(0), fullSamples(0), srcSamplesData(0), dstSamplesData(0),
		swrContext(0), lastUpdate(0), level(0), dataPos(0) {
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
	int64 level;

	QByteArray data;
	int32 dataPos;

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
	d->codecContext = d->stream->codec;
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
	if ((res = avcodec_open2(d->codecContext, d->codec, NULL)) < 0) {
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
				writeFrame(encoded, framesize);
				encoded += framesize;
			}
			if (encoded != _captured.size()) {
				d->fullSamples = 0;
				d->dataPos = 0;
				d->data.clear();
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
	qint32 samples = d->fullSamples;
	if (d->device) {
		alcCaptureStop(d->device);
		alcCaptureCloseDevice(d->device);
		d->device = 0;

		if (d->ioContext) {
			av_free(d->ioContext);
			d->ioContext = 0;
		}
		if (d->codecContext) {
			avcodec_close(d->codecContext);
			d->codecContext = 0;
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
			d->swrContext = 0;
		}
		if (d->opened) {
			avformat_close_input(&d->fmtContext);
			d->opened = false;
			d->ioBuffer = 0;
		} else if (d->ioBuffer) {
			av_free(d->ioBuffer);
			d->ioBuffer = 0;
		}
		if (d->fmtContext) {
			avformat_free_context(d->fmtContext);
			d->fmtContext = 0;
		}
		d->fmt = 0;
		d->stream = 0;
		d->codec = 0;

		d->lastUpdate = 0;
		d->level = 0;

		d->dataPos = 0;
		d->data.clear();
	}
	if (needResult) emit done(result, samples);
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
				if (levelindex < skipSamples + fadeSamples) {
					d->level += qRound(qAbs(*ptr) * float64(levelindex - skipSamples) / fadeSamples);
				} else {
					d->level += qAbs(*ptr);
				}
			}
		}
		qint32 samplesFull = d->fullSamples + _captured.size() / sizeof(short), samplesSinceUpdate = samplesFull - d->lastUpdate;
		if (samplesSinceUpdate > AudioVoiceMsgUpdateView * AudioVoiceMsgFrequency / 1000) {
			emit update(d->level / samplesSinceUpdate, samplesFull);
			d->lastUpdate = samplesFull;
			d->level = 0;
		}
		// Write frames
		int32 framesize = d->srcSamples * d->codecContext->channels * sizeof(short), encoded = 0;
		while (uint32(_captured.size()) >= encoded + framesize + fadeSamples * sizeof(short)) {
			writeFrame(encoded, framesize);
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

void AudioCaptureInner::writeFrame(int32 offset, int32 framesize) {
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

	short *srcSamplesDataChannel = (short*)(_captured.data() + offset), **srcSamplesData = &srcSamplesDataChannel;
//	memcpy(d->srcSamplesData[0], _captured.constData() + offset, framesize);
	int32 skipSamples = AudioVoiceMsgSkip * AudioVoiceMsgFrequency / 1000, fadeSamples = AudioVoiceMsgFade * AudioVoiceMsgFrequency / 1000;
	if (d->fullSamples < skipSamples + fadeSamples) {
		int32 fadedCnt = qMin(samplesCnt, skipSamples + fadeSamples - d->fullSamples);
		float64 coef = 1. / fadeSamples, fadedFrom = d->fullSamples - skipSamples;
		short *ptr = (short*)srcSamplesData[0], *zeroEnd = ptr + qMin(samplesCnt, qMax(0, skipSamples - d->fullSamples)), *end = ptr + fadedCnt;
		for (; ptr != zeroEnd; ++ptr, ++fadedFrom) {
			*ptr = 0;
		}
		for (; ptr != end; ++ptr, ++fadedFrom) {
			*ptr = qRound(fadedFrom * coef * *ptr);
		}
	}

	// Convert to final format

	d->dstSamples = av_rescale_rnd(swr_get_delay(d->swrContext, d->codecContext->sample_rate) + d->srcSamples, d->codecContext->sample_rate, d->codecContext->sample_rate, AV_ROUND_UP);
	if (d->dstSamples > d->maxDstSamples) {
		d->maxDstSamples = d->dstSamples;
		av_free(d->dstSamplesData[0]);

		if ((res = av_samples_alloc(d->dstSamplesData, 0, d->codecContext->channels, d->dstSamples, d->codecContext->sample_fmt, 0)) < 0) {
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

	AVPacket pkt;
	memset(&pkt, 0, sizeof(pkt)); // data and size must be 0;
	AVFrame *frame = av_frame_alloc();
	int gotPacket;
	av_init_packet(&pkt);

	frame->nb_samples = d->dstSamples;
	avcodec_fill_audio_frame(frame, d->codecContext->channels, d->codecContext->sample_fmt, d->dstSamplesData[0], d->dstSamplesSize, 0);
	if ((res = avcodec_encode_audio2(d->codecContext, &pkt, frame, &gotPacket)) < 0) {
		LOG(("Audio Error: Unable to avcodec_encode_audio2 for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	if (gotPacket) {
		pkt.stream_index = d->stream->index;
		if ((res = av_interleaved_write_frame(d->fmtContext, &pkt)) < 0) {
			LOG(("Audio Error: Unable to av_interleaved_write_frame for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			onStop(false);
			emit error();
			return;
		}
	}
	d->fullSamples += samplesCnt;

	av_frame_free(&frame);
}

class FFMpegAttributesReader : public AudioPlayerLoader {
public:

	FFMpegAttributesReader(const FileLocation &file, const QByteArray &data) : AudioPlayerLoader(file, data),
		ioBuffer(0), ioContext(0), fmtContext(0), codec(0), streamId(0),
		_opened(false) {
	}

	bool open(qint64 position = 0) {
		if (!AudioPlayerLoader::openFile()) {
			return false;
		}

		ioBuffer = (uchar*)av_malloc(AVBlockSize);
		if (data.isEmpty()) {
			ioContext = avio_alloc_context(ioBuffer, AVBlockSize, 0, static_cast<void*>(this), &FFMpegAttributesReader::_read_file, 0, &FFMpegAttributesReader::_seek_file);
		} else {
			ioContext = avio_alloc_context(ioBuffer, AVBlockSize, 0, static_cast<void*>(this), &FFMpegAttributesReader::_read_data, 0, &FFMpegAttributesReader::_seek_data);
		}
		fmtContext = avformat_alloc_context();
		if (!fmtContext) {
			DEBUG_LOG(("Audio Read Error: Unable to avformat_alloc_context for file '%1', data size '%2'").arg(fname).arg(data.size()));
			return false;
		}
		fmtContext->pb = ioContext;

		int res = 0;
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		if ((res = avformat_open_input(&fmtContext, 0, 0, 0)) < 0) {
            ioBuffer = 0;

            DEBUG_LOG(("Audio Read Error: Unable to avformat_open_input for file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}
		_opened = true;

		if ((res = avformat_find_stream_info(fmtContext, 0)) < 0) {
			DEBUG_LOG(("Audio Read Error: Unable to avformat_find_stream_info for file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}

		streamId = av_find_best_stream(fmtContext, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
		if (streamId >= 0) {
			DEBUG_LOG(("Audio Read Error: Found video stream in file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(streamId).arg(av_make_error_string(err, sizeof(err), streamId)));
			return false;
		}

		streamId = av_find_best_stream(fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
		if (streamId < 0) {
			DEBUG_LOG(("Audio Read Error: Unable to av_find_best_stream for file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(streamId).arg(av_make_error_string(err, sizeof(err), streamId)));
			return false;
		}

		freq = fmtContext->streams[streamId]->codec->sample_rate;
		if (fmtContext->streams[streamId]->duration == AV_NOPTS_VALUE) {
			len = (fmtContext->duration * freq) / AV_TIME_BASE;
		} else {
			len = (fmtContext->streams[streamId]->duration * freq * fmtContext->streams[streamId]->time_base.num) / fmtContext->streams[streamId]->time_base.den;
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

	int64 duration() {
		return len;
	}

	int32 frequency() {
		return freq;
	}

	int32 format() {
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

	int readMore(QByteArray &result, int64 &samplesAdded) {
		DEBUG_LOG(("Audio Read Error: should not call this"));
		return -1;
	}

	~FFMpegAttributesReader() {
		if (ioContext) av_free(ioContext);
		if (_opened) {
			avformat_close_input(&fmtContext);
		} else if (ioBuffer) {
			av_free(ioBuffer);
		}
		if (fmtContext) avformat_free_context(fmtContext);
	}

private:

	QString fname, data;

	int32 freq;
	int64 len;
	QString _title, _performer;
	QImage _cover;
	QByteArray _coverBytes, _coverFormat;

	uchar *ioBuffer;
	AVIOContext *ioContext;
	AVFormatContext *fmtContext;
	AVCodec *codec;
	int32 streamId;

	bool _opened;

	static int _read_data(void *opaque, uint8_t *buf, int buf_size) {
		FFMpegAttributesReader *l = reinterpret_cast<FFMpegAttributesReader*>(opaque);

		int32 nbytes = qMin(l->data.size() - l->dataPos, int32(buf_size));
		if (nbytes <= 0) {
			return 0;
		}

		memcpy(buf, l->data.constData() + l->dataPos, nbytes);
		l->dataPos += nbytes;
		return nbytes;
	}

	static int64_t _seek_data(void *opaque, int64_t offset, int whence) {
		FFMpegAttributesReader *l = reinterpret_cast<FFMpegAttributesReader*>(opaque);

		int32 newPos = -1;
		switch (whence) {
		case SEEK_SET: newPos = offset; break;
		case SEEK_CUR: newPos = l->dataPos + offset; break;
		case SEEK_END: newPos = l->data.size() + offset; break;
		}
		if (newPos < 0 || newPos > l->data.size()) {
			return -1;
		}
		l->dataPos = newPos;
		return l->dataPos;
	}

	static int _read_file(void *opaque, uint8_t *buf, int buf_size) {
		FFMpegAttributesReader *l = reinterpret_cast<FFMpegAttributesReader*>(opaque);
		return int(l->f.read((char*)(buf), buf_size));
	}

	static int64_t _seek_file(void *opaque, int64_t offset, int whence) {
		FFMpegAttributesReader *l = reinterpret_cast<FFMpegAttributesReader*>(opaque);

		switch (whence) {
		case SEEK_SET: return l->f.seek(offset) ? l->f.pos() : -1;
		case SEEK_CUR: return l->f.seek(l->f.pos() + offset) ? l->f.pos() : -1;
		case SEEK_END: return l->f.seek(l->f.size() + offset) ? l->f.pos() : -1;
		}
		return -1;
	}
};

MTPDocumentAttribute audioReadSongAttributes(const QString &fname, const QByteArray &data, QImage &cover, QByteArray &coverBytes, QByteArray &coverFormat) {
	FFMpegAttributesReader reader(FileLocation(StorageFilePartial, fname), data);
	if (reader.open()) {
		int32 duration = reader.duration() / reader.frequency();
		if (reader.duration() > 0) {
			cover = reader.cover();
			coverBytes = reader.coverBytes();
			coverFormat = reader.coverFormat();
			return MTP_documentAttributeAudio(MTP_int(duration), MTP_string(reader.title()), MTP_string(reader.performer()));
		}
	}
	return MTP_documentAttributeFilename(MTP_string(fname));
}
