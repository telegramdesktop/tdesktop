/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio_loaders.h"

#include "media/audio/media_audio.h"
#include "media/audio/media_audio_ffmpeg_loader.h"
#include "media/audio/media_child_ffmpeg_loader.h"

namespace Media {
namespace Player {
namespace {

constexpr auto kPlaybackBufferSize = 256 * 1024;

} // namespace

Loaders::Loaders(QThread *thread)
: _fromExternalNotify([=] { videoSoundAdded(); }) {
	moveToThread(thread);
	_fromExternalNotify.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(onInit()));
	connect(thread, SIGNAL(finished()), this, SLOT(deleteLater()));
}

void Loaders::feedFromExternal(ExternalSoundPart &&part) {
	auto invoke = false;
	{
		QMutexLocker lock(&_fromExternalMutex);
		invoke = _fromExternalQueues.empty()
			&& _fromExternalForceToBuffer.empty();
		auto &queue = _fromExternalQueues[part.audio];
		queue.insert(
			end(queue),
			std::make_move_iterator(part.packets.begin()),
			std::make_move_iterator(part.packets.end()));
	}
	if (invoke) {
		_fromExternalNotify.call();
	}
}

void Loaders::forceToBufferExternal(const AudioMsgId &audioId) {
	auto invoke = false;
	{
		QMutexLocker lock(&_fromExternalMutex);
		invoke = _fromExternalQueues.empty()
			&& _fromExternalForceToBuffer.empty();
		_fromExternalForceToBuffer.emplace(audioId);
	}
	if (invoke) {
		_fromExternalNotify.call();
	}
}

void Loaders::videoSoundAdded() {
	auto queues = decltype(_fromExternalQueues)();
	auto forces = decltype(_fromExternalForceToBuffer)();
	{
		QMutexLocker lock(&_fromExternalMutex);
		queues = base::take(_fromExternalQueues);
		forces = base::take(_fromExternalForceToBuffer);
	}
	for (const auto &audioId : forces) {
		const auto tryLoader = [&](const auto &id, auto &loader) {
			if (audioId == id && loader) {
				loader->setForceToBuffer(true);
				if (loader->holdsSavedDecodedSamples()
					&& !queues.contains(audioId)) {
					loadData(audioId);
				}
				return true;
			}
			return false;
		};
		tryLoader(_audio, _audioLoader)
			|| tryLoader(_song, _songLoader)
			|| tryLoader(_video, _videoLoader);
	}
	for (auto &pair : queues) {
		const auto audioId = pair.first;
		auto &packets = pair.second;
		const auto tryLoader = [&](const auto &id, auto &loader) {
			if (id == audioId && loader) {
				loader->enqueuePackets(std::move(packets));
				if (loader->holdsSavedDecodedSamples()) {
					loadData(audioId);
				}
				return true;
			}
			return false;
		};
		tryLoader(_audio, _audioLoader)
			|| tryLoader(_song, _songLoader)
			|| tryLoader(_video, _videoLoader);
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
	case AudioMsgId::Type::Voice:
		std::swap(result, _audio);
		_audioLoader = nullptr;
		break;
	case AudioMsgId::Type::Song:
		std::swap(result, _song);
		_songLoader = nullptr;
		break;
	case AudioMsgId::Type::Video:
		std::swap(result, _video);
		_videoLoader = nullptr;
		break;
	}
	return result;
}

void Loaders::setStoppedState(Mixer::Track *track, State state) {
	mixer()->setStoppedState(track, state);
}

void Loaders::emitError(AudioMsgId::Type type) {
	error(clear(type));
}

void Loaders::onLoad(const AudioMsgId &audio) {
	loadData(audio);
}

void Loaders::loadData(AudioMsgId audio, crl::time positionMs) {
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
	while (samples.size() < kPlaybackBufferSize) {
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
			waiting = (samples.size() < kPlaybackBufferSize)
				&& (!samplesCount || !l->forceToBuffer());
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

	if (started || samplesCount) {
		Audio::AttachToDevice();
	}
	if (started) {
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
		track->ensureStreamCreated(type);

		auto bufferIndex = track->getNotQueuedBufferIndex();

		if (!internal::audioCheckError()) {
			setStoppedState(track, State::StoppedAtError);
			emitError(type);
			return;
		}

		if (bufferIndex < 0) { // No free buffers, wait.
			l->saveDecodedSamples(&samples, &samplesCount);
			return;
		} else if (l->forceToBuffer()) {
			l->setForceToBuffer(false);
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
	track->state.waitingForData = false;

	if (finished) {
		track->loaded = true;
		track->state.length = track->bufferedPosition + track->bufferedLength;
		clear(type);
	}

	track->loading = false;
	if (IsPausedOrPausing(track->state.state)
		|| IsStoppedOrStopping(track->state.state)) {
		return;
	}
	ALint state = AL_INITIAL;
	alGetSourcei(track->stream.source, AL_SOURCE_STATE, &state);
	if (!internal::audioCheckError()) {
		setStoppedState(track, State::StoppedAtError);
		emitError(type);
		return;
	}

	if (state == AL_PLAYING) {
		return;
	} else if (state == AL_STOPPED && !internal::CheckAudioDeviceConnected()) {
		return;
	}

	alSourcef(track->stream.source, AL_GAIN, ComputeVolume(type));
	if (!internal::audioCheckError()) {
		setStoppedState(track, State::StoppedAtError);
		emitError(type);
		return;
	}

	if (state == AL_STOPPED) {
		alSourcei(track->stream.source, AL_SAMPLE_OFFSET, qMax(track->state.position - track->bufferedPosition, 0LL));
		if (!internal::audioCheckError()) {
			setStoppedState(track, State::StoppedAtError);
			emitError(type);
			return;
		}
	}
	alSourcePlay(track->stream.source);
	if (!internal::audioCheckError()) {
		setStoppedState(track, State::StoppedAtError);
		emitError(type);
		return;
	}

	needToCheck();
}

AudioPlayerLoader *Loaders::setupLoader(
		const AudioMsgId &audio,
		SetupError &err,
		crl::time positionMs) {
	err = SetupErrorAtStart;
	QMutexLocker lock(internal::audioPlayerMutex());
	if (!mixer()) return nullptr;

	auto track = mixer()->trackForType(audio.type());
	if (!track || track->state.id != audio || !track->loading) {
		error(audio);
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

		if (audio.externalPlayId()) {
			if (!track->externalData) {
				clear(audio.type());
				track->state.state = State::StoppedAtError;
				error(audio);
				LOG(("Audio Error: video sound data not ready"));
				return nullptr;
			}
			*loader = std::make_unique<ChildFFMpegLoader>(
				std::move(track->externalData));
		} else {
			*loader = std::make_unique<FFMpegLoader>(
				track->file,
				track->data,
				bytes::vector());
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
	Expects(audio.type() != AudioMsgId::Type::Unknown);

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

Loaders::~Loaders() = default;

} // namespace Player
} // namespace Media
