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
#include "media/media_audio_track.h"

#include "media/media_audio_ffmpeg_loader.h"
#include "media/media_audio.h"
#include "messenger.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

namespace Media {
namespace Audio {
namespace {

constexpr auto kMaxFileSize = 10 * 1024 * 1024;
constexpr auto kDetachDeviceTimeout = TimeMs(500); // destroy the audio device after 500ms of silence
constexpr auto kTrackUpdateTimeout = TimeMs(100);

ALuint CreateSource() {
	auto source = ALuint(0);
	alGenSources(1, &source);
	alSourcef(source, AL_PITCH, 1.f);
	alSourcef(source, AL_GAIN, 1.f);
	alSource3f(source, AL_POSITION, 0, 0, 0);
	alSource3f(source, AL_VELOCITY, 0, 0, 0);
	return source;
}

ALuint CreateBuffer() {
	auto buffer = ALuint(0);
	alGenBuffers(1, &buffer);
	return buffer;
}

} // namespace

Track::Track(gsl::not_null<Instance*> instance) : _instance(instance) {
	_instance->registerTrack(this);
}

void Track::fillFromData(base::byte_vector &&data) {
	FFMpegLoader loader(FileLocation(), QByteArray(), std::move(data));

	auto position = qint64(0);
	if (!loader.open(position)) {
		_failed = true;
		return;
	}

	do {
		auto buffer = QByteArray();
		int64 samplesAdded = 0;
		auto result = loader.readMore(buffer, samplesAdded);
		if (samplesAdded > 0) {
			auto bufferBytes = reinterpret_cast<const gsl::byte*>(buffer.constData());
			_samplesCount += samplesAdded;
			_samples.insert(_samples.end(), bufferBytes, bufferBytes + buffer.size());
		}

		using Result = AudioPlayerLoader::ReadResult;
		switch (result) {
		case Result::Error:
		case Result::NotYet:
		case Result::Wait: {
			_failed = true;
		} break;
		}
		if (result != Result::Ok) {
			break;
		}
	} while (true);

	_alFormat = loader.format();
	_sampleRate = loader.samplesFrequency();
	_lengthMs = (loader.samplesCount() * TimeMs(1000)) / _sampleRate;
}

void Track::fillFromFile(const FileLocation &location) {
	if (location.accessEnable()) {
		fillFromFile(location.name());
		location.accessDisable();
	} else {
		LOG(("Track Error: Could not enable access to file '%1'.").arg(location.name()));
		_failed = true;
	}
}

void Track::fillFromFile(const QString &filePath) {
	QFile f(filePath);
	if (f.open(QIODevice::ReadOnly)) {
		auto size = f.size();
		if (size > 0 && size <= kMaxFileSize) {
			auto bytes = base::byte_vector(size);
			if (f.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) == bytes.size()) {
				fillFromData(std::move(bytes));
			} else {
				LOG(("Track Error: Could not read %1 bytes from file '%2'.").arg(bytes.size()).arg(filePath));
				_failed = true;
			}
		} else {
			LOG(("Track Error: Bad file '%1' size: %2.").arg(filePath).arg(size));
			_failed = true;
		}
	} else {
		LOG(("Track Error: Could not open file '%1'.").arg(filePath));
		_failed = true;
	}
}

void Track::playWithLooping(bool looping) {
	_active = true;
	if (failed() || _samples.empty()) {
		finish();
		return;
	}
	ensureSourceCreated();
	alSourceStop(_alSource);
	_looping = looping;
	alSourcei(_alSource, AL_LOOPING, _looping ? 1 : 0);
	alSourcePlay(_alSource);
	_instance->trackStarted(this);
}

void Track::finish() {
	if (_active) {
		_active = false;
		_instance->trackFinished(this);
	}
	_alPosition = 0;
}

void Track::ensureSourceCreated() {
	if (alIsSource(_alSource)) {
		return;
	}

	{
		QMutexLocker lock(Player::internal::audioPlayerMutex());
		if (!AttachToDevice()) {
			_failed = true;
			return;
		}
	}

	_alSource = CreateSource();
	_alBuffer = CreateBuffer();

	alBufferData(_alBuffer, _alFormat, _samples.data(), _samples.size(), _sampleRate);
	alSourcei(_alSource, AL_BUFFER, _alBuffer);
}

void Track::updateState() {
	if (!isActive() || !alIsSource(_alSource)) {
		return;
	}

	auto state = ALint(0);
	alGetSourcei(_alSource, AL_SOURCE_STATE, &state);
	if (state != AL_PLAYING) {
		finish();
	} else {
		auto currentPosition = ALint(0);
		alGetSourcei(_alSource, AL_SAMPLE_OFFSET, &currentPosition);
		_alPosition = currentPosition;
	}
}

void Track::detachFromDevice() {
	if (alIsSource(_alSource)) {
		updateState();
		alSourceStop(_alSource);
		alSourcei(_alSource, AL_BUFFER, AL_NONE);
		alDeleteBuffers(1, &_alBuffer);
		alDeleteSources(1, &_alSource);
	}
	_alBuffer = 0;
	_alSource = 0;
}

void Track::reattachToDevice() {
	if (!isActive() || alIsSource(_alSource)) {
		return;
	}
	ensureSourceCreated();

	alSourcei(_alSource, AL_LOOPING, _looping ? 1 : 0);
	alSourcei(_alSource, AL_SAMPLE_OFFSET, static_cast<ALint>(_alPosition));
	alSourcePlay(_alSource);
}

Track::~Track() {
	detachFromDevice();
	_instance->unregisterTrack(this);
}

Instance::Instance() {
	_updateTimer.setCallback([this] {
		auto hasActive = false;
		for (auto track : _tracks) {
			track->updateState();
			if (track->isActive()) {
				hasActive = true;
			}
		}
		if (hasActive) {
			Audio::StopDetachIfNotUsedSafe();
		}
	});

	_detachFromDeviceTimer.setCallback([this] {
		_detachFromDeviceForce = false;
		Player::internal::DetachFromDevice();
	});
}

std::unique_ptr<Track> Instance::createTrack() {
	return std::make_unique<Track>(this);
}

Instance::~Instance() {
	Expects(_tracks.empty());
}

void Instance::registerTrack(Track *track) {
	_tracks.insert(track);
}

void Instance::unregisterTrack(Track *track) {
	_tracks.erase(track);
}

void Instance::trackStarted(Track *track) {
	stopDetachIfNotUsed();
	if (!_updateTimer.isActive()) {
		_updateTimer.callEach(kTrackUpdateTimeout);
	}
}

void Instance::trackFinished(Track *track) {
	if (!hasActiveTracks()) {
		_updateTimer.cancel();
		scheduleDetachIfNotUsed();
	}
	if (track->isLooping()) {
		trackFinished().notify(track, true);
	}
}

void Instance::detachTracks() {
	for (auto track : _tracks) {
		track->detachFromDevice();
	}
}

void Instance::reattachTracks() {
	if (!IsAttachedToDevice()) {
		return;
	}
	for (auto track : _tracks) {
		track->reattachToDevice();
	}
}

bool Instance::hasActiveTracks() const {
	for (auto track : _tracks) {
		if (track->isActive()) {
			return true;
		}
	}
	return false;
}

void Instance::scheduleDetachFromDevice() {
	_detachFromDeviceForce = true;
	scheduleDetachIfNotUsed();
}

void Instance::scheduleDetachIfNotUsed() {
	if (!_detachFromDeviceTimer.isActive()) {
		_detachFromDeviceTimer.callOnce(kDetachDeviceTimeout);
	}
}

void Instance::stopDetachIfNotUsed() {
	if (!_detachFromDeviceForce) {
		_detachFromDeviceTimer.cancel();
	}
}

Instance &Current() {
	return Messenger::Instance().audio();
}

} // namespace Audio
} // namespace Media
