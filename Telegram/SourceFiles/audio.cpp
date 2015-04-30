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
#include "stdafx.h"
#include "audio.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <opusfile.h>
#include <ogg/ogg.h>

namespace {
	ALCdevice *audioDevice = 0;
	ALCcontext *audioContext = 0;
	ALuint notifySource = 0;
	ALuint notifyBuffer = 0;
	QMutex voicemsgsMutex;
	VoiceMessages *voicemsgs = 0;
}

bool _checkALCError() {
	ALenum errCode;
	if ((errCode = alcGetError(audioDevice)) != ALC_NO_ERROR) {
		LOG(("Audio Error: (alc) %1").arg((const char *)alcGetString(audioDevice, errCode)));
		return false;
	}
	return true;
}

bool _checkALError() {
	ALenum errCode;
	if ((errCode = alGetError()) != AL_NO_ERROR) {
		LOG(("Audio Error: (al) %1").arg((const char *)alGetString(errCode)));
		return false;
	}
	return true;
}

void audioInit() {
	uint64 ms = getms();
	if (audioDevice) return;

	audioDevice = alcOpenDevice(NULL);
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

	alBufferData(notifyBuffer, format, data, subchunk2Size, sampleRate);
	alSourcei(notifySource, AL_BUFFER, notifyBuffer);
	if (!_checkALError()) return audioFinish();

	voicemsgs = new VoiceMessages();
	alcSuspendContext(audioContext);
	LOG(("Audio init time: %1").arg(getms() - ms));
}

bool audioWorks() {
	return !!voicemsgs;
}

void audioPlayNotify() {
	if (!audioWorks()) return;

	audioVoice()->processContext();
	alSourcePlay(notifySource);
	emit audioVoice()->faderOnTimer();
}

void audioFinish() {
	if (voicemsgs) {
		delete voicemsgs;
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
}

VoiceMessages::VoiceMessages() : _current(0),
_fader(new VoiceMessagesFader(&_faderThread)), _loader(new VoiceMessagesLoader(&_loaderThread)) {
	connect(this, SIGNAL(faderOnTimer()), _fader, SLOT(onTimer()));
	connect(this, SIGNAL(loaderOnStart(AudioData*)), _loader, SLOT(onStart(AudioData*)));
	connect(this, SIGNAL(loaderOnCancel(AudioData*)), _loader, SLOT(onCancel(AudioData*)));
	connect(&_faderThread, SIGNAL(started()), _fader, SLOT(onInit()));
	connect(&_loaderThread, SIGNAL(started()), _loader, SLOT(onInit()));
	connect(&_faderThread, SIGNAL(finished()), _fader, SLOT(deleteLater()));
	connect(&_loaderThread, SIGNAL(finished()), _loader, SLOT(deleteLater()));
	connect(_loader, SIGNAL(needToCheck()), _fader, SLOT(onTimer()));
	connect(_loader, SIGNAL(error(AudioData*)), this, SLOT(onError(AudioData*)));
	connect(_fader, SIGNAL(needToPreload(AudioData*)), _loader, SLOT(onLoad(AudioData*)));
	connect(_fader, SIGNAL(playPositionUpdated(AudioData*)), this, SIGNAL(updated(AudioData*)));
	connect(_fader, SIGNAL(audioStopped(AudioData*)), this, SIGNAL(stopped(AudioData*)));
	connect(_fader, SIGNAL(error(AudioData*)), this, SLOT(onError(AudioData*)));
	_loaderThread.start();
	_faderThread.start();
}

VoiceMessages::~VoiceMessages() {
	{
		QMutexLocker lock(&voicemsgsMutex);
		voicemsgs = 0;
	}

	for (int32 i = 0; i < AudioVoiceMsgSimultaneously; ++i) {
		alSourceStop(_data[i].source);
		if (alIsBuffer(_data[i].buffers[0])) {
			alDeleteBuffers(3, _data[i].buffers);
			for (int32 j = 0; j < 3; ++j) {
				_data[i].buffers[j] = _data[i].samplesCount[j] = 0;
			}
		}
		if (alIsSource(_data[i].source)) {
			alDeleteSources(1, &_data[i].source);
			_data[i].source = 0;
		}
	}
	_faderThread.quit();
	_loaderThread.quit();
	_faderThread.wait();
	_loaderThread.wait();
}

void VoiceMessages::onError(AudioData *audio) {
	emit stopped(audio);
}

bool VoiceMessages::updateCurrentStarted(int32 pos) {
	if (pos < 0) {
		if (alIsSource(_data[_current].source)) {
			alGetSourcei(_data[_current].source, AL_SAMPLE_OFFSET, &pos);
		} else {
			pos = 0;
		}
	}
	if (!_checkALError()) {
		_data[_current].state = VoiceMessageStopped;
		onError(_data[_current].audio);
		return false;
	}
	_data[_current].started = _data[_current].position = pos + _data[_current].skipStart;
	return true;
}

void VoiceMessages::play(AudioData *audio) {
	QMutexLocker lock(&voicemsgsMutex);

	bool startNow = true;
	if (_data[_current].audio != audio) {
		switch (_data[_current].state) {
		case VoiceMessageStarting:
		case VoiceMessageResuming:
		case VoiceMessagePlaying:
			_data[_current].state = VoiceMessageFinishing;
			updateCurrentStarted();
			startNow = false;
			break;
		case VoiceMessagePausing: _data[_current].state = VoiceMessageFinishing; startNow = false; break;
		case VoiceMessagePaused: _data[_current].state = VoiceMessageStopped; break;
		}
		if (_data[_current].audio) {
			emit loaderOnCancel(_data[_current].audio);
			emit faderOnTimer();
		}
	}

	int32 index = 0;
	for (; index < AudioVoiceMsgSimultaneously; ++index) {
		if (_data[index].audio == audio) {
			_current = index;
			break;
		}
	}
	if (index == AudioVoiceMsgSimultaneously && ++_current >= AudioVoiceMsgSimultaneously) {
		_current -= AudioVoiceMsgSimultaneously;
	}
	_data[_current].audio = audio;
	_data[_current].fname = audio->already(true);
	_data[_current].data = audio->data;
	if (_data[_current].fname.isEmpty() && _data[_current].data.isEmpty()) {
		_data[_current].state = VoiceMessageStopped;
		onError(audio);
	} else if (updateCurrentStarted(0)) {
		_data[_current].state = startNow ? VoiceMessagePlaying : VoiceMessageStarting;
		_data[_current].loading = true;
		emit loaderOnStart(audio);
	}
}

void VoiceMessages::pauseresume() {
	QMutexLocker lock(&voicemsgsMutex);

	switch (_data[_current].state) {
	case VoiceMessagePausing:
	case VoiceMessagePaused:
		if (_data[_current].state == VoiceMessagePaused) {
			updateCurrentStarted();
		}
		_data[_current].state = VoiceMessageResuming;
		processContext();
		alSourcePlay(_data[_current].source);
	break;
	case VoiceMessageStarting:
	case VoiceMessageResuming:
	case VoiceMessagePlaying:
		_data[_current].state = VoiceMessagePausing;
		updateCurrentStarted();
	break;
	case VoiceMessageFinishing: _data[_current].state = VoiceMessagePausing; break;
	}
	emit faderOnTimer();
}

void VoiceMessages::currentState(AudioData **audio, VoiceMessageState *state, int64 *position, int64 *duration) {
	QMutexLocker lock(&voicemsgsMutex);
	if (audio) *audio = _data[_current].audio;
	if (state) *state = _data[_current].state;
	if (position) *position = _data[_current].position;
	if (duration) *duration = _data[_current].duration;
}

void VoiceMessages::processContext() {
	_fader->processContext();
}

VoiceMessages *audioVoice() {
	return voicemsgs;
}

VoiceMessagesFader::VoiceMessagesFader(QThread *thread) : _timer(this), _suspendFlag(false) {
	moveToThread(thread);
	_timer.moveToThread(thread);
	_suspendTimer.moveToThread(thread);

	_timer.setSingleShot(true);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(onTimer()));

	_suspendTimer.setSingleShot(true);
	connect(&_suspendTimer, SIGNAL(timeout()), this, SLOT(onSuspendTimer()));
	connect(this, SIGNAL(stopSuspend()), this, SLOT(onSuspendTimerStop()), Qt::QueuedConnection);
}

void VoiceMessagesFader::onInit() {
}

void VoiceMessagesFader::onTimer() {
	bool hasFading = false, hasPlaying = false;
	QMutexLocker lock(&voicemsgsMutex);
	VoiceMessages *voice = audioVoice();
	if (!voice) return;

	for (int32 i = 0; i < AudioVoiceMsgSimultaneously; ++i) {
		VoiceMessages::Msg &m(voice->_data[i]);
		if (m.state == VoiceMessageStopped || m.state == VoiceMessagePaused || !m.source) continue;

		bool playing = false, fading = false;
		ALint pos = 0;
		ALint state = AL_INITIAL;
		alGetSourcei(m.source, AL_SAMPLE_OFFSET, &pos);
		alGetSourcei(m.source, AL_SOURCE_STATE, &state);
		if (!_checkALError()) {
			m.state = VoiceMessageStopped;
			emit error(m.audio);
		} else {
			switch (m.state) {
			case VoiceMessageFinishing:
			case VoiceMessagePausing:
			case VoiceMessageStarting:
			case VoiceMessageResuming:
				fading = true;
			break;
			case VoiceMessagePlaying:
				playing = true;
			break;
			}
			if (fading && (state == AL_PLAYING || !m.loading)) {
				if (state != AL_PLAYING) {
					fading = false;
					if (m.source) {
						alSourcef(m.source, AL_GAIN, 1);
						alSourceStop(m.source);
					}
					m.state = VoiceMessageStopped;
					emit audioStopped(m.audio);
				} else if (1000 * (pos + m.skipStart - m.started) >= AudioFadeDuration * AudioVoiceMsgFrequency) {
					fading = false;
					alSourcef(m.source, AL_GAIN, 1);
					switch (m.state) {
					case VoiceMessageFinishing: alSourceStop(m.source); m.state = VoiceMessageStopped; break;
					case VoiceMessagePausing: alSourcePause(m.source); m.state = VoiceMessagePaused; break;
					case VoiceMessageStarting:
					case VoiceMessageResuming:
						m.state = VoiceMessagePlaying;
						playing = true;
					break;
					}
				} else {
					float64 newGain = 1000. * (pos + m.skipStart - m.started) / (AudioFadeDuration * AudioVoiceMsgFrequency);
					if (m.state == VoiceMessagePausing || m.state == VoiceMessageFinishing) {
						newGain = 1. - newGain;
					}
					alSourcef(m.source, AL_GAIN, newGain);
				}
			} else if (playing && (state == AL_PLAYING || !m.loading)) {
				if (state != AL_PLAYING) {
					playing = false;
					if (m.source) {
						alSourceStop(m.source);
						alSourcef(m.source, AL_GAIN, 1);
					}
					m.state = VoiceMessageStopped;
					emit audioStopped(m.audio);
				}
			}
			if (pos + m.skipStart - m.position >= AudioCheckPositionDelta) {
				m.position = pos + m.skipStart;
				emit playPositionUpdated(m.audio);
			}
			if (!m.loading && m.skipEnd > 0 && m.position + AudioPreloadSamples + m.skipEnd > m.duration) {
				m.loading = true;
				emit needToPreload(m.audio);
			}
			if (playing) hasPlaying = true;
			if (fading) hasFading = true;
		}
	}
	if (!hasPlaying) {
		ALint state = AL_INITIAL;
		alGetSourcei(notifySource, AL_SOURCE_STATE, &state);
		if (_checkALError() && state == AL_PLAYING) {
			hasPlaying = true;
		}
	}
	if (hasFading) {
		_timer.start(AudioFadeTimeout);
		processContext();
	} else if (hasPlaying) {
		_timer.start(AudioCheckPositionTimeout);
		processContext();
	} else {
		QMutexLocker lock(&_suspendMutex);
		_suspendFlag = true;
		_suspendTimer.start(AudioSuspendTimeout);
	}
}

void VoiceMessagesFader::onSuspendTimer() {
	QMutexLocker lock(&_suspendMutex);
	if (_suspendFlag) {
		alcSuspendContext(audioContext);
	}
}

void VoiceMessagesFader::onSuspendTimerStop() {
	if (_suspendTimer.isActive()) _suspendTimer.stop();
}

void VoiceMessagesFader::processContext() {
	QMutexLocker lock(&_suspendMutex);
	_suspendFlag = false;
	emit stopSuspend();
	alcProcessContext(audioContext);
}

struct VoiceMessagesLoader::Loader {
	QString fname;
	QByteArray data;
	OggOpusFile *file;
	ogg_int64_t pcm_offset;
	ogg_int64_t pcm_print_offset;
	int prev_li;

	Loader() : file(0), pcm_offset(0), pcm_print_offset(0), prev_li(-1) {

	}
};

VoiceMessagesLoader::VoiceMessagesLoader(QThread *thread) {
	moveToThread(thread);
}

VoiceMessagesLoader::~VoiceMessagesLoader() {
	for (Loaders::iterator i = _loaders.begin(), e = _loaders.end(); i != e; ++i) {
		delete i.value();
	}
	_loaders.clear();
}

void VoiceMessagesLoader::onInit() {
}

void VoiceMessagesLoader::onStart(AudioData *audio) {
	Loaders::iterator i = _loaders.find(audio);
	if (i != _loaders.end()) {
		delete (*i);
		_loaders.erase(i);
	}
	onLoad(audio);
}

void VoiceMessagesLoader::loadError(Loaders::iterator i) {
	emit error(i.key());
	delete (*i);
	_loaders.erase(i);
}

void VoiceMessagesLoader::onLoad(AudioData *audio) {
	bool started = false;
	int32 audioindex = -1;
	Loader *l = 0;
	Loaders::iterator j = _loaders.end();
	{
		QMutexLocker lock(&voicemsgsMutex);
		VoiceMessages *voice = audioVoice();
		if (!voice) return;

		for (int32 i = 0; i < AudioVoiceMsgSimultaneously; ++i) {
			VoiceMessages::Msg &m(voice->_data[i]);
			if (m.audio != audio || !m.loading) continue;

			audioindex = i;
			j = _loaders.find(audio);
			if (j != _loaders.end() && (j.value()->fname != m.fname || j.value()->data.size() != m.data.size())) {
				delete j.value();
				_loaders.erase(j);
				j = _loaders.end();
			}
			if (j == _loaders.end()) {
				l = (j = _loaders.insert(audio, new Loader())).value();
				l->fname = m.fname;
				l->data = m.data;
				
				int ret;
				if (m.data.isEmpty()) {
					l->file = op_open_file(m.fname.toUtf8().constData(), &ret);
				} else {
					l->file = op_open_memory((const unsigned char*)m.data.constData(), m.data.size(), &ret);
				}
				if (!l->file) {
					LOG(("Audio Error: op_open_file failed for '%1', data size '%2', error code %3").arg(m.fname).arg(m.data.size()).arg(ret));
					m.state = VoiceMessageStopped;
					return loadError(j);
				}
				ogg_int64_t duration = op_pcm_total(l->file, -1);
				if (duration < 0) {
					LOG(("Audio Error: op_pcm_total failed to get full duration for '%1', data size '%2', error code %3").arg(m.fname).arg(m.data.size()).arg(duration));
					m.state = VoiceMessageStopped;
					return loadError(j);
				}
				m.duration = duration;
				m.skipStart = 0;
				m.skipEnd = duration;
				m.position = 0;
				m.started = 0;
				started = true;
			} else {
				if (!m.skipEnd) continue;
				l = j.value();
			}
			break;
		}
	}

	if (j == _loaders.end()) {
		LOG(("Audio Error: trying to load part of audio, that is not playing at the moment"));
		emit error(audio);
		return;
	}
	if (started) {
		l->pcm_offset = op_pcm_tell(l->file);
		l->pcm_print_offset = l->pcm_offset - AudioVoiceMsgFrequency;
	}

	bool finished = false;
    DEBUG_LOG(("Audio Info: reading buffer for file '%1', data size '%2', current pcm_offset %3").arg(l->fname).arg(l->data.size()).arg(l->pcm_offset));

	QByteArray result;
	int64 samplesAdded = 0;
	while (result.size() < AudioVoiceMsgBufferSize) {
		opus_int16 pcm[AudioVoiceMsgFrequency * AudioVoiceMsgChannels];

		int ret = op_read_stereo(l->file, pcm, sizeof(pcm) / sizeof(*pcm));
		if (ret < 0) {
			/*{
				QMutexLocker lock(&voicemsgsMutex);
				VoiceMessages *voice = audioVoice();
				if (voice) {
					VoiceMessages::Msg &m(voice->_data[audioindex]);
					if (m.audio == audio) {
						m.state = VoiceMessageStopped;
					}
				}
			}*/
			LOG(("Audio Error: op_read_stereo failed, error code %1 (corrupted voice message?)").arg(ret));
			finished = true;
			break;
//			return loadError(j);
		}

		int li = op_current_link(l->file);
		if (li != l->prev_li) {
			const OpusHead *head = op_head(l->file, li);
			const OpusTags *tags = op_tags(l->file, li);
			for (int32 ci = 0; ci < tags->comments; ++ci) {
				const char *comment = tags->user_comments[ci];
				if (opus_tagncompare("METADATA_BLOCK_PICTURE", 22, comment) == 0) {
					OpusPictureTag pic;
					int err = opus_picture_tag_parse(&pic, comment);
					if (err >= 0) {
						opus_picture_tag_clear(&pic);
					}
				}
			}
			if (!op_seekable(l->file)) {
				l->pcm_offset = op_pcm_tell(l->file) - ret;
			}
		}
		if (li != l->prev_li || l->pcm_offset >= l->pcm_print_offset + AudioVoiceMsgFrequency) {
			l->pcm_print_offset = l->pcm_offset;
		}
		l->pcm_offset = op_pcm_tell(l->file);

		if (!ret) {
			DEBUG_LOG(("Audio Info: read completed"));
			finished = true;
			break;
		}
		result.append((const char*)pcm, sizeof(*pcm) * ret * AudioVoiceMsgChannels);
		l->prev_li = li;
		samplesAdded += ret;

		{
			QMutexLocker lock(&voicemsgsMutex);
			VoiceMessages *voice = audioVoice();
			if (!voice) return;

			VoiceMessages::Msg &m(voice->_data[audioindex]);
			if (m.audio != audio || !m.loading || m.fname != l->fname || m.data.size() != l->data.size()) {
				LOG(("Audio Error: playing changed while loading"));
				m.state = VoiceMessageStopped;
				return loadError(j);
			}
		}
	}

	QMutexLocker lock(&voicemsgsMutex);
	VoiceMessages *voice = audioVoice();
	if (!voice) return;

	VoiceMessages::Msg &m(voice->_data[audioindex]);
	if (m.audio != audio || !m.loading || m.fname != l->fname || m.data.size() != l->data.size()) {
		LOG(("Audio Error: playing changed while loading"));
		m.state = VoiceMessageStopped;
		return loadError(j);
	}

	if (started) {
		if (m.source) {
			alSourceStop(m.source);
			for (int32 i = 0; i < 3; ++i) {
				if (m.samplesCount[i]) {
					alSourceUnqueueBuffers(m.source, 1, m.buffers + i);
					m.samplesCount[i] = 0;
				}
			}
			m.nextBuffer = 0;
		}
	}
	if (samplesAdded) {
		if (!m.source) {
			alGenSources(1, &m.source);
			alSourcef(m.source, AL_PITCH, 1.f);
			alSourcef(m.source, AL_GAIN, 1.f);
			alSource3f(m.source, AL_POSITION, 0, 0, 0);
			alSource3f(m.source, AL_VELOCITY, 0, 0, 0);
			alSourcei(m.source, AL_LOOPING, 0);
		}
		if (!m.buffers[m.nextBuffer]) alGenBuffers(3, m.buffers);
		if (!_checkALError()) {
			m.state = VoiceMessageStopped;
			return loadError(j);
		}

		if (m.samplesCount[m.nextBuffer]) {
			alSourceUnqueueBuffers(m.source, 1, m.buffers + m.nextBuffer);
			m.skipStart += m.samplesCount[m.nextBuffer];
		}

		m.samplesCount[m.nextBuffer] = samplesAdded;
		alBufferData(m.buffers[m.nextBuffer], AL_FORMAT_STEREO16, result.constData(), result.size(), AudioVoiceMsgFrequency);
		alSourceQueueBuffers(m.source, 1, m.buffers + m.nextBuffer);
		m.skipEnd -= samplesAdded;

		m.nextBuffer = (m.nextBuffer + 1) % 3;

		if (!_checkALError()) {
			m.state = VoiceMessageStopped;
			return loadError(j);
		}
	} else {
		finished = true;
	}
	if (finished) {
		m.skipEnd = 0;
		m.duration = m.skipStart + m.samplesCount[0] + m.samplesCount[1] + m.samplesCount[2];
	}
	m.loading = false;
	if (m.state == VoiceMessageResuming || m.state == VoiceMessagePlaying || m.state == VoiceMessageStarting) {
		ALint state = AL_INITIAL;
		alGetSourcei(m.source, AL_SOURCE_STATE, &state);
		if (_checkALError()) {
			if (state != AL_PLAYING) {
				voice->processContext();
				alSourcePlay(m.source);
				emit needToCheck();
			}
		}
	}
}

void VoiceMessagesLoader::onCancel(AudioData *audio) {
	Loaders::iterator i = _loaders.find(audio);
	if (i != _loaders.end()) {
		delete (*i);
		_loaders.erase(i);
	}

	QMutexLocker lock(&voicemsgsMutex);
	VoiceMessages *voice = audioVoice();
	if (!voice) return;

	for (int32 i = 0; i < AudioVoiceMsgSimultaneously; ++i) {
		VoiceMessages::Msg &m(voice->_data[i]);
		if (m.audio == audio) {
			m.loading = false;
		}
	}
}
