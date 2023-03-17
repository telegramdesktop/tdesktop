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
#include "media/media_common.h"

namespace Media {
namespace Player {

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
	auto type = audio.type();
	auto setup = setupLoader(audio, positionMs);
	const auto l = setup.loader;
	if (!l) {
		if (setup.errorAtStart) {
			emitError(type);
		}
		return;
	}

	const auto sampleSize = l->sampleSize();
	const auto speedChanged = !EqualSpeeds(setup.newSpeed, setup.oldSpeed);
	auto updatedWithSpeed = speedChanged
		? rebufferOnSpeedChange(setup)
		: std::optional<Mixer::Track::WithSpeed>();
	if (!speedChanged && setup.oldSpeed > 0.) {
		const auto normalPosition = Mixer::Track::SpeedIndependentPosition(
			setup.position,
			setup.oldSpeed);
		l->dropFramesTill(normalPosition);
	}

	const auto started = setup.justStarted;
	auto finished = false;
	auto waiting = false;
	auto errAtStart = started;

	auto accumulated = QByteArray();
	auto accumulatedCount = 0;
	if (l->holdsSavedDecodedSamples()) {
		l->takeSavedDecodedSamples(&accumulated);
		accumulatedCount = accumulated.size() / sampleSize;
	}
	const auto accumulateTill = l->bytesPerBuffer();
	while (accumulated.size() < accumulateTill) {
		using Error = AudioPlayerLoader::ReadError;
		const auto result = l->readMore();
		if (result == Error::Retry) {
			continue;
		}
		const auto sampleBytes = v::is<bytes::const_span>(result)
			? v::get<bytes::const_span>(result)
			: bytes::const_span();
		if (!sampleBytes.empty()) {
			accumulated.append(
				reinterpret_cast<const char*>(sampleBytes.data()),
				sampleBytes.size());
			accumulatedCount += sampleBytes.size() / sampleSize;
		} else if (result == Error::Other) {
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
		} else if (result == Error::EndOfFile) {
			finished = true;
			break;
		} else if (result == Error::Wait) {
			waiting = (accumulated.size() < accumulateTill)
				&& (accumulated.isEmpty() || !l->forceToBuffer());
			if (waiting) {
				l->saveDecodedSamples(&accumulated);
			}
			break;
		} else if (v::is<bytes::const_span>(result)) {
			errAtStart = false;
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

	if (started || !accumulated.isEmpty() || updatedWithSpeed) {
		Audio::AttachToDevice();
	}
	if (started) {
		Assert(!updatedWithSpeed);
		track->started();
		if (!internal::audioCheckError()) {
			setStoppedState(track, State::StoppedAtStart);
			emitError(type);
			return;
		}

		track->format = l->format();
		track->state.frequency = l->samplesFrequency();

		track->state.position = (positionMs * track->state.frequency)
			/ 1000LL;
		track->updateWithSpeedPosition();
		track->withSpeed.bufferedPosition = track->withSpeed.position;
		track->withSpeed.fadeStartPosition = track->withSpeed.position;
	} else if (updatedWithSpeed) {
		auto old = Mixer::Track();
		old.stream = base::take(track->stream);
		old.withSpeed = std::exchange(track->withSpeed, *updatedWithSpeed);
		track->speed = setup.newSpeed;
		track->reattach(type);
		old.detach();
	}
	if (!accumulated.isEmpty()) {
		track->ensureStreamCreated(type);

		auto bufferIndex = track->getNotQueuedBufferIndex();

		if (!internal::audioCheckError()) {
			setStoppedState(track, State::StoppedAtError);
			emitError(type);
			return;
		}

		if (bufferIndex < 0) { // No free buffers, wait.
			track->waitingForBuffer = true;
			l->saveDecodedSamples(&accumulated);
			return;
		} else if (l->forceToBuffer()) {
			l->setForceToBuffer(false);
		}
		track->waitingForBuffer = false;

		track->withSpeed.buffered[bufferIndex] = accumulated;
		track->withSpeed.samples[bufferIndex] = accumulatedCount;
		track->withSpeed.bufferedLength += accumulatedCount;
		alBufferData(
			track->stream.buffers[bufferIndex],
			track->format,
			accumulated.constData(),
			accumulated.size(),
			track->state.frequency);

		alSourceQueueBuffers(
			track->stream.source,
			1,
			track->stream.buffers + bufferIndex);

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
		track->withSpeed.length = track->withSpeed.bufferedPosition
			+ track->withSpeed.bufferedLength;
		track->state.length = Mixer::Track::SpeedIndependentPosition(
			track->withSpeed.length,
			track->speed);
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
		alSourcei(
			track->stream.source,
			AL_SAMPLE_OFFSET,
			qMax(track->withSpeed.position - track->withSpeed.bufferedPosition, 0LL));
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

Loaders::SetupLoaderResult Loaders::setupLoader(
		const AudioMsgId &audio,
		crl::time positionMs) {
	QMutexLocker lock(internal::audioPlayerMutex());
	if (!mixer()) {
		return {};
	}

	auto track = mixer()->trackForType(audio.type());
	if (!track || track->state.id != audio || !track->loading) {
		error(audio);
		LOG(("Audio Error: trying to load part of audio, that is not current at the moment"));
		return {};
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

	auto SpeedDependentPosition = Mixer::Track::SpeedDependentPosition;
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
				return {};
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

		track->speed = track->nextSpeed;
		if (!l->open(positionMs, track->speed)) {
			track->state.state = State::StoppedAtStart;
			return { .errorAtStart = true };
		}
		const auto duration = l->duration();
		if (duration <= 0) {
			track->state.state = State::StoppedAtStart;
			return { .errorAtStart = true };
		}
		track->state.frequency = l->samplesFrequency();
		track->state.length = (duration * track->state.frequency) / 1000;
		track->withSpeed.length = SpeedDependentPosition(
			track->state.length,
			track->speed);
		return { .loader = l, .justStarted = true };
	} else if (!EqualSpeeds(track->nextSpeed, track->speed)) {
		return {
			.loader = l,
			.oldSpeed = track->speed,
			.newSpeed = track->nextSpeed,
			.fadeStartPosition = track->withSpeed.fadeStartPosition,
			.position = track->withSpeed.fineTunedPosition,
			.normalLength = track->state.length,
			.frequency = track->state.frequency,
		};
	} else if (track->loaded) {
		LOG(("Audio Error: trying to load part of audio, that is already loaded to the end"));
		return {};
	}
	return {
		.loader = l,
		.oldSpeed = track->speed,
		.newSpeed = track->nextSpeed,
		.position = track->withSpeed.fineTunedPosition,
		.frequency = track->state.frequency,
	};
}

Mixer::Track::WithSpeed Loaders::rebufferOnSpeedChange(
		const SetupLoaderResult &setup) {
	Expects(setup.oldSpeed > 0. && setup.newSpeed > 0.);
	Expects(setup.loader != nullptr);

	const auto speed = setup.newSpeed;
	const auto change = setup.oldSpeed / speed;
	const auto normalPosition = Mixer::Track::SpeedIndependentPosition(
		setup.position,
		setup.oldSpeed);
	const auto newPosition = int64(base::SafeRound(setup.position * change));
	auto result = Mixer::Track::WithSpeed{
		.fineTunedPosition = newPosition,
		.position = newPosition,
		.length = Mixer::Track::SpeedDependentPosition(
			setup.normalLength,
			speed),
		.fadeStartPosition = int64(
			base::SafeRound(setup.fadeStartPosition * change)),
	};
	const auto l = setup.loader;
	l->dropFramesTill(normalPosition);
	const auto normalFrom = l->startReadingQueuedFrames(speed);
	if (normalFrom < 0) {
		result.bufferedPosition = newPosition;
		return result;
	}

	result.bufferedPosition = Mixer::Track::SpeedDependentPosition(
		normalFrom,
		speed);
	for (auto i = 0; i != Mixer::Track::kBuffersCount; ++i) {
		auto finished = false;
		auto accumulated = QByteArray();
		auto accumulatedCount = int64();
		const auto sampleSize = l->sampleSize();
		const auto accumulateTill = l->bytesPerBuffer();
		while (accumulated.size() < accumulateTill) {
			const auto result = l->readMore();
			const auto sampleBytes = v::is<bytes::const_span>(result)
				? v::get<bytes::const_span>(result)
				: bytes::const_span();
			if (!sampleBytes.empty()) {
				accumulated.append(
					reinterpret_cast<const char*>(sampleBytes.data()),
					sampleBytes.size());
				accumulatedCount += sampleBytes.size() / sampleSize;
				continue;
			} else if (result == AudioPlayerLoader::ReadError::Retry) {
				continue;
			}
			Assert(result == AudioPlayerLoader::ReadError::RetryNotQueued
				|| result == AudioPlayerLoader::ReadError::EndOfFile);
			finished = true;
			break;
		}
		if (!accumulated.isEmpty()) {
			result.samples[i] = accumulatedCount;
			result.bufferedLength += accumulatedCount;
			result.buffered[i] = accumulated;
		}
		if (finished) {
			break;
		}
	}

	const auto limit = result.bufferedPosition + result.bufferedLength;
	if (newPosition > limit) {
		result.fineTunedPosition = limit;
		result.position = limit;
	}
	if (limit > result.length) {
		result.length = limit;
	}

	return result;
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
