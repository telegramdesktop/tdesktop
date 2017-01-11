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
#include "media/media_audio_loaders.h"

#include "media/media_audio.h"
#include "media/media_audio_ffmpeg_loader.h"
#include "media/media_child_ffmpeg_loader.h"

AudioPlayerLoaders::AudioPlayerLoaders(QThread *thread) : _fromVideoNotify(this, "onVideoSoundAdded") {
	moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(onInit()));
	connect(thread, SIGNAL(finished()), this, SLOT(deleteLater()));
}

void AudioPlayerLoaders::feedFromVideo(VideoSoundPart &&part) {
	bool invoke = false;
	{
		QMutexLocker lock(&_fromVideoMutex);
		if (_fromVideoPlayId == part.videoPlayId) {
			_fromVideoQueue.enqueue(FFMpeg::dataWrapFromPacket(*part.packet));
			invoke = true;
		} else {
			FFMpeg::freePacket(part.packet);
		}
	}
	if (invoke) {
		_fromVideoNotify.call();
	}
}

void AudioPlayerLoaders::startFromVideo(uint64 videoPlayId) {
	QMutexLocker lock(&_fromVideoMutex);
	_fromVideoPlayId = videoPlayId;
	clearFromVideoQueue();
}

void AudioPlayerLoaders::stopFromVideo() {
	startFromVideo(0);
}

void AudioPlayerLoaders::onVideoSoundAdded() {
	bool waitingAndAdded = false;
	{
		QMutexLocker lock(&_fromVideoMutex);
		if (_videoLoader && _videoLoader->playId() == _fromVideoPlayId && !_fromVideoQueue.isEmpty()) {
			_videoLoader->enqueuePackets(_fromVideoQueue);
			waitingAndAdded = _videoLoader->holdsSavedDecodedSamples();
		}
	}
	if (waitingAndAdded) {
		onLoad(_video);
	}
}

AudioPlayerLoaders::~AudioPlayerLoaders() {
	QMutexLocker lock(&_fromVideoMutex);
	clearFromVideoQueue();
}

void AudioPlayerLoaders::clearFromVideoQueue() {
	auto queue = base::take(_fromVideoQueue);
	for (auto &packetData : queue) {
		AVPacket packet;
		FFMpeg::packetFromDataWrap(packet, packetData);
		FFMpeg::freePacket(&packet);
	}
}

void AudioPlayerLoaders::onInit() {
}

void AudioPlayerLoaders::onStart(const AudioMsgId &audio, qint64 position) {
	auto type = audio.type();
	clear(type);
	{
		QMutexLocker lock(internal::audioPlayerMutex());
		AudioPlayer *voice = audioPlayer();
		if (!voice) return;

		auto data = voice->dataForType(type);
		if (!data) return;

		data->loading = true;
	}

	loadData(audio, position);
}

AudioMsgId AudioPlayerLoaders::clear(AudioMsgId::Type type) {
	AudioMsgId result;
	switch (type) {
	case AudioMsgId::Type::Voice: std::swap(result, _audio); _audioLoader = nullptr; break;
	case AudioMsgId::Type::Song: std::swap(result, _song); _songLoader = nullptr; break;
	case AudioMsgId::Type::Video: std::swap(result, _video); _videoLoader = nullptr; break;
	}
	return result;
}

void AudioPlayerLoaders::setStoppedState(AudioPlayer::AudioMsg *m, AudioPlayerState state) {
	m->playbackState.state = state;
	m->playbackState.position = 0;
}

void AudioPlayerLoaders::emitError(AudioMsgId::Type type) {
	emit error(clear(type));
}

void AudioPlayerLoaders::onLoad(const AudioMsgId &audio) {
	loadData(audio, 0);
}

void AudioPlayerLoaders::loadData(AudioMsgId audio, qint64 position) {
	SetupError err = SetupNoErrorStarted;
	auto type = audio.type();
	AudioPlayerLoader *l = setupLoader(audio, err, position);
	if (!l) {
		if (err == SetupErrorAtStart) {
			emitError(type);
		}
		return;
	}

	bool started = (err == SetupNoErrorStarted);
	bool finished = false;
	bool waiting = false;
	bool errAtStart = started;

	QByteArray samples;
	int64 samplesCount = 0;
	if (l->holdsSavedDecodedSamples()) {
		l->takeSavedDecodedSamples(&samples, &samplesCount);
	}
	while (samples.size() < AudioVoiceMsgBufferSize) {
		auto res = l->readMore(samples, samplesCount);
		using Result = AudioPlayerLoader::ReadResult;
		if (res == Result::Error) {
			if (errAtStart) {
				{
					QMutexLocker lock(internal::audioPlayerMutex());
					AudioPlayer::AudioMsg *m = checkLoader(type);
					if (m) m->playbackState.state = AudioPlayerStoppedAtStart;
				}
				emitError(type);
				return;
			}
			finished = true;
			break;
		} else if (res == Result::EndOfFile) {
			finished = true;
			break;
		} else if (res == Result::Ok) {
			errAtStart = false;
		} else if (res == Result::Wait) {
			waiting = (samples.size() < AudioVoiceMsgBufferSize);
			if (waiting) {
				l->saveDecodedSamples(&samples, &samplesCount);
			}
			break;
		}

		QMutexLocker lock(internal::audioPlayerMutex());
		if (!checkLoader(type)) {
			clear(type);
			return;
		}
	}

	QMutexLocker lock(internal::audioPlayerMutex());
	AudioPlayer::AudioMsg *m = checkLoader(type);
	if (!m) {
		clear(type);
		return;
	}

	if (started) {
		if (m->source) {
			alSourceStop(m->source);
			for (int32 i = 0; i < 3; ++i) {
				if (m->samplesCount[i]) {
					ALuint buffer = 0;
					alSourceUnqueueBuffers(m->source, 1, &buffer);
					m->samplesCount[i] = 0;
				}
			}
			m->nextBuffer = 0;
		}
		m->skipStart = position;
		m->skipEnd = m->playbackState.duration - position;
		m->playbackState.position = position;
		m->started = 0;
	}
	if (samplesCount) {
		if (!m->source) {
			alGenSources(1, &m->source);
			alSourcef(m->source, AL_PITCH, 1.f);
			alSource3f(m->source, AL_POSITION, 0, 0, 0);
			alSource3f(m->source, AL_VELOCITY, 0, 0, 0);
			alSourcei(m->source, AL_LOOPING, 0);
		}
		if (!m->buffers[m->nextBuffer]) {
			alGenBuffers(3, m->buffers);
		}

		// If this buffer is queued, try to unqueue some buffer.
		if (m->samplesCount[m->nextBuffer]) {
			ALint processed = 0;
			alGetSourcei(m->source, AL_BUFFERS_PROCESSED, &processed);
			if (processed < 1) { // No processed buffers, wait.
				l->saveDecodedSamples(&samples, &samplesCount);
				return;
			}

			// Unqueue some processed buffer.
			ALuint buffer = 0;
			alSourceUnqueueBuffers(m->source, 1, &buffer);
			if (!internal::audioCheckError()) {
				setStoppedState(m, AudioPlayerStoppedAtError);
				emitError(type);
				return;
			}

			// Find it in the list and make it the nextBuffer.
			bool found = false;
			for (int i = 0; i < 3; ++i) {
				if (m->buffers[i] == buffer) {
					found = true;
					m->nextBuffer = i;
					break;
				}
			}
			if (!found) {
				LOG(("Audio Error: Could not find the unqueued buffer! Buffer %1 in source %2 with processed count %3").arg(buffer).arg(m->source).arg(processed));
				setStoppedState(m, AudioPlayerStoppedAtError);
				emitError(type);
				return;
			}

			if (m->samplesCount[m->nextBuffer]) {
				m->skipStart += m->samplesCount[m->nextBuffer];
				m->samplesCount[m->nextBuffer] = 0;
			}
		}

		auto frequency = l->frequency();
		auto format = l->format();
		m->samplesCount[m->nextBuffer] = samplesCount;
		alBufferData(m->buffers[m->nextBuffer], format, samples.constData(), samples.size(), frequency);

		alSourceQueueBuffers(m->source, 1, m->buffers + m->nextBuffer);
		m->skipEnd -= samplesCount;

		m->nextBuffer = (m->nextBuffer + 1) % 3;

		if (!internal::audioCheckError()) {
			setStoppedState(m, AudioPlayerStoppedAtError);
			emitError(type);
			return;
		}
	} else {
		if (waiting) {
			return;
		}
		finished = true;
	}

	if (finished) {
		m->skipEnd = 0;
		m->playbackState.duration = m->skipStart + m->samplesCount[0] + m->samplesCount[1] + m->samplesCount[2];
		clear(type);
	}

	m->loading = false;
	if (m->playbackState.state == AudioPlayerResuming || m->playbackState.state == AudioPlayerPlaying || m->playbackState.state == AudioPlayerStarting) {
		ALint state = AL_INITIAL;
		alGetSourcei(m->source, AL_SOURCE_STATE, &state);
		if (internal::audioCheckError()) {
			if (state != AL_PLAYING) {
				audioPlayer()->resumeDevice();

				switch (type) {
				case AudioMsgId::Type::Voice: alSourcef(m->source, AL_GAIN, internal::audioSuppressGain()); break;
				case AudioMsgId::Type::Song: alSourcef(m->source, AL_GAIN, internal::audioSuppressSongGain() * Global::SongVolume()); break;
				case AudioMsgId::Type::Video: alSourcef(m->source, AL_GAIN, internal::audioSuppressSongGain() * Global::VideoVolume()); break;
				}
				if (!internal::audioCheckError()) {
					setStoppedState(m, AudioPlayerStoppedAtError);
					emitError(type);
					return;
				}

				alSourcePlay(m->source);
				if (!internal::audioCheckError()) {
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

AudioPlayerLoader *AudioPlayerLoaders::setupLoader(const AudioMsgId &audio, SetupError &err, qint64 &position) {
	err = SetupErrorAtStart;
	QMutexLocker lock(internal::audioPlayerMutex());
	AudioPlayer *voice = audioPlayer();
	if (!voice) return nullptr;

	auto data = voice->dataForType(audio.type());
	if (!data || data->audio != audio || !data->loading) {
		emit error(audio);
		LOG(("Audio Error: trying to load part of audio, that is not current at the moment"));
		err = SetupErrorNotPlaying;
		return nullptr;
	}

	bool isGoodId = false;
	AudioPlayerLoader *l = nullptr;
	switch (audio.type()) {
	case AudioMsgId::Type::Voice: l = _audioLoader.get(); isGoodId = (_audio == audio); break;
	case AudioMsgId::Type::Song: l = _songLoader.get(); isGoodId = (_song == audio); break;
	case AudioMsgId::Type::Video: l = _videoLoader.get(); isGoodId = (_video == audio); break;
	}

	if (l && (!isGoodId || !l->check(data->file, data->data))) {
		clear(audio.type());
		l = nullptr;
	}

	if (!l) {
		std_::unique_ptr<AudioPlayerLoader> *loader = nullptr;
		switch (audio.type()) {
		case AudioMsgId::Type::Voice: _audio = audio; loader = &_audioLoader; break;
		case AudioMsgId::Type::Song: _song = audio; loader = &_songLoader; break;
		case AudioMsgId::Type::Video: _video = audio; break;
		}

		if (audio.type() == AudioMsgId::Type::Video) {
			if (!data->videoData) {
				data->playbackState.state = AudioPlayerStoppedAtError;
				emit error(audio);
				LOG(("Audio Error: video sound data not ready"));
				return nullptr;
			}
			_videoLoader = std_::make_unique<ChildFFMpegLoader>(data->videoPlayId, std_::move(data->videoData));
			l = _videoLoader.get();
		} else {
			*loader = std_::make_unique<FFMpegLoader>(data->file, data->data);
			l = loader->get();
		}

		if (!l->open(position)) {
			data->playbackState.state = AudioPlayerStoppedAtStart;
			return nullptr;
		}
		int64 duration = l->duration();
		if (duration <= 0) {
			data->playbackState.state = AudioPlayerStoppedAtStart;
			return nullptr;
		}
		data->playbackState.duration = duration;
		data->playbackState.frequency = l->frequency();
		if (!data->playbackState.frequency) data->playbackState.frequency = AudioVoiceMsgFrequency;
		err = SetupNoErrorStarted;
	} else {
		if (!data->skipEnd) {
			err = SetupErrorLoadedFull;
			LOG(("Audio Error: trying to load part of audio, that is already loaded to the end"));
			return nullptr;
		}
	}
	return l;
}

AudioPlayer::AudioMsg *AudioPlayerLoaders::checkLoader(AudioMsgId::Type type) {
	AudioPlayer *voice = audioPlayer();
	if (!voice) return 0;

	auto data = voice->dataForType(type);
	bool isGoodId = false;
	AudioPlayerLoader *l = nullptr;
	switch (type) {
	case AudioMsgId::Type::Voice: l = _audioLoader.get(); isGoodId = (data->audio == _audio); break;
	case AudioMsgId::Type::Song: l = _songLoader.get(); isGoodId = (data->audio == _song); break;
	case AudioMsgId::Type::Video: l = _videoLoader.get(); isGoodId = (data->audio == _video); break;
	}
	if (!l || !data) return nullptr;

	if (!isGoodId || !data->loading || !l->check(data->file, data->data)) {
		LOG(("Audio Error: playing changed while loading"));
		return nullptr;
	}

	return data;
}

void AudioPlayerLoaders::onCancel(const AudioMsgId &audio) {
	switch (audio.type()) {
	case AudioMsgId::Type::Voice: if (_audio == audio) clear(audio.type()); break;
	case AudioMsgId::Type::Song: if (_song == audio) clear(audio.type()); break;
	case AudioMsgId::Type::Video: if (_video == audio) clear(audio.type()); break;
	}

	QMutexLocker lock(internal::audioPlayerMutex());
	AudioPlayer *voice = audioPlayer();
	if (!voice) return;

	for (int i = 0; i < AudioSimultaneousLimit; ++i) {
		auto data = voice->dataForType(audio.type(), i);
		if (data->audio == audio) {
			data->loading = false;
		}
	}
}
