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
#include "media/media_audio_loaders.h"

#include "media/media_audio.h"
#include "media/media_audio_ffmpeg_loader.h"
#include "media/media_child_ffmpeg_loader.h"

namespace Media {
namespace Player {

Loaders::Loaders(QThread *thread) : _fromVideoNotify([this] { videoSoundAdded(); }) {
	moveToThread(thread);
	_fromVideoNotify.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(onInit()));
	connect(thread, SIGNAL(finished()), this, SLOT(deleteLater()));
}

void Loaders::feedFromVideo(VideoSoundPart &&part) {
	auto invoke = false;
	{
		QMutexLocker lock(&_fromVideoMutex);
		_fromVideoQueues[part.audio].enqueue(FFMpeg::dataWrapFromPacket(*part.packet));
		invoke = true;
	}
	if (invoke) {
		_fromVideoNotify.call();
	}
}

void Loaders::videoSoundAdded() {
	auto waitingAndAdded = false;
	auto queues = decltype(_fromVideoQueues)();
	{
		QMutexLocker lock(&_fromVideoMutex);
		queues = base::take(_fromVideoQueues);
	}
	auto tryLoader = [this](auto &audio, auto &loader, auto &it) {
		if (audio == it.key() && loader) {
			loader->enqueuePackets(it.value());
			if (loader->holdsSavedDecodedSamples()) {
				onLoad(audio);
			}
			return true;
		}
		return false;
	};
	for (auto i = queues.begin(), e = queues.end(); i != e; ++i) {
		if (!tryLoader(_audio, _audioLoader, i)
			&& !tryLoader(_song, _songLoader, i)
			&& !tryLoader(_video, _videoLoader, i)) {
			for (auto &packetData : i.value()) {
				AVPacket packet;
				FFMpeg::packetFromDataWrap(packet, packetData);
				FFMpeg::freePacket(&packet);
			}
		}
	}
	if (waitingAndAdded) {
		onLoad(_video);
	}
}

Loaders::~Loaders() {
	QMutexLocker lock(&_fromVideoMutex);
	clearFromVideoQueue();
}

void Loaders::clearFromVideoQueue() {
	auto queues = base::take(_fromVideoQueues);
	for (auto &queue : queues) {
		for (auto &packetData : queue) {
			AVPacket packet;
			FFMpeg::packetFromDataWrap(packet, packetData);
			FFMpeg::freePacket(&packet);
		}
	}
}

void Loaders::onInit() {
}

void Loaders::onStart(const AudioMsgId &audio, qint64 positionMs) {
	auto type = audio.type();
	clear(type);
	{
		QMutexLocker lock(internal::audioPlayerMutex());
		if (!mixer()) return;

		auto track = mixer()->trackForType(type);
		if (!track) return;

		track->loading = true;
	}

	loadData(audio, positionMs);
}

AudioMsgId Loaders::clear(AudioMsgId::Type type) {
	AudioMsgId result;
	switch (type) {
	case AudioMsgId::Type::Voice: std::swap(result, _audio); _audioLoader = nullptr; break;
	case AudioMsgId::Type::Song: std::swap(result, _song); _songLoader = nullptr; break;
	case AudioMsgId::Type::Video: std::swap(result, _video); _videoLoader = nullptr; break;
	}
	return result;
}

void Loaders::setStoppedState(Mixer::Track *track, State state) {
	mixer()->setStoppedState(track, state);
}

void Loaders::emitError(AudioMsgId::Type type) {
	emit error(clear(type));
}

void Loaders::onLoad(const AudioMsgId &audio) {
	loadData(audio, TimeMs(0));
}

void Loaders::loadData(AudioMsgId audio, TimeMs positionMs) {
	auto err = SetupNoErrorStarted;
	auto type = audio.type();
	auto l = setupLoader(audio, err, positionMs);
	if (!l) {
		if (err == SetupErrorAtStart) {
			emitError(type);
		}
		return;
	}

	auto started = (err == SetupNoErrorStarted);
	auto finished = false;
	auto waiting = false;
	auto errAtStart = started;

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
					if (auto track = checkLoader(type)) {
						track->state.state = State::StoppedAtStart;
					}
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
	auto track = checkLoader(type);
	if (!track) {
		clear(type);
		return;
	}

	if (started) {
		Audio::AttachToDevice();

		track->started();
		if (!internal::audioCheckError()) {
			setStoppedState(track, State::StoppedAtStart);
			emitError(type);
			return;
		}

		track->format = l->format();
		track->frequency = l->samplesFrequency();

		const auto position = (positionMs * track->frequency) / 1000LL;
		track->bufferedPosition = position;
		track->state.position = position;
		track->fadeStartPosition = position;
	}
	if (samplesCount) {
		track->ensureStreamCreated();

		auto bufferIndex = track->getNotQueuedBufferIndex();

		if (!internal::audioCheckError()) {
			setStoppedState(track, State::StoppedAtError);
			emitError(type);
			return;
		}

		if (bufferIndex < 0) { // No free buffers, wait.
			l->saveDecodedSamples(&samples, &samplesCount);
			return;
		}

		track->bufferSamples[bufferIndex] = samples;
		track->samplesCount[bufferIndex] = samplesCount;
		track->bufferedLength += samplesCount;
		alBufferData(track->stream.buffers[bufferIndex], track->format, samples.constData(), samples.size(), track->frequency);

		alSourceQueueBuffers(track->stream.source, 1, track->stream.buffers + bufferIndex);

		if (!internal::audioCheckError()) {
			setStoppedState(track, State::StoppedAtError);
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
		track->loaded = true;
		track->state.length = track->bufferedPosition + track->bufferedLength;
		clear(type);
	}

	track->loading = false;
	if (track->state.state == State::Resuming || track->state.state == State::Playing || track->state.state == State::Starting) {
		ALint state = AL_INITIAL;
		alGetSourcei(track->stream.source, AL_SOURCE_STATE, &state);
		if (internal::audioCheckError()) {
			if (state != AL_PLAYING) {
				if (state == AL_STOPPED && !internal::CheckAudioDeviceConnected()) {
					return;
				}

				alSourcef(track->stream.source, AL_GAIN, ComputeVolume(type));
				if (!internal::audioCheckError()) {
					setStoppedState(track, State::StoppedAtError);
					emitError(type);
					return;
				}

				alSourcePlay(track->stream.source);
				if (!internal::audioCheckError()) {
					setStoppedState(track, State::StoppedAtError);
					emitError(type);
					return;
				}

				emit needToCheck();
			}
		} else {
			setStoppedState(track, State::StoppedAtError);
			emitError(type);
		}
	}
}

AudioPlayerLoader *Loaders::setupLoader(
		const AudioMsgId &audio,
		SetupError &err,
		TimeMs positionMs) {
	err = SetupErrorAtStart;
	QMutexLocker lock(internal::audioPlayerMutex());
	if (!mixer()) return nullptr;

	auto track = mixer()->trackForType(audio.type());
	if (!track || track->state.id != audio || !track->loading) {
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

	if (l && (!isGoodId || !l->check(track->file, track->data))) {
		clear(audio.type());
		l = nullptr;
	}

	if (!l) {
		std::unique_ptr<AudioPlayerLoader> *loader = nullptr;
		switch (audio.type()) {
		case AudioMsgId::Type::Voice: _audio = audio; loader = &_audioLoader; break;
		case AudioMsgId::Type::Song: _song = audio; loader = &_songLoader; break;
		case AudioMsgId::Type::Video: _video = audio; loader = &_videoLoader; break;
		}

		if (audio.playId()) {
			if (!track->videoData) {
				clear(audio.type());
				track->state.state = State::StoppedAtError;
				emit error(audio);
				LOG(("Audio Error: video sound data not ready"));
				return nullptr;
			}
			*loader = std::make_unique<ChildFFMpegLoader>(std::move(track->videoData));
		} else {
			*loader = std::make_unique<FFMpegLoader>(track->file, track->data, base::byte_vector());
		}
		l = loader->get();

		if (!l->open(positionMs)) {
			track->state.state = State::StoppedAtStart;
			return nullptr;
		}
		auto length = l->samplesCount();
		if (length <= 0) {
			track->state.state = State::StoppedAtStart;
			return nullptr;
		}
		track->state.length = length;
		track->state.frequency = l->samplesFrequency();
		err = SetupNoErrorStarted;
	} else if (track->loaded) {
		err = SetupErrorLoadedFull;
		LOG(("Audio Error: trying to load part of audio, that is already loaded to the end"));
		return nullptr;
	}
	return l;
}

Mixer::Track *Loaders::checkLoader(AudioMsgId::Type type) {
	if (!mixer()) return nullptr;

	auto track = mixer()->trackForType(type);
	auto isGoodId = false;
	AudioPlayerLoader *l = nullptr;
	switch (type) {
	case AudioMsgId::Type::Voice: l = _audioLoader.get(); isGoodId = (track->state.id == _audio); break;
	case AudioMsgId::Type::Song: l = _songLoader.get(); isGoodId = (track->state.id == _song); break;
	case AudioMsgId::Type::Video: l = _videoLoader.get(); isGoodId = (track->state.id == _video); break;
	}
	if (!l || !track) return nullptr;

	if (!isGoodId || !track->loading || !l->check(track->file, track->data)) {
		LOG(("Audio Error: playing changed while loading"));
		return nullptr;
	}

	return track;
}

void Loaders::onCancel(const AudioMsgId &audio) {
	switch (audio.type()) {
	case AudioMsgId::Type::Voice: if (_audio == audio) clear(audio.type()); break;
	case AudioMsgId::Type::Song: if (_song == audio) clear(audio.type()); break;
	case AudioMsgId::Type::Video: if (_video == audio) clear(audio.type()); break;
	}

	QMutexLocker lock(internal::audioPlayerMutex());
	if (!mixer()) return;

	for (auto i = 0; i != kTogetherLimit; ++i) {
		auto track = mixer()->trackForType(audio.type(), i);
		if (track->state.id == audio) {
			track->loading = false;
		}
	}
}

} // namespace Player
} // namespace Media
