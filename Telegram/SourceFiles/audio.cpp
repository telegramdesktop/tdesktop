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

#include <mpg123.h>
#include <mpeghead.h>

#include <neaacdec.h>
#include <mp4ff.h>

namespace {
	ALCdevice *audioDevice = 0;
	ALCcontext *audioContext = 0;
	ALuint notifySource = 0;
	ALuint notifyBuffer = 0;
	QMutex voicemsgsMutex;
	VoiceMessages *voicemsgs = 0;
	bool _mpg123 = false;
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

	int mpg123res = mpg123_init();
	if (mpg123res == MPG123_OK) {
		_mpg123 = true;
	} else {
		LOG(("Could not init MPG123, result: %1").arg(mpg123res));
	}

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

	if (_mpg123) mpg123_exit();
}

VoiceMessages::VoiceMessages() : _current(0),
_fader(new VoiceMessagesFader(&_faderThread)), _loader(new VoiceMessagesLoaders(&_loaderThread)) {
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

void VoiceMessages::currentState(AudioData **audio, VoiceMessageState *state, int64 *position, int64 *duration, int32 *frequency) {
	QMutexLocker lock(&voicemsgsMutex);
	if (audio) *audio = _data[_current].audio;
	if (state) *state = _data[_current].state;
	if (position) *position = _data[_current].position;
	if (duration) *duration = _data[_current].duration;
	if (frequency) *frequency = _data[_current].frequency;
}

void VoiceMessages::clearStoppedAtStart(AudioData *audio) {
	QMutexLocker lock(&voicemsgsMutex);
	if (_data[_current].audio == audio && _data[_current].state == VoiceMessageStoppedAtStart) {
		_data[_current].state = VoiceMessageStopped;
	}
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
		if (m.state == VoiceMessageStopped || m.state == VoiceMessageStoppedAtStart || m.state == VoiceMessagePaused || !m.source) continue;

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
				} else if (1000 * (pos + m.skipStart - m.started) >= AudioFadeDuration * m.frequency) {
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
					float64 newGain = 1000. * (pos + m.skipStart - m.started) / (AudioFadeDuration * m.frequency);
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

class VoiceMessagesLoader {
public:
	VoiceMessagesLoader(const QString &fname, const QByteArray &data) : fname(fname), data(data), dataPos(0) {
	}
	virtual ~VoiceMessagesLoader() {
	}

	bool check(const QString &fname, const QByteArray &data) {
		return this->fname == fname && this->data.size() == data.size();
	}

	virtual bool open() = 0;
	virtual int64 duration() = 0;
	virtual int32 frequency() = 0;
	virtual int32 format() = 0;
	virtual void started() = 0;
	virtual bool readMore(QByteArray &result, int64 &samplesAdded) = 0;

protected:

	QString fname;
	QByteArray data;

	QFile f;
	int32 dataPos;
	
	bool openFile() {
		if (data.isEmpty()) {
			if (f.isOpen()) f.close();
			f.setFileName(fname);
			if (!f.open(QIODevice::ReadOnly)) {
				LOG(("Audio Error: could not open file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(f.error()).arg(f.errorString()));
				return false;
			}
		}
		dataPos = 0;
		return true;
	}

};

class OggOpusLoader : public VoiceMessagesLoader {
public:
	OggOpusLoader(const QString &fname, const QByteArray &data) : VoiceMessagesLoader(fname, data), file(0), pcm_offset(0), pcm_print_offset(0), prev_li(-1) {
	}

	bool open() {
		if (!VoiceMessagesLoader::openFile()) {
			return false;
		}

		OpusFileCallbacks cb = { &OggOpusLoader::_read_data, &OggOpusLoader::_seek_data, &OggOpusLoader::_tell_data, 0 };
		if (data.isEmpty()) {
			cb = { &OggOpusLoader::_read_file, &OggOpusLoader::_seek_file, &OggOpusLoader::_tell_file, 0 };
		}

		int ret = 0;
		file = op_open_callbacks(reinterpret_cast<void*>(this), &cb, 0, 0, &ret);
		if (!file) {
			LOG(("Audio Error: op_open_file failed for '%1', data size '%2', error code %3").arg(fname).arg(data.size()).arg(ret));
			return false;
		}
		return true;
	}

	int64 duration() {
		ogg_int64_t duration = op_pcm_total(file, -1);
		if (duration < 0) {
			LOG(("Audio Error: op_pcm_total failed to get full duration for '%1', data size '%2', error code %3").arg(fname).arg(data.size()).arg(duration));
		}
		return duration;
	}

	int32 frequency() {
		return AudioVoiceMsgFrequency;
	}

	int32 format() {
		return AL_FORMAT_STEREO16;
	}

	void started() {
		pcm_offset = op_pcm_tell(file);
		pcm_print_offset = pcm_offset - AudioVoiceMsgFrequency;
	}

	bool readMore(QByteArray &result, int64 &samplesAdded) {
		DEBUG_LOG(("Audio Info: reading buffer for file '%1', data size '%2', current pcm_offset %3").arg(fname).arg(data.size()).arg(pcm_offset));
		
		opus_int16 pcm[AudioVoiceMsgFrequency * AudioVoiceMsgChannels];

		int ret = op_read_stereo(file, pcm, sizeof(pcm) / sizeof(*pcm));
		if (ret < 0) {
			LOG(("Audio Error: op_read_stereo failed, error code %1 (corrupted voice message?)").arg(ret));
			return false;
		}

		int li = op_current_link(file);
		if (li != prev_li) {
			const OpusHead *head = op_head(file, li);
			const OpusTags *tags = op_tags(file, li);
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
			if (!op_seekable(file)) {
				pcm_offset = op_pcm_tell(file) - ret;
			}
		}
		if (li != prev_li || pcm_offset >= pcm_print_offset + AudioVoiceMsgFrequency) {
			pcm_print_offset = pcm_offset;
		}
		pcm_offset = op_pcm_tell(file);

		if (!ret) {
			DEBUG_LOG(("Audio Info: read completed"));
			return false;
		}
		result.append((const char*)pcm, sizeof(*pcm) * ret * AudioVoiceMsgChannels);
		prev_li = li;
		samplesAdded += ret;
		return true;
	}

	~OggOpusLoader() {
	}

private:
	OggOpusFile *file;

	ogg_int64_t pcm_offset;
	ogg_int64_t pcm_print_offset;
	int prev_li;

	static int _read_data(void *_stream, unsigned char *_ptr, int _nbytes) {
		OggOpusLoader *l = reinterpret_cast<OggOpusLoader*>(_stream);

		int32 nbytes = qMin(l->data.size() - l->dataPos, _nbytes);
		if (nbytes <= 0) {
			return 0;
		}

		memcpy(_ptr, l->data.constData() + l->dataPos, nbytes);
		l->dataPos += nbytes;
		return nbytes;
	}

	static int _seek_data(void *_stream, opus_int64 _offset, int _whence) {
		OggOpusLoader *l = reinterpret_cast<OggOpusLoader*>(_stream);

		int32 newPos = -1;
		switch (_whence) {
		case SEEK_SET: newPos = _offset; break;
		case SEEK_CUR: newPos = l->dataPos + _offset; break;
		case SEEK_END: newPos = l->data.size() + _offset; break;
		}
		if (newPos < 0 || newPos > l->data.size()) {
			return -1;
		}
		l->dataPos = newPos;
		return 0;
	}

	static opus_int64 _tell_data(void *_stream) {
		OggOpusLoader *l = reinterpret_cast<OggOpusLoader*>(_stream);
		return l->dataPos;
	}

	static int _read_file(void *_stream, unsigned char *_ptr, int _nbytes) {
		OggOpusLoader *l = reinterpret_cast<OggOpusLoader*>(_stream);
		return int(l->f.read((char*)(_ptr), _nbytes));
	}

	static int _seek_file(void *_stream, opus_int64 _offset, int _whence) {
		OggOpusLoader *l = reinterpret_cast<OggOpusLoader*>(_stream);

		switch (_whence) {
		case SEEK_SET: return l->f.seek(_offset) ? 0 : -1;
		case SEEK_CUR: return l->f.seek(l->f.pos() + _offset) ? 0 : -1;
		case SEEK_END: return l->f.seek(l->f.size() + _offset) ? 0 : -1;
		}
		return -1;
	}

	static opus_int64 _tell_file(void *_stream) {
		OggOpusLoader *l = reinterpret_cast<OggOpusLoader*>(_stream);
		return l->f.pos();
	}
};

class Mpg123Loader : public VoiceMessagesLoader {
public:
	Mpg123Loader(const QString &fname, const QByteArray &data) : VoiceMessagesLoader(fname, data),
		handle(0), opened(false), freq(AudioVoiceMsgFrequency), fmt(AL_FORMAT_STEREO16), channels(0) {
		int ret = 0;
		handle = mpg123_new(NULL, &ret);
		if (!handle) {
			LOG(("Audio Error: Unable to create mpg123 handle: %1\n").arg(mpg123_plain_strerror(ret)));
			return;
		}
		mpg123_param(handle, MPG123_REMOVE_FLAGS, MPG123_FORCE_FLOAT, 0.); // not float
	}

	bool open() {
		if (!VoiceMessagesLoader::openFile()) {
			return false;
		}

		int res;
		if (data.isEmpty()) {
			res = mpg123_replace_reader_handle(handle, &Mpg123Loader::_read_file, &Mpg123Loader::_seek_file, 0);
		} else {
			res = mpg123_replace_reader_handle(handle, &Mpg123Loader::_read_data, &Mpg123Loader::_seek_data, 0);
		}
		if (res != MPG123_OK) {
			LOG(("Audio Error: Unable to mpg123_replace_reader_handle() file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(res).arg(mpg123_strerror(handle)));
			return false;
		}
		res = mpg123_open_handle(handle, reinterpret_cast<void*>(this));
		if (res != MPG123_OK) {
			LOG(("Audio Error: Unable to mpg123_open() file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(res).arg(mpg123_strerror(handle)));
			return false;
		}
		opened = true;

		int encoding = 0;
		long rate = 0;
		res = mpg123_getformat(handle, &rate, &channels, &encoding);
		if (res != MPG123_OK) {
			LOG(("Audio Error: Unable to mpg123_getformat() file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(res).arg(mpg123_strerror(handle)));
			return false;
		}
		if (channels == 2) {
			if (encoding == MPG123_ENC_SIGNED_16) {
				fmt = AL_FORMAT_STEREO16;
			} else if (encoding == MPG123_ENC_UNSIGNED_8) {
				fmt = AL_FORMAT_STEREO8;
			} else {
				LOG(("Audio Error: Bad encoding for 2 channels in mpg123_getformat() file '%1', data size '%2', encoding %3").arg(fname).arg(data.size()).arg(encoding));
				return false;
			}
		} else if (channels == 1) {
			if (encoding == MPG123_ENC_SIGNED_16) {
				fmt = AL_FORMAT_MONO16;
			} else if (encoding == MPG123_ENC_UNSIGNED_8) {
				fmt = AL_FORMAT_MONO8;
			} else {
				LOG(("Audio Error: Bad encoding for 1 channel in mpg123_getformat() file '%1', data size '%2', encoding %3").arg(fname).arg(data.size()).arg(encoding));
				return false;
			}
		} else {
			LOG(("Audio Error: Bad channels in mpg123_getformat() file '%1', data size '%2', channels %3").arg(fname).arg(data.size()).arg(channels));
			return false;
		}
		freq = rate;

		mpg123_format_none(handle);
		res = mpg123_format(handle, freq, channels, encoding);
		if (res != MPG123_OK) {
			LOG(("Audio Error: Unable to mpg123_format() file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(res).arg(mpg123_strerror(handle)));
			return false;
		}
		return true;
	}

	int64 duration() {
		return mpg123_length(handle);
	}

	int32 frequency() {
		return freq;
	}

	int32 format() {
		return fmt;
	}

	void started() {
	}

	bool readMore(QByteArray &result, int64 &samplesAdded) {
		int64 more_samples;
		uchar buffer[sizeof(short) * AudioVoiceMsgFrequency * AudioVoiceMsgChannels];
		size_t buffer_size = sizeof(buffer), done = 0;
		int res = mpg123_read(handle, buffer, buffer_size, &done);
		if (done) {
			samplesAdded += done / (sizeof(short) * channels);
			result.append((const char*)buffer, done);
		}
		if (res == MPG123_DONE) return false;
		if (res != MPG123_OK) {
			LOG(("Audio Error: Unable to mpg123_read() file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(res).arg(mpg123_strerror(handle)));
			return false;
		}
		return true;
	}

	~Mpg123Loader() {
		if (handle) {
			if (opened) mpg123_close(handle);
			mpg123_delete(handle);
		}
	}

private:
	mpg123_handle *handle;
	bool opened;
	int32 freq, fmt;
	int32 channels;

	static ssize_t _read_data(void *_stream, void *_ptr, size_t _nbytes) {
		Mpg123Loader *l = reinterpret_cast<Mpg123Loader*>(_stream);

		int32 nbytes = qMin(l->data.size() - l->dataPos, int32(_nbytes));
		if (nbytes <= 0) {
			return 0;
		}

		memcpy(_ptr, l->data.constData() + l->dataPos, nbytes);
		l->dataPos += nbytes;
		return nbytes;
	}

	static off_t _seek_data(void *_stream, off_t _offset, int _whence) {
		Mpg123Loader *l = reinterpret_cast<Mpg123Loader*>(_stream);

		int32 newPos = -1;
		switch (_whence) {
		case SEEK_SET: newPos = _offset; break;
		case SEEK_CUR: newPos = l->dataPos + _offset; break;
		case SEEK_END: newPos = l->data.size() + _offset; break;
		}
		if (newPos < 0) {
			return -1;
		}
		l->dataPos = newPos;
		return l->dataPos;
	}

	static ssize_t _read_file(void *_stream, void *_ptr, size_t _nbytes) {
		Mpg123Loader *l = reinterpret_cast<Mpg123Loader*>(_stream);
		return ssize_t(l->f.read((char*)(_ptr), _nbytes));
	}

	static off_t _seek_file(void *_stream, off_t _offset, int _whence) {
		Mpg123Loader *l = reinterpret_cast<Mpg123Loader*>(_stream);

		switch (_whence) {
		case SEEK_SET: return l->f.seek(_offset) ? l->f.pos() : -1;
		case SEEK_CUR: return l->f.seek(l->f.pos() + _offset) ? l->f.pos() : -1;
		case SEEK_END: return l->f.seek(l->f.size() + _offset) ? l->f.pos() : -1;
		}
		return -1;
	}
};

class FAADMp4Loader : public VoiceMessagesLoader {
public:
	FAADMp4Loader(const QString &fname, const QByteArray &data) : VoiceMessagesLoader(fname, data),
freq(AudioVoiceMsgFrequency), fmt(AL_FORMAT_STEREO16), len(0),
mp4f(0),
initial(true), useAacLength(false),
framesize(1024), timescale(AudioVoiceMsgFrequency),
trackId(-1), sampleId(0), samplesCount(0) {
	}

	bool open() {
		if (!VoiceMessagesLoader::openFile()) {
			return false;
		}

		if (data.isEmpty()) {
			mp4cb = { &FAADMp4Loader::_read_file, 0, &FAADMp4Loader::_seek_file, 0, static_cast<void*>(this) };
		} else {
			mp4cb = { &FAADMp4Loader::_read_data, 0, &FAADMp4Loader::_seek_data, 0, static_cast<void*>(this) };
		}

		hDecoder = NeAACDecOpen();

		config = NeAACDecGetCurrentConfiguration(hDecoder);
		config->outputFormat = FAAD_FMT_16BIT;
		config->downMatrix = 1; // Down matrix 5.1 to 2 channels
		NeAACDecSetConfiguration(hDecoder, config);

		mp4f = mp4ff_open_read(&mp4cb);
		if (!mp4f) {
			LOG(("Audio Error: Unable to mp4ff_open_read() file '%1', data size '%2'").arg(fname).arg(data.size()));
			return false;
		}

		trackId = getAACTrack();
		if (trackId < 0) {
			LOG(("Audio Error: Unable to find correct AAC sound track in the MP4 file '%1', data size '%2'").arg(fname).arg(data.size()));
			return false;
		}

		uchar *buffer = 0;
		uint buffer_size = 0;
		mp4ff_get_decoder_config(mp4f, trackId, &buffer, &buffer_size);

		unsigned long samplerate = 0;
		uchar channels = 2;
		if (NeAACDecInit2(hDecoder, buffer, buffer_size, &samplerate, &channels) < 0) {
			free(buffer);
			LOG(("Audio Error: Error initializaing decoder library for file '%1', data size '%2'").arg(fname).arg(data.size()));
			return false;
		}
		freq = samplerate;
		switch (channels) {
		case 1: fmt = AL_FORMAT_MONO16; break;
		case 2: fmt = AL_FORMAT_STEREO16; break;
		}

		timescale = mp4ff_time_scale(mp4f, trackId);
		if (buffer) {
			if (NeAACDecAudioSpecificConfig(buffer, buffer_size, &mp4ASC) >= 0) {
				if (mp4ASC.frameLengthFlag == 1) framesize = 960;
				if (mp4ASC.sbr_present_flag == 1) framesize *= 2;
			}
			free(buffer);
		}

		samplesCount = mp4ff_num_samples(mp4f, trackId);
		int32 f = 1024;
		if (mp4ASC.sbr_present_flag == 1) {
			f *= 2;
		}
		len = int64(samplesCount) * (f - 1);
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

	void started() {
	}

	bool readMore(QByteArray &result, int64 &samplesAdded) {
		if (sampleId >= samplesCount) return false;

		int32 dur = mp4ff_get_sample_duration(mp4f, trackId, sampleId);

		uchar *buffer = 0;
		uint32 buffer_size = 0;
		if (!mp4ff_read_sample(mp4f, trackId, sampleId, &buffer, &buffer_size)) {
			LOG(("Audio Error: Unable to mp4ff_read_sample() file '%1', data size '%2'").arg(fname).arg(data.size()));
			return false;
		}

		void *sample_buffer = NeAACDecDecode(hDecoder, &frameInfo, buffer, buffer_size);

		if (buffer) free(buffer);

		if (sampleId == 0) dur = 0;

		uint32 sample_count = frameInfo.samples;
		if (!useAacLength && timescale == freq) {
			sample_count = (uint32)(dur * frameInfo.channels);
			if (sample_count > frameInfo.samples) {
				sample_count = frameInfo.samples;
			}

			if (!initial && (sampleId < samplesCount / 2) && (sample_count != frameInfo.samples)) {
				DEBUG_LOG(("Audio Warning: MP4 seems to have incorrect frame duration, using values from AAC data in file '%1', data size '%2'").arg(fname).arg(data.size()));
				useAacLength = true;
				sample_count = frameInfo.samples;
			}
		}

		uint32 delay = 0;
		if (initial && (sample_count < framesize * frameInfo.channels) && (frameInfo.samples > sample_count)) {
			delay = frameInfo.samples - sample_count;
		}

		switch (frameInfo.channels) {
		case 1: fmt = AL_FORMAT_MONO16; break;
		case 2: fmt = AL_FORMAT_STEREO16; break;
		}

		if (sample_count > 0) initial = false;

		if (frameInfo.error) {
			DEBUG_LOG(("Audio Warning: Read frame error in file '%1', data size '%2', error %3, %4").arg(fname).arg(data.size()).arg(frameInfo.error).arg(NeAACDecGetErrorMessage(frameInfo.error)));
		} else if (sample_count > 0) {
			samplesAdded += sample_count / frameInfo.channels;
			result.append((const char*)sample_buffer + delay * sizeof(short), sample_count * sizeof(short)); // delay
		}

		++sampleId;
		return true;
	}

	~FAADMp4Loader() {
		NeAACDecClose(hDecoder);
		mp4ff_close(mp4f);
	}

private:
	int32 freq, fmt;
	int64 len;

	NeAACDecHandle hDecoder;
	NeAACDecConfigurationPtr config;
	NeAACDecFrameInfo frameInfo;
	mp4AudioSpecificConfig mp4ASC;

	mp4ff_t *mp4f;
	mp4ff_callback_t mp4cb;

	bool initial, useAacLength;
	int32 framesize, timescale;
	int32 trackId, sampleId, samplesCount;

	int32 getAACTrack() {
		int32 rc;
		for (int32 i = 0, numTracks = mp4ff_total_tracks(mp4f); i < numTracks; i++)
		{
			uchar *buff = 0;
			uint32 buff_size = 0;
			mp4ff_get_decoder_config(mp4f, i, &buff, &buff_size);
			if (buff) {
				mp4AudioSpecificConfig mp4ASC;
				rc = NeAACDecAudioSpecificConfig(buff, buff_size, &mp4ASC);
				free(buff);
				if (rc < 0) continue;
				return i;
			}
		}
		return -1;
	}

	static uint32_t _read_data(void *_stream, void *_ptr, uint32_t _nbytes) {
		FAADMp4Loader *l = reinterpret_cast<FAADMp4Loader*>(_stream);

		int32 nbytes = qMin(l->data.size() - l->dataPos, int32(_nbytes));
		if (nbytes <= 0) {
			return 0;
		}

		memcpy(_ptr, l->data.constData() + l->dataPos, nbytes);
		l->dataPos += nbytes;
		return nbytes;
	}

	static uint32_t _seek_data(void *_stream, uint64_t _offset) {
		FAADMp4Loader *l = reinterpret_cast<FAADMp4Loader*>(_stream);

		int32 newPos = _offset;
		if (newPos < 0) {
			return (uint32_t)-1;
		}
		l->dataPos = newPos;
		return 0;
	}

	static uint32_t _read_file(void *_stream, void *_ptr, uint32_t _nbytes) {
		FAADMp4Loader *l = reinterpret_cast<FAADMp4Loader*>(_stream);
		return ssize_t(l->f.read((char*)(_ptr), _nbytes));
	}

	static uint32_t _seek_file(void *_stream, uint64_t _offset) {
		FAADMp4Loader *l = reinterpret_cast<FAADMp4Loader*>(_stream);

		return l->f.seek(_offset) ? 0 : (uint32_t)-1;
	}
};

VoiceMessagesLoaders::VoiceMessagesLoaders(QThread *thread) {
	moveToThread(thread);
}

VoiceMessagesLoaders::~VoiceMessagesLoaders() {
	for (Loaders::iterator i = _loaders.begin(), e = _loaders.end(); i != e; ++i) {
		delete i.value();
	}
	_loaders.clear();
}

void VoiceMessagesLoaders::onInit() {
}

void VoiceMessagesLoaders::onStart(AudioData *audio) {
	Loaders::iterator i = _loaders.find(audio);
	if (i != _loaders.end()) {
		delete (*i);
		_loaders.erase(i);
	}
	onLoad(audio);
}

void VoiceMessagesLoaders::loadError(Loaders::iterator i) {
	emit error(i.key());
	delete (*i);
	_loaders.erase(i);
}

void VoiceMessagesLoaders::onLoad(AudioData *audio) {
	bool started = false;
	int32 audioindex = -1;
	VoiceMessagesLoader *l = 0;
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
			if (j != _loaders.end() && !j.value()->check(m.fname, m.data)) {
				delete j.value();
				_loaders.erase(j);
				j = _loaders.end();
			}
			if (j == _loaders.end()) {
				QByteArray header = m.data.mid(0, 8);
				if (header.isEmpty()) {
					QFile f(m.fname);
					if (!f.open(QIODevice::ReadOnly)) {
						LOG(("Audio Error: could not open file '%1'").arg(m.fname));
						m.state = VoiceMessageStoppedAtStart;
						return emit error(audio);
					}
					header = f.read(8);
				}
				if (header.size() < 8) {
					LOG(("Audio Error: could not read header from file '%1', data size %2").arg(m.fname).arg(m.data.isEmpty() ? QFileInfo(m.fname).size() : m.data.size()));
					m.state = VoiceMessageStoppedAtStart;
					return emit error(audio);
				}
				uint32 mpegHead = (uint32(uchar(header.at(0))) << 24) | (uint32(uchar(header.at(1))) << 16) | (uint32(uchar(header.at(2))) << 8) | uint32(uchar(header.at(3)));
				bool validMpegHead = ((mpegHead & HDR_SYNC) == HDR_SYNC) && !!(HDR_LAYER_VAL(mpegHead)) && (HDR_BITRATE_VAL(mpegHead) != 0x0F) && (HDR_SAMPLERATE_VAL(mpegHead) != 0x03);

				if (header.at(0) == 'O' && header.at(1) == 'g' && header.at(2) == 'g' && header.at(3) == 'S') {
					j = _loaders.insert(audio, new OggOpusLoader(m.fname, m.data));
				} else if (header.at(4) == 'f' && header.at(5) == 't' && header.at(6) == 'y' && header.at(7) == 'p') {
					j = _loaders.insert(audio, new FAADMp4Loader(m.fname, m.data));
				} else if ((header.at(0) == 'I' && header.at(1) == 'D' && header.at(2) == '3') || validMpegHead) {
					if (m.data.isEmpty()) {
						QFile f(m.fname);
						f.open(QIODevice::ReadOnly);
						m.data = f.readAll();
						m.fname = QString();
					}
					j = _loaders.insert(audio, new Mpg123Loader(m.fname, m.data));
				} else {
					LOG(("Audio Error: could not guess file format from header, header %1 file '%2', data size %3").arg(mb(header.constData(), header.size()).str()).arg(m.fname).arg(m.data.isEmpty() ? QFileInfo(m.fname).size() : m.data.size()));
					m.state = VoiceMessageStoppedAtStart;
					return emit error(audio);
				}
				l = j.value();
				
				int ret;
				if (!l->open()) {
					m.state = VoiceMessageStoppedAtStart;
					return loadError(j);
				}
				int64 duration = l->duration();
				if (duration <= 0) {
					m.state = VoiceMessageStoppedAtStart;
					return loadError(j);
				}
				m.duration = duration;
				m.frequency = l->frequency();
				if (!m.frequency) m.frequency = AudioVoiceMsgFrequency;
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
		l->started();
	}

	bool finished = false;

	QByteArray result;
	int64 samplesAdded = 0, frequency = l->frequency(), format = l->format();
	while (result.size() < AudioVoiceMsgBufferSize) {
		if (!l->readMore(result, samplesAdded)) {
			finished = true;
			break;
		}
		{
			QMutexLocker lock(&voicemsgsMutex);
			VoiceMessages *voice = audioVoice();
			if (!voice) return;

			VoiceMessages::Msg &m(voice->_data[audioindex]);
			if (m.audio != audio || !m.loading || !l->check(m.fname, m.data)) {
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
	if (m.audio != audio || !m.loading || !l->check(m.fname, m.data)) {
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
		alBufferData(m.buffers[m.nextBuffer], format, result.constData(), result.size(), frequency);
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

void VoiceMessagesLoaders::onCancel(AudioData *audio) {
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
