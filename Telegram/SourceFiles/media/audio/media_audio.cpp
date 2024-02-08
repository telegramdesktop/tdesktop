/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio.h"

#include "media/audio/media_audio_ffmpeg_loader.h"
#include "media/audio/media_child_ffmpeg_loader.h"
#include "media/audio/media_audio_loaders.h"
#include "media/audio/media_audio_track.h"
#include "media/media_common.h"
#include "media/streaming/media_streaming_utility.h"
#include "webrtc/webrtc_environment.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "ui/chat/attach/attach_prepare.h"

#include <al.h>
#include <alc.h>

#include <numeric>

Q_DECLARE_METATYPE(AudioMsgId);
Q_DECLARE_METATYPE(VoiceWaveform);

namespace {

constexpr auto kSuppressRatioAll = 0.2;
constexpr auto kSuppressRatioSong = 0.05;
constexpr auto kWaveformCounterBufferSize = 256 * 1024;

QMutex AudioMutex;
ALCdevice *AudioDevice = nullptr;
ALCcontext *AudioContext = nullptr;
Webrtc::DeviceResolvedId AudioDeviceLastUsedId{
	.type = Webrtc::DeviceType::Playback
};

auto VolumeMultiplierAll = 1.;
auto VolumeMultiplierSong = 1.;

} // namespace

namespace Media {
namespace Audio {
namespace {

Player::Mixer *MixerInstance = nullptr;

// Thread: Any.
bool ContextErrorHappened() {
	ALenum errCode;
	if ((errCode = alcGetError(AudioDevice)) != ALC_NO_ERROR) {
		LOG(("Audio Context Error: %1, %2").arg(errCode).arg((const char *)alcGetString(AudioDevice, errCode)));
		return true;
	}
	return false;
}

// Thread: Any.
bool PlaybackErrorHappened() {
	ALenum errCode;
	if ((errCode = alGetError()) != AL_NO_ERROR) {
		LOG(("Audio Playback Error: %1, %2").arg(errCode).arg((const char *)alGetString(errCode)));
		return true;
	}
	return false;
}

// Thread: Any. Must be locked: AudioMutex.
void DestroyPlaybackDevice() {
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

// Thread: Any. Must be locked: AudioMutex.
bool CreatePlaybackDevice() {
	if (AudioDevice) return true;

	AudioDeviceLastUsedId = Current().playbackDeviceId();

	const auto id = AudioDeviceLastUsedId.isDefault()
		? std::string()
		: AudioDeviceLastUsedId.value.toStdString();
	AudioDevice = alcOpenDevice(id.empty() ? nullptr : id.c_str());
	if (!AudioDevice) {
		LOG(("Audio Error: Could not create default playback device, refreshing.."));
		crl::on_main([] {
			const auto type = Webrtc::DeviceType::Playback;
			Core::App().mediaDevices().forceRefresh(type);
		});
		return false;
	}

	AudioContext = alcCreateContext(AudioDevice, nullptr);
	alcMakeContextCurrent(AudioContext);
	if (ContextErrorHappened()) {
		DestroyPlaybackDevice();
		return false;
	}

	ALfloat v[] = { 0.f, 0.f, -1.f, 0.f, 1.f, 0.f };
	alListener3f(AL_POSITION, 0.f, 0.f, 0.f);
	alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f);
	alListenerfv(AL_ORIENTATION, v);

	alDistanceModel(AL_NONE);

	return true;
}

// Thread: Main. Must be locked: AudioMutex.
void ClosePlaybackDevice(not_null<Instance*> instance) {
	if (!AudioDevice) return;

	LOG(("Audio Info: Closing audio playback device."));

	if (Player::mixer()) {
		Player::mixer()->prepareToCloseDevice();
	}
	instance->detachTracks();

	DestroyPlaybackDevice();
}

} // namespace

// Thread: Main.
void Start(not_null<Instance*> instance) {
	Assert(AudioDevice == nullptr);

	qRegisterMetaType<AudioMsgId>();
	qRegisterMetaType<VoiceWaveform>();

	const auto loglevel = getenv("ALSOFT_LOGLEVEL");
	LOG(("OpenAL Logging Level: %1").arg(loglevel ? loglevel : "(not set)"));

	MixerInstance = new Player::Mixer(instance);
}

// Thread: Main.
void Finish(not_null<Instance*> instance) {
	// MixerInstance variable should be modified under AudioMutex protection.
	// So it is modified in the ~Mixer() destructor after all tracks are cleared.
	delete MixerInstance;

	// No sync required already.
	ClosePlaybackDevice(instance);
}

// Thread: Main. Locks: AudioMutex.
bool IsAttachedToDevice() {
	QMutexLocker lock(&AudioMutex);
	return (AudioDevice != nullptr);
}

// Thread: Any. Must be locked: AudioMutex.
bool AttachToDevice() {
	if (AudioDevice) {
		return true;
	}
	LOG(("Audio Info: recreating audio device and reattaching the tracks"));

	CreatePlaybackDevice();
	if (!AudioDevice) {
		return false;
	}

	if (const auto m = Player::mixer()) {
		m->reattachTracks();
		m->scheduleFaderCallback();
	}

	crl::on_main([] {
		if (!Core::Quitting()) {
			Current().reattachTracks();
		}
	});
	return true;
}

void ScheduleDetachFromDeviceSafe() {
	crl::on_main([] {
		if (!Core::Quitting()) {
			Current().scheduleDetachFromDevice();
		}
	});
}

void ScheduleDetachIfNotUsedSafe() {
	crl::on_main([] {
		if (!Core::Quitting()) {
			Current().scheduleDetachIfNotUsed();
		}
	});
}

void StopDetachIfNotUsedSafe() {
	crl::on_main([] {
		if (!Core::Quitting()) {
			Current().stopDetachIfNotUsed();
		}
	});
}

bool SupportsSpeedControl() {
	static const auto result = [] {
		return avfilter_get_by_name("abuffer")
			&& avfilter_get_by_name("abuffersink")
			&& avfilter_get_by_name("atempo");
	}();
	return result;
}

} // namespace Audio

namespace Player {
namespace {

constexpr auto kVolumeRound = 10000;
constexpr auto kPreloadSeconds = 2LL; // preload next part if less than 2 seconds remains
constexpr auto kFadeDuration = crl::time(500);
constexpr auto kCheckPlaybackPositionTimeout = crl::time(100); // 100ms per check audio position
constexpr auto kCheckPlaybackPositionDelta = 2400LL; // update position called each 2400 samples
constexpr auto kCheckFadingTimeout = crl::time(7); // 7ms

rpl::event_stream<AudioMsgId> UpdatedStream;

} // namespace

rpl::producer<AudioMsgId> Updated() {
	return UpdatedStream.events();
}

// Thread: Any. Must be locked: AudioMutex.
float64 ComputeVolume(AudioMsgId::Type type) {
	const auto gain = [&] {
		switch (type) {
		case AudioMsgId::Type::Voice:
			return VolumeMultiplierAll * mixer()->getSongVolume();
		case AudioMsgId::Type::Song:
			return VolumeMultiplierSong * mixer()->getSongVolume();
		case AudioMsgId::Type::Video: return mixer()->getVideoVolume();
		}
		return 1.;
	}();
	return gain * gain * gain;
}

Mixer *mixer() {
	return Audio::MixerInstance;
}

void Mixer::Track::createStream(AudioMsgId::Type type) {
	alGenSources(1, &stream.source);
	alSourcef(stream.source, AL_PITCH, 1.f);
	alSource3f(stream.source, AL_POSITION, 0, 0, 0);
	alSource3f(stream.source, AL_VELOCITY, 0, 0, 0);
	alSourcei(stream.source, AL_LOOPING, 0);
	alSourcei(stream.source, AL_SOURCE_RELATIVE, 1);
	alSourcei(stream.source, AL_ROLLOFF_FACTOR, 0);
	if (alIsExtensionPresent("AL_SOFT_direct_channels_remix")) {
		alSourcei(
			stream.source,
			alGetEnumValue("AL_DIRECT_CHANNELS_SOFT"),
			alGetEnumValue("AL_REMIX_UNMATCHED_SOFT"));
	}
	alGenBuffers(3, stream.buffers);
}

void Mixer::Track::destroyStream() {
	if (isStreamCreated()) {
		alDeleteBuffers(3, stream.buffers);
		alDeleteSources(1, &stream.source);
	}
	stream.source = 0;
	for (auto i = 0; i != 3; ++i) {
		stream.buffers[i] = 0;
	}
}

void Mixer::Track::reattach(AudioMsgId::Type type) {
	if (isStreamCreated()
		|| (!withSpeed.samples[0] && !state.id.externalPlayId())) {
		return;
	}

	createStream(type);
	for (auto i = 0; i != kBuffersCount; ++i) {
		if (!withSpeed.samples[i]) {
			break;
		}
		alBufferData(
			stream.buffers[i],
			format,
			withSpeed.buffered[i].constData(),
			withSpeed.buffered[i].size(),
			state.frequency);
		alSourceQueueBuffers(stream.source, 1, stream.buffers + i);
	}

	alSourcei(
		stream.source,
		AL_SAMPLE_OFFSET,
		qMax(withSpeed.position - withSpeed.bufferedPosition, 0LL));
	if (!IsStopped(state.state)
		&& (state.state != State::PausedAtEnd)
		&& !state.waitingForData) {
		alSourcef(stream.source, AL_GAIN, ComputeVolume(type));
		alSourcePlay(stream.source);
		if (IsPaused(state.state)) {
			// We must always start the source if we want the AL_SAMPLE_OFFSET to be applied.
			// Otherwise it won't be read by alGetSource and we'll get a corrupt position.
			// So in case of a paused source we start it and then immediately pause it.
			alSourcePause(stream.source);
		}
	}
}

void Mixer::Track::detach() {
	getNotQueuedBufferIndex();
	resetStream();
	destroyStream();
}

void Mixer::Track::clear() {
	detach();

	state = TrackState();
	file = Core::FileLocation();
	data = QByteArray();
	format = 0;
	loading = false;
	loaded = false;
	waitingForBuffer = false;
	withSpeed = WithSpeed();
	speed = 1.;

	setExternalData(nullptr);
	lastUpdateWhen = 0;
	lastUpdatePosition = 0;
}

void Mixer::Track::started() {
	resetStream();

	format = 0;
	loaded = false;
	withSpeed = WithSpeed();
}

bool Mixer::Track::isStreamCreated() const {
	return alIsSource(stream.source);
}

void Mixer::Track::ensureStreamCreated(AudioMsgId::Type type) {
	if (!isStreamCreated()) {
		createStream(type);
	}
}

int Mixer::Track::getNotQueuedBufferIndex() {
	// See if there are no free buffers right now.
	while (withSpeed.samples[kBuffersCount - 1] != 0) {
		// Try to unqueue some buffer.
		ALint processed = 0;
		alGetSourcei(stream.source, AL_BUFFERS_PROCESSED, &processed);
		if (processed < 1) { // No processed buffers, wait.
			return -1;
		}

		// Unqueue some processed buffer.
		ALuint buffer = 0;
		alSourceUnqueueBuffers(stream.source, 1, &buffer);

		// Find it in the list and clear it.
		bool found = false;
		for (auto i = 0; i != kBuffersCount; ++i) {
			if (stream.buffers[i] == buffer) {
				const auto samplesInBuffer = withSpeed.samples[i];
				withSpeed.bufferedPosition += samplesInBuffer;
				withSpeed.bufferedLength -= samplesInBuffer;
				for (auto j = i + 1; j != kBuffersCount; ++j) {
					withSpeed.samples[j - 1] = withSpeed.samples[j];
					stream.buffers[j - 1] = stream.buffers[j];
					withSpeed.buffered[j - 1] = withSpeed.buffered[j];
				}
				withSpeed.samples[kBuffersCount - 1] = 0;
				stream.buffers[kBuffersCount - 1] = buffer;
				withSpeed.buffered[kBuffersCount - 1] = QByteArray();
				found = true;
				break;
			}
		}
		if (!found) {
			LOG(("Audio Error: Could not find the unqueued buffer! Buffer %1 in source %2 with processed count %3").arg(buffer).arg(stream.source).arg(processed));
			return -1;
		}
	}

	for (auto i = 0; i != kBuffersCount; ++i) {
		if (!withSpeed.samples[i]) {
			return i;
		}
	}
	return -1;
}

void Mixer::Track::setExternalData(
		std::unique_ptr<ExternalSoundData> data) {
	nextSpeed = speed = data ? data->speed : 1.;
	externalData = std::move(data);
}

void Mixer::Track::updateStatePosition() {
	state.position = SpeedIndependentPosition(withSpeed.position, speed);
}

void Mixer::Track::updateWithSpeedPosition() {
	withSpeed.position = SpeedDependentPosition(state.position, speed);
	withSpeed.fineTunedPosition = withSpeed.position;
}

int64 Mixer::Track::SpeedIndependentPosition(
		int64 position,
		float64 speed) {
	Expects(speed <= kSpeedMax);

	return int64(base::SafeRound(position * speed));
}

int64 Mixer::Track::SpeedDependentPosition(
		int64 position,
		float64 speed) {
	Expects(speed >= kSpeedMin);

	return int64(base::SafeRound(position / speed));
}

void Mixer::Track::resetStream() {
	if (isStreamCreated()) {
		alSourceStop(stream.source);
		alSourcei(stream.source, AL_BUFFER, AL_NONE);
	}
}

Mixer::Track::~Track() = default;

Mixer::Mixer(not_null<Audio::Instance*> instance)
: _instance(instance)
, _volumeVideo(kVolumeRound)
, _volumeSong(kVolumeRound)
, _fader(new Fader(&_faderThread))
, _loader(new Loaders(&_loaderThread)) {
	connect(this, SIGNAL(suppressSong()), _fader, SLOT(onSuppressSong()));
	connect(this, SIGNAL(unsuppressSong()), _fader, SLOT(onUnsuppressSong()));
	connect(this, SIGNAL(suppressAll(qint64)), _fader, SLOT(onSuppressAll(qint64)));

	Core::App().settings().songVolumeChanges(
	) | rpl::start_with_next([=] {
		InvokeQueued(_fader, [fader = _fader] {
			fader->songVolumeChanged();
		});
	}, _lifetime);

	Core::App().settings().videoVolumeChanges(
	) | rpl::start_with_next([=] {
		InvokeQueued(_fader, [fader = _fader] {
			fader->videoVolumeChanged();
		});
	}, _lifetime);

	connect(this, SIGNAL(loaderOnStart(AudioMsgId,qint64)), _loader, SLOT(onStart(AudioMsgId,qint64)));
	connect(this, SIGNAL(loaderOnCancel(AudioMsgId)), _loader, SLOT(onCancel(AudioMsgId)), Qt::QueuedConnection);
	connect(_loader, SIGNAL(needToCheck()), _fader, SLOT(onTimer()));
	connect(_loader, SIGNAL(error(AudioMsgId)), this, SLOT(onError(AudioMsgId)));
	connect(_fader, SIGNAL(needToPreload(AudioMsgId)), _loader, SLOT(onLoad(AudioMsgId)));
	connect(_fader, SIGNAL(playPositionUpdated(AudioMsgId)), this, SIGNAL(updated(AudioMsgId)));
	connect(_fader, SIGNAL(audioStopped(AudioMsgId)), this, SLOT(onStopped(AudioMsgId)));
	connect(_fader, SIGNAL(error(AudioMsgId)), this, SLOT(onError(AudioMsgId)));
	connect(this, SIGNAL(stoppedOnError(AudioMsgId)), this, SIGNAL(updated(AudioMsgId)), Qt::QueuedConnection);
	connect(this, SIGNAL(updated(AudioMsgId)), this, SLOT(onUpdated(AudioMsgId)));

	_loaderThread.start();
	_faderThread.start();
}

// Thread: Main. Locks: AudioMutex.
Mixer::~Mixer() {
	{
		QMutexLocker lock(&AudioMutex);

		for (auto i = 0; i != kTogetherLimit; ++i) {
			trackForType(AudioMsgId::Type::Voice, i)->clear();
			trackForType(AudioMsgId::Type::Song, i)->clear();
		}
		_videoTrack.clear();

		Audio::ClosePlaybackDevice(_instance);
		Audio::MixerInstance = nullptr;
	}

	_faderThread.quit();
	_loaderThread.quit();
	_faderThread.wait();
	_loaderThread.wait();
}

void Mixer::scheduleFaderCallback() {
	InvokeQueued(_fader, [fader = _fader] {
		fader->onTimer();
	});
}

void Mixer::onUpdated(const AudioMsgId &audio) {
	if (audio.externalPlayId()) {
		externalSoundProgress(audio);
	}
	crl::on_main([=] {
		// We've replaced base::Observable with on_main, because
		// base::Observable::notify is not syncronous by default.
		UpdatedStream.fire_copy(audio);
	});
}

void Mixer::onError(const AudioMsgId &audio) {
	stoppedOnError(audio);

	QMutexLocker lock(&AudioMutex);
	auto type = audio.type();
	if (type == AudioMsgId::Type::Voice) {
		if (auto current = trackForType(type)) {
			if (current->state.id == audio) {
				unsuppressSong();
			}
		}
	}
}

void Mixer::onStopped(const AudioMsgId &audio) {
	updated(audio);

	QMutexLocker lock(&AudioMutex);
	auto type = audio.type();
	if (type == AudioMsgId::Type::Voice) {
		if (auto current = trackForType(type)) {
			if (current->state.id == audio) {
				unsuppressSong();
			}
		}
	}
}

Mixer::Track *Mixer::trackForType(AudioMsgId::Type type, int index) {
	if (index < 0) {
		if (auto indexPtr = currentIndex(type)) {
			index = *indexPtr;
		} else {
			return nullptr;
		}
	}
	switch (type) {
	case AudioMsgId::Type::Voice: return &_audioTracks[index];
	case AudioMsgId::Type::Song: return &_songTracks[index];
	case AudioMsgId::Type::Video: return &_videoTrack;
	}
	return nullptr;
}

const Mixer::Track *Mixer::trackForType(AudioMsgId::Type type, int index) const {
	return const_cast<Mixer*>(this)->trackForType(type, index);
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

void Mixer::resetFadeStartPosition(AudioMsgId::Type type, int positionInBuffered) {
	auto track = trackForType(type);
	if (!track) return;

	if (positionInBuffered < 0) {
		Audio::AttachToDevice();
		if (track->isStreamCreated()) {
			ALint alSampleOffset = 0;
			ALint alState = AL_INITIAL;
			alGetSourcei(track->stream.source, AL_SAMPLE_OFFSET, &alSampleOffset);
			alGetSourcei(track->stream.source, AL_SOURCE_STATE, &alState);
			if (Audio::PlaybackErrorHappened()) {
				setStoppedState(track, State::StoppedAtError);
				onError(track->state.id);
				return;
			} else if ((alState == AL_STOPPED)
				&& (alSampleOffset == 0)
				&& !internal::CheckAudioDeviceConnected()) {
				track->withSpeed.fadeStartPosition = track->withSpeed.position;
				return;
			}

			const auto stoppedAtEnd = track->state.waitingForData
				|| ((alState == AL_STOPPED)
					&& (!IsStopped(track->state.state)
						|| IsStoppedAtEnd(track->state.state)));
			positionInBuffered = stoppedAtEnd
				? track->withSpeed.bufferedLength
				: alSampleOffset;
		} else {
			positionInBuffered = 0;
		}
	}
	const auto withSpeedPosition = track->withSpeed.samples[0]
		? (track->withSpeed.bufferedPosition + positionInBuffered)
		: track->withSpeed.position;
	track->withSpeed.fineTunedPosition = withSpeedPosition;
	track->withSpeed.position = withSpeedPosition;
	track->withSpeed.fadeStartPosition = withSpeedPosition;
	track->updateStatePosition();
}

bool Mixer::fadedStop(AudioMsgId::Type type, bool *fadedStart) {
	auto current = trackForType(type);
	if (!current) return false;

	switch (current->state.state) {
	case State::Starting:
	case State::Resuming:
	case State::Playing: {
		current->state.state = State::Stopping;
		resetFadeStartPosition(type);
		if (fadedStart) *fadedStart = true;
	} break;
	case State::Pausing: {
		current->state.state = State::Stopping;
		if (fadedStart) *fadedStart = true;
	} break;
	case State::Paused:
	case State::PausedAtEnd: {
		setStoppedState(current);
	} return true;
	}
	return false;
}

void Mixer::play(
		const AudioMsgId &audio,
		std::unique_ptr<ExternalSoundData> externalData,
		crl::time positionMs) {
	Expects(externalData != nullptr);
	Expects(audio.externalPlayId() != 0);

	setSongVolume(Core::App().settings().songVolume());
	setVideoVolume(Core::App().settings().videoVolume());

	auto type = audio.type();
	AudioMsgId stopped;
	{
		QMutexLocker lock(&AudioMutex);
		Audio::AttachToDevice();
		if (!AudioDevice) return;

		auto fadedStart = false;
		auto current = trackForType(type);
		if (!current) return;

		if (current->state.id != audio) {
			if (fadedStop(type, &fadedStart)) {
				stopped = current->state.id;
			}
			if (current->state.id) {
				loaderOnCancel(current->state.id);
				scheduleFaderCallback();
			}
			if (type != AudioMsgId::Type::Video) {
				auto foundCurrent = currentIndex(type);
				auto index = 0;
				for (; index != kTogetherLimit; ++index) {
					if (trackForType(type, index)->state.id == audio) {
						*foundCurrent = index;
						break;
					}
				}
				if (index == kTogetherLimit && ++*foundCurrent >= kTogetherLimit) {
					*foundCurrent -= kTogetherLimit;
				}
				current = trackForType(type);
			}
		}

		current->clear(); // Clear all previous state.
		current->state.id = audio;
		current->lastUpdateWhen = 0;
		current->lastUpdatePosition = 0;
		current->setExternalData(std::move(externalData));
		current->state.position = (positionMs * current->state.frequency)
			/ 1000LL;
		current->updateWithSpeedPosition();
		current->state.state = current->externalData
			? State::Paused
			: fadedStart
			? State::Starting
			: State::Playing;
		current->loading = true;
		loaderOnStart(current->state.id, positionMs);
		if (type == AudioMsgId::Type::Voice) {
			suppressSong();
		}
	}
	if (stopped) {
		updated(stopped);
	}
}

void Mixer::feedFromExternal(ExternalSoundPart &&part) {
	_loader->feedFromExternal(std::move(part));
}

void Mixer::forceToBufferExternal(const AudioMsgId &audioId) {
	_loader->forceToBufferExternal(audioId);
}

// Thread: Main. Locks: AudioMutex.
void Mixer::setSpeedFromExternal(const AudioMsgId &audioId, float64 speed) {
	QMutexLocker lock(&AudioMutex);
	const auto track = trackForType(audioId.type());
	if (track->state.id == audioId) {
		track->nextSpeed = speed;
		if (!EqualSpeeds(track->speed, track->nextSpeed)
			&& !IsStoppedOrStopping(track->state.state)) {
			track->loading = true;
			track->loaded = false;
			InvokeQueued(_loader, [loader = _loader, id = audioId] {
				loader->onLoad(id);
			});
		}
	}
}

Streaming::TimePoint Mixer::getExternalSyncTimePoint(
		const AudioMsgId &audio) const {
	Expects(audio.externalPlayId() != 0);

	auto result = Streaming::TimePoint();
	const auto type = audio.type();

	QMutexLocker lock(&AudioMutex);
	const auto track = trackForType(type);
	if (track && track->state.id == audio && track->lastUpdateWhen > 0) {
		result.trackTime = track->lastUpdatePosition;
		result.worldTime = track->lastUpdateWhen;
	}
	return result;
}

crl::time Mixer::getExternalCorrectedTime(const AudioMsgId &audio, crl::time frameMs, crl::time systemMs) {
	auto result = frameMs;
	const auto type = audio.type();

	QMutexLocker lock(&AudioMutex);
	const auto track = trackForType(type);
	if (track && track->state.id == audio && track->lastUpdateWhen > 0) {
		result = static_cast<crl::time>(track->lastUpdatePosition);
		if (systemMs > track->lastUpdateWhen) {
			result += (systemMs - track->lastUpdateWhen);
		}
	}
	return result;
}

void Mixer::externalSoundProgress(const AudioMsgId &audio) {
	const auto type = audio.type();

	QMutexLocker lock(&AudioMutex);
	const auto current = trackForType(type);
	if (current && current->state.length && current->state.frequency) {
		if (current->state.id == audio
			&& current->state.state == State::Playing) {
			current->lastUpdateWhen = crl::now();
			current->lastUpdatePosition = (current->state.position * 1000LL)
				/ current->state.frequency;
		}
	}
}

bool Mixer::checkCurrentALError(AudioMsgId::Type type) {
	if (!Audio::PlaybackErrorHappened()) return true;

	const auto data = trackForType(type);
	if (!data) {
		setStoppedState(data, State::StoppedAtError);
		onError(data->state.id);
	}
	return false;
}

void Mixer::pause(const AudioMsgId &audio, bool fast) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto type = audio.type();
		auto track = trackForType(type);
		if (!track || track->state.id != audio) {
			return;
		}

		current = track->state.id;
		switch (track->state.state) {
		case State::Starting:
		case State::Resuming:
		case State::Playing: {
			track->state.state = fast ? State::Paused : State::Pausing;
			resetFadeStartPosition(type);
			if (type == AudioMsgId::Type::Voice) {
				unsuppressSong();
			}
		} break;

		case State::Pausing:
		case State::Stopping: {
			track->state.state = fast ? State::Paused : State::Pausing;
		} break;
		}

		if (fast && track->isStreamCreated()) {
			ALint state = AL_INITIAL;
			alGetSourcei(track->stream.source, AL_SOURCE_STATE, &state);
			if (!checkCurrentALError(type)) return;

			if (state == AL_PLAYING) {
				alSourcePause(track->stream.source);
				if (!checkCurrentALError(type)) return;
			}
		}

		scheduleFaderCallback();

		track->lastUpdateWhen = 0;
		track->lastUpdatePosition = 0;
	}
	if (current) updated(current);
}

void Mixer::resume(const AudioMsgId &audio, bool fast) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto type = audio.type();
		auto track = trackForType(type);
		if (!track || track->state.id != audio) {
			return;
		}

		current = track->state.id;
		switch (track->state.state) {
		case State::Pausing:
		case State::Paused:
		case State::PausedAtEnd: {
			if (track->state.state == State::Paused) {
				// This calls Audio::AttachToDevice().
				resetFadeStartPosition(type);
			} else {
				Audio::AttachToDevice();
			}
			track->state.state = fast ? State::Playing : State::Resuming;

			if (track->isStreamCreated()) {
				// When starting the video audio is in paused state and
				// gets resumed before the stream is created with any data.
				ALint state = AL_INITIAL;
				alGetSourcei(track->stream.source, AL_SOURCE_STATE, &state);
				if (!checkCurrentALError(type)) return;

				if (state != AL_PLAYING) {
					if (state == AL_STOPPED && !internal::CheckAudioDeviceConnected()) {
						return;
					}

					alSourcef(track->stream.source, AL_GAIN, ComputeVolume(type));
					if (!checkCurrentALError(type)) return;

					if (state == AL_STOPPED) {
						alSourcei(
							track->stream.source,
							AL_SAMPLE_OFFSET,
							qMax(track->withSpeed.position - track->withSpeed.bufferedPosition, 0LL));
						if (!checkCurrentALError(type)) return;
					}
					alSourcePlay(track->stream.source);
					if (!checkCurrentALError(type)) return;
				}
				if (type == AudioMsgId::Type::Voice) {
					suppressSong();
				}
			}
		} break;
		}
		scheduleFaderCallback();
	}
	if (current) updated(current);
}

void Mixer::stop(const AudioMsgId &audio) {
	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto type = audio.type();
		auto track = trackForType(type);
		if (!track || track->state.id != audio) {
			return;
		}

		current = audio;
		fadedStop(type);
		if (type == AudioMsgId::Type::Voice) {
			unsuppressSong();
		} else if (type == AudioMsgId::Type::Video) {
			track->clear();
			loaderOnCancel(audio);
		}
	}
	if (current) updated(current);
}

void Mixer::stop(const AudioMsgId &audio, State state) {
	Expects(IsStopped(state));

	AudioMsgId current;
	{
		QMutexLocker lock(&AudioMutex);
		auto type = audio.type();
		auto track = trackForType(type);
		if (!track
			|| track->state.id != audio
			|| IsStopped(track->state.state)) {
			return;
		}

		current = audio;
		setStoppedState(track, state);
		if (type == AudioMsgId::Type::Voice) {
			unsuppressSong();
		} else if (type == AudioMsgId::Type::Video) {
			track->clear();
		}
	}
	if (current) updated(current);
}

void Mixer::stopAndClear() {
	Track *current_audio = nullptr, *current_song = nullptr;
	{
		QMutexLocker lock(&AudioMutex);
		if ((current_audio = trackForType(AudioMsgId::Type::Voice))) {
			setStoppedState(current_audio);
		}
		if ((current_song = trackForType(AudioMsgId::Type::Song))) {
			setStoppedState(current_song);
		}
	}
	if (current_song) {
		updated(current_song->state.id);
	}
	if (current_audio) {
		updated(current_audio->state.id);
	}
	{
		QMutexLocker lock(&AudioMutex);
		auto clearAndCancel = [this](AudioMsgId::Type type, int index) {
			auto track = trackForType(type, index);
			if (track->state.id) {
				loaderOnCancel(track->state.id);
			}
			track->clear();
		};
		for (auto index = 0; index != kTogetherLimit; ++index) {
			clearAndCancel(AudioMsgId::Type::Voice, index);
			clearAndCancel(AudioMsgId::Type::Song, index);
		}
		_videoTrack.clear();
	}
}

TrackState Mixer::currentState(AudioMsgId::Type type) {
	QMutexLocker lock(&AudioMutex);
	auto current = trackForType(type);
	if (!current) {
		return TrackState();
	}
	return current->state;
}

void Mixer::setStoppedState(Track *current, State state) {
	current->state.state = state;
	current->state.position = 0;
	current->withSpeed.position = 0;
	current->withSpeed.fineTunedPosition = 0;
	if (current->isStreamCreated()) {
		alSourceStop(current->stream.source);
		alSourcef(current->stream.source, AL_GAIN, 1);
	}
	if (current->state.id) {
		loaderOnCancel(current->state.id);
	}
}

// Thread: Main. Must be locked: AudioMutex.
void Mixer::prepareToCloseDevice() {
	for (auto i = 0; i != kTogetherLimit; ++i) {
		trackForType(AudioMsgId::Type::Voice, i)->detach();
		trackForType(AudioMsgId::Type::Song, i)->detach();
	}
	_videoTrack.detach();
}

// Thread: Main. Must be locked: AudioMutex.
void Mixer::reattachIfNeeded() {
	Audio::Current().stopDetachIfNotUsed();

	auto reattachNeeded = [this] {
		auto isPlayingState = [](const Track &track) {
			auto state = track.state.state;
			return (state == State::Playing) || IsFading(state);
		};
		for (auto i = 0; i != kTogetherLimit; ++i) {
			if (isPlayingState(*trackForType(AudioMsgId::Type::Voice, i))
				|| isPlayingState(*trackForType(AudioMsgId::Type::Song, i))) {
				return true;
			}
		}
		return isPlayingState(_videoTrack);
	};

	if (reattachNeeded() || Audio::Current().hasActiveTracks()) {
		Audio::AttachToDevice();
	}
}

// Thread: Any. Must be locked: AudioMutex.
void Mixer::reattachTracks() {
	for (auto i = 0; i != kTogetherLimit; ++i) {
		trackForType(AudioMsgId::Type::Voice, i)->reattach(AudioMsgId::Type::Voice);
		trackForType(AudioMsgId::Type::Song, i)->reattach(AudioMsgId::Type::Song);
	}
	_videoTrack.reattach(AudioMsgId::Type::Video);
}

void Mixer::setSongVolume(float64 volume) {
	_volumeSong.storeRelease(qRound(volume * kVolumeRound));
}

float64 Mixer::getSongVolume() const {
	return float64(_volumeSong.loadAcquire()) / kVolumeRound;
}

void Mixer::setVideoVolume(float64 volume) {
	_volumeVideo.storeRelease(qRound(volume * kVolumeRound));
}

float64 Mixer::getVideoVolume() const {
	return float64(_volumeVideo.loadAcquire()) / kVolumeRound;
}

Fader::Fader(QThread *thread) : QObject()
, _timer(this)
, _suppressVolumeAll(1., 1.)
, _suppressVolumeSong(1., 1.) {
	moveToThread(thread);
	_timer.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(onInit()));
	connect(thread, SIGNAL(finished()), this, SLOT(deleteLater()));

	_timer.setSingleShot(true);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(onTimer()));
}

void Fader::onInit() {
}

void Fader::onTimer() {
	QMutexLocker lock(&AudioMutex);
	if (!mixer()) return;

	constexpr auto kMediaPlayerSuppressDuration = crl::time(150);

	auto volumeChangedAll = false;
	auto volumeChangedSong = false;
	if (_suppressAll || _suppressSongAnim) {
		auto ms = crl::now();
		if (_suppressAll) {
			if (ms >= _suppressAllEnd || ms < _suppressAllStart) {
				_suppressAll = _suppressAllAnim = false;
				_suppressVolumeAll = anim::value(1., 1.);
			} else if (ms > _suppressAllEnd - kFadeDuration) {
				if (_suppressVolumeAll.to() != 1.) _suppressVolumeAll.start(1.);
				_suppressVolumeAll.update(1. - ((_suppressAllEnd - ms) / float64(kFadeDuration)), anim::linear);
			} else if (ms >= _suppressAllStart + kMediaPlayerSuppressDuration) {
				if (_suppressAllAnim) {
					_suppressVolumeAll.finish();
					_suppressAllAnim = false;
				}
			} else if (ms > _suppressAllStart) {
				_suppressVolumeAll.update((ms - _suppressAllStart) / float64(kMediaPlayerSuppressDuration), anim::linear);
			}
			auto wasVolumeMultiplierAll = VolumeMultiplierAll;
			VolumeMultiplierAll = _suppressVolumeAll.current();
			volumeChangedAll = (VolumeMultiplierAll != wasVolumeMultiplierAll);
		}
		if (_suppressSongAnim) {
			if (ms >= _suppressSongStart + kFadeDuration) {
				_suppressVolumeSong.finish();
				_suppressSongAnim = false;
			} else {
				_suppressVolumeSong.update((ms - _suppressSongStart) / float64(kFadeDuration), anim::linear);
			}
		}
		auto wasVolumeMultiplierSong = VolumeMultiplierSong;
		VolumeMultiplierSong = _suppressVolumeSong.current();
		accumulate_min(VolumeMultiplierSong, VolumeMultiplierAll);
		volumeChangedSong = (VolumeMultiplierSong != wasVolumeMultiplierSong);
	}
	auto hasFading = (_suppressAll || _suppressSongAnim);
	auto hasPlaying = false;

	auto updatePlayback = [this, &hasPlaying, &hasFading](AudioMsgId::Type type, int index, float64 volumeMultiplier, bool suppressGainChanged) {
		auto track = mixer()->trackForType(type, index);
		if (IsStopped(track->state.state) || track->state.state == State::Paused || !track->isStreamCreated()) return;

		auto emitSignals = updateOnePlayback(track, hasPlaying, hasFading, volumeMultiplier, suppressGainChanged);
		if (emitSignals & EmitError) error(track->state.id);
		if (emitSignals & EmitStopped) audioStopped(track->state.id);
		if (emitSignals & EmitPositionUpdated) playPositionUpdated(track->state.id);
		if (emitSignals & EmitNeedToPreload) needToPreload(track->state.id);
	};
	auto suppressGainForMusic = ComputeVolume(AudioMsgId::Type::Song);
	auto suppressGainForMusicChanged = volumeChangedSong || _volumeChangedSong;
	auto suppressGainForVoice = ComputeVolume(AudioMsgId::Type::Voice);
	for (auto i = 0; i != kTogetherLimit; ++i) {
		updatePlayback(AudioMsgId::Type::Voice, i, suppressGainForVoice, suppressGainForMusicChanged);
		updatePlayback(AudioMsgId::Type::Song, i, suppressGainForMusic, suppressGainForMusicChanged);
	}
	auto suppressGainForVideo = ComputeVolume(AudioMsgId::Type::Video);
	auto suppressGainForVideoChanged = volumeChangedAll || _volumeChangedVideo;
	updatePlayback(AudioMsgId::Type::Video, 0, suppressGainForVideo, suppressGainForVideoChanged);

	_volumeChangedSong = _volumeChangedVideo = false;

	if (hasFading) {
		_timer.start(kCheckFadingTimeout);
		Audio::StopDetachIfNotUsedSafe();
	} else if (hasPlaying) {
		_timer.start(kCheckPlaybackPositionTimeout);
		Audio::StopDetachIfNotUsedSafe();
	} else {
		Audio::ScheduleDetachIfNotUsedSafe();
	}
}

int32 Fader::updateOnePlayback(Mixer::Track *track, bool &hasPlaying, bool &hasFading, float64 volumeMultiplier, bool volumeChanged) {
	const auto errorHappened = [&] {
		if (Audio::PlaybackErrorHappened()) {
			setStoppedState(track, State::StoppedAtError);
			return true;
		}
		return false;
	};

	ALint alSampleOffset = 0;
	ALint alState = AL_INITIAL;
	alGetSourcei(track->stream.source, AL_SAMPLE_OFFSET, &alSampleOffset);
	alGetSourcei(track->stream.source, AL_SOURCE_STATE, &alState);
	if (errorHappened()) {
		return EmitError;
	} else if ((alState == AL_STOPPED)
		&& (alSampleOffset == 0)
		&& !internal::CheckAudioDeviceConnected()) {
		return 0;
	}

	int32 emitSignals = 0;
	const auto stoppedAtEnd = track->state.waitingForData
		|| ((alState == AL_STOPPED)
			&& (!IsStopped(track->state.state)
				|| IsStoppedAtEnd(track->state.state)));
	const auto positionInBuffered = stoppedAtEnd
		? track->withSpeed.bufferedLength
		: alSampleOffset;
	const auto waitingForDataOld = track->state.waitingForData;
	track->state.waitingForData = stoppedAtEnd
		&& (track->state.state != State::Stopping);
	const auto withSpeedPosition = track->withSpeed.bufferedPosition
		+ positionInBuffered;

	auto playing = (track->state.state == State::Playing);
	auto fading = IsFading(track->state.state);
	if (alState != AL_PLAYING && !track->loading) {
		if (fading || playing) {
			fading = false;
			playing = false;
			if (track->state.state == State::Pausing) {
				setStoppedState(track, State::PausedAtEnd);
			} else if (track->state.state == State::Stopping) {
				setStoppedState(track, State::Stopped);
			} else {
				setStoppedState(track, State::StoppedAtEnd);
			}
			if (errorHappened()) return EmitError;
			emitSignals |= EmitStopped;
		}
	} else if (fading && alState == AL_PLAYING) {
		const auto fadingForSamplesCount = withSpeedPosition
			- track->withSpeed.fadeStartPosition;
		if (crl::time(1000) * fadingForSamplesCount >= kFadeDuration * track->state.frequency) {
			fading = false;
			alSourcef(track->stream.source, AL_GAIN, 1. * volumeMultiplier);
			if (errorHappened()) return EmitError;

			switch (track->state.state) {
			case State::Stopping: {
				setStoppedState(track);
				alState = AL_STOPPED;
			} break;
			case State::Pausing: {
				alSourcePause(track->stream.source);
				if (errorHappened()) return EmitError;

				track->state.state = State::Paused;
			} break;
			case State::Starting:
			case State::Resuming: {
				track->state.state = State::Playing;
				playing = true;
			} break;
			}
		} else {
			auto newGain = crl::time(1000) * fadingForSamplesCount / float64(kFadeDuration * track->state.frequency);
			if (track->state.state == State::Pausing || track->state.state == State::Stopping) {
				newGain = 1. - newGain;
			}
			alSourcef(track->stream.source, AL_GAIN, newGain * volumeMultiplier);
			if (errorHappened()) return EmitError;
		}
	} else if (playing && alState == AL_PLAYING) {
		if (volumeChanged) {
			alSourcef(track->stream.source, AL_GAIN, 1. * volumeMultiplier);
			if (errorHappened()) return EmitError;
		}
	}
	track->withSpeed.fineTunedPosition = withSpeedPosition;
	if (alState == AL_PLAYING && withSpeedPosition >= track->withSpeed.position + kCheckPlaybackPositionDelta) {
		track->withSpeed.position = withSpeedPosition;
		track->updateStatePosition();
		emitSignals |= EmitPositionUpdated;
	} else if (track->state.waitingForData && !waitingForDataOld) {
		if (withSpeedPosition > track->withSpeed.position) {
			track->withSpeed.position = withSpeedPosition;
		}
		// When stopped because of insufficient data while streaming,
		// inform the player about the last position we were at.
		emitSignals |= EmitPositionUpdated;
	}
	if (playing || track->state.state == State::Starting || track->state.state == State::Resuming) {
		if ((!track->loaded && !track->loading) || track->waitingForBuffer) {
			auto needPreload = (track->withSpeed.position + kPreloadSeconds * track->state.frequency > track->withSpeed.bufferedPosition + track->withSpeed.bufferedLength);
			if (needPreload) {
				track->loading = true;
				emitSignals |= EmitNeedToPreload;
			}
		}
	}
	if (playing) hasPlaying = true;
	if (fading) hasFading = true;

	return emitSignals;
}

void Fader::setStoppedState(Mixer::Track *track, State state) {
	mixer()->setStoppedState(track, state);
}

void Fader::onSuppressSong() {
	if (!_suppressSong) {
		_suppressSong = true;
		_suppressSongAnim = true;
		_suppressSongStart = crl::now();
		_suppressVolumeSong.start(kSuppressRatioSong);
		onTimer();
	}
}

void Fader::onUnsuppressSong() {
	if (_suppressSong) {
		_suppressSong = false;
		_suppressSongAnim = true;
		_suppressSongStart = crl::now();
		_suppressVolumeSong.start(1.);
		onTimer();
	}
}

void Fader::onSuppressAll(qint64 duration) {
	_suppressAll = true;
	auto now = crl::now();
	if (_suppressAllEnd < now + kFadeDuration) {
		_suppressAllStart = now;
	}
	_suppressAllEnd = now + duration;
	_suppressVolumeAll.start(kSuppressRatioAll);
	onTimer();
}

void Fader::songVolumeChanged() {
	_volumeChangedSong = true;
	onTimer();
}

void Fader::videoVolumeChanged() {
	_volumeChangedVideo = true;
	onTimer();
}

namespace internal {

// Thread: Any.
QMutex *audioPlayerMutex() {
	return &AudioMutex;
}

// Thread: Any.
bool audioCheckError() {
	return !Audio::PlaybackErrorHappened();
}

// Thread: Any. Must be locked: AudioMutex.
bool audioDeviceIsConnected() {
	if (!AudioDevice) {
		return false;
	}
	// always connected in the basic OpenAL, disconnect status is an extension
	auto isConnected = ALint(1);
	if (alcIsExtensionPresent(nullptr, "ALC_EXT_disconnect")) {
		alcGetIntegerv(AudioDevice, alcGetEnumValue(nullptr, "ALC_CONNECTED"), 1, &isConnected);
	}
	if (Audio::ContextErrorHappened()) {
		return false;
	}
	return (isConnected != 0);
}

// Thread: Any. Must be locked: AudioMutex.
bool CheckAudioDeviceConnected() {
	if (audioDeviceIsConnected()) {
		return true;
	}
	Audio::ScheduleDetachFromDeviceSafe();
	return false;
}

// Thread: Main. Locks: AudioMutex.
void DetachFromDevice(not_null<Audio::Instance*> instance) {
	QMutexLocker lock(&AudioMutex);
	Audio::ClosePlaybackDevice(instance);
	if (mixer()) {
		mixer()->reattachIfNeeded();
	}
}

bool DetachIfDeviceChanged(
		not_null<Audio::Instance*> instance,
		const Webrtc::DeviceResolvedId &nowDeviceId) {
	QMutexLocker lock(&AudioMutex);
	if (AudioDeviceLastUsedId == nowDeviceId) {
		return false;
	}
	Audio::ClosePlaybackDevice(instance);
	if (mixer()) {
		mixer()->reattachIfNeeded();
	}
	return true;
}

} // namespace internal

} // namespace Player

class FFMpegAttributesReader : public AbstractFFMpegLoader {
public:
	FFMpegAttributesReader(const Core::FileLocation &file, const QByteArray &data)
	: AbstractFFMpegLoader(file, data, bytes::vector()) {
	}

	bool open(crl::time positionMs, float64 speed = 1.) override {
		if (!AbstractFFMpegLoader::open(positionMs, speed)) {
			return false;
		}

		for (int32 i = 0, l = fmtContext->nb_streams; i < l; ++i) {
			const auto stream = fmtContext->streams[i];
			if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
				if (!_cover.isNull()) {
					continue;
				}
				const auto &packet = stream->attached_pic;
				if (packet.size) {
					const auto coverBytes = QByteArray(
						(const char*)packet.data,
						packet.size);
					auto read = Images::Read({
						.content = coverBytes,
						.forceOpaque = true,
					});
					if (!read.image.isNull()) {
						_cover = std::move(read.image);
						_coverBytes = coverBytes;
						_coverFormat = read.format;
					}
				}
			} else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
				DEBUG_LOG(("Audio Read Error: Found video stream in file '%1', data size '%2', stream %3.")
					.arg(_file.name())
					.arg(_data.size())
					.arg(i));
				return false;
			}
		}

		extractMetaData(fmtContext->streams[streamId]->metadata);
		extractMetaData(fmtContext->metadata);

		return true;
	}

	void trySet(QString &to, AVDictionary *dict, const char *key) {
		if (!to.isEmpty()) return;
		if (AVDictionaryEntry* tag = av_dict_get(dict, key, nullptr, 0)) {
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

	int sampleSize() override {
		Unexpected("We shouldn't try to read sample size here.");
	}

	int format() override {
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

	ReadResult readMore() override {
		DEBUG_LOG(("Audio Read Error: should not call this"));
		return ReadError::Other;
	}

	~FFMpegAttributesReader() {
	}

private:

	QString _title, _performer;
	QImage _cover;
	QByteArray _coverBytes, _coverFormat;

};

namespace Player {

Ui::PreparedFileInformation PrepareForSending(
		const QString &fname,
		const QByteArray &data) {
	auto result = Ui::PreparedFileInformation::Song();
	FFMpegAttributesReader reader(Core::FileLocation(fname), data);
	const auto positionMs = crl::time(0);
	if (reader.open(positionMs) && reader.duration() > 0) {
		result.duration = reader.duration();
		result.title = reader.title();
		result.performer = reader.performer();
		result.cover = reader.cover();
	}
	return { .media = result };
}

} // namespace Player

class FFMpegWaveformCounter : public FFMpegLoader {
public:
	FFMpegWaveformCounter(const Core::FileLocation &file, const QByteArray &data) : FFMpegLoader(file, data, bytes::vector()) {
	}

	bool open(crl::time positionMs, float64 speed = 1.) override {
		if (!FFMpegLoader::open(positionMs, speed)) {
			return false;
		}

		QByteArray buffer;
		buffer.reserve(kWaveformCounterBufferSize);
		const auto samplesCount = samplesFrequency() * duration() / 1000;
		int64 countbytes = sampleSize() * samplesCount;
		int64 processed = 0;
		int64 sumbytes = 0;
		if (samplesCount < Media::Player::kWaveformSamplesCount) {
			return false;
		}

		QVector<uint16> peaks;
		peaks.reserve(Media::Player::kWaveformSamplesCount);

		auto fmt = format();
		auto peak = uint16(0);
		auto callback = [&](uint16 sample) {
			accumulate_max(peak, sample);
			sumbytes += Media::Player::kWaveformSamplesCount;
			if (sumbytes >= countbytes) {
				sumbytes -= countbytes;
				peaks.push_back(peak);
				peak = 0;
			}
		};
		while (processed < countbytes) {
			const auto result = readMore();
			Assert(result != ReadError::Wait); // Not a child loader.
			if (result == ReadError::Retry) {
				continue;
			} else if (result == ReadError::Other
				|| result == ReadError::EndOfFile) {
				break;
			}
			Assert(v::is<bytes::const_span>(result));
			const auto sampleBytes = v::get<bytes::const_span>(result);
			Assert(!sampleBytes.empty());
			if (fmt == AL_FORMAT_MONO8 || fmt == AL_FORMAT_STEREO8) {
				Media::Audio::IterateSamples<uchar>(sampleBytes, callback);
			} else if (fmt == AL_FORMAT_MONO16 || fmt == AL_FORMAT_STEREO16) {
				Media::Audio::IterateSamples<int16>(sampleBytes, callback);
			}
			processed += sampleBytes.size();
		}
		if (sumbytes > 0 && peaks.size() < Media::Player::kWaveformSamplesCount) {
			peaks.push_back(peak);
		}

		if (peaks.isEmpty()) {
			return false;
		}

		auto sum = std::accumulate(peaks.cbegin(), peaks.cend(), 0LL);
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

} // namespace Media

VoiceWaveform audioCountWaveform(
		const Core::FileLocation &file,
		const QByteArray &data) {
	Media::FFMpegWaveformCounter counter(file, data);
	const auto positionMs = crl::time(0);
	if (counter.open(positionMs)) {
		return counter.waveform();
	}
	return VoiceWaveform();
}
