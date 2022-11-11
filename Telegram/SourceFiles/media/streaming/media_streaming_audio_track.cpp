/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_audio_track.h"

#include "media/streaming/media_streaming_utility.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_child_ffmpeg_loader.h"
#include "media/player/media_player_instance.h"

namespace Media {
namespace Streaming {

AudioTrack::AudioTrack(
	const PlaybackOptions &options,
	Stream &&stream,
	AudioMsgId audioId,
	FnMut<void(const Information &)> ready,
	Fn<void(Error)> error)
: _options(options)
, _stream(std::move(stream))
, _audioId(audioId)
, _ready(std::move(ready))
, _error(std::move(error))
, _playPosition(options.position) {
	Expects(_stream.duration > 1);
	Expects(_stream.duration != kDurationUnavailable); // Not supported.
	Expects(_ready != nullptr);
	Expects(_error != nullptr);
	Expects(_audioId.externalPlayId() != 0);
}

int AudioTrack::streamIndex() const {
	// Thread-safe, because _stream.index is immutable.
	return _stream.index;
}

AVRational AudioTrack::streamTimeBase() const {
	return _stream.timeBase;
}

crl::time AudioTrack::streamDuration() const {
	return _stream.duration;
}

void AudioTrack::process(std::vector<FFmpeg::Packet> &&packets) {
	if (packets.empty()) {
		return;
	} else if (packets.front().empty()) {
		Assert(packets.size() == 1);
		_readTillEnd = true;
	}
	for (auto i = begin(packets), e = end(packets); i != e; ++i) {
		if (initialized()) {
			mixerEnqueue(gsl::make_span(&*i, (e - i)));
			break;
		} else if (!tryReadFirstFrame(std::move(*i))) {
			_error(Error::InvalidData);
			break;
		}
	}
}

void AudioTrack::waitForData() {
	if (initialized()) {
		mixerForceToBuffer();
	}
}

bool AudioTrack::initialized() const {
	return !_ready;
}

bool AudioTrack::tryReadFirstFrame(FFmpeg::Packet &&packet) {
	if (ProcessPacket(_stream, std::move(packet)).failed()) {
		return false;
	}
	while (true) {
		if (const auto error = ReadNextFrame(_stream)) {
			if (error.code() == AVERROR_EOF) {
				if (!_initialSkippingFrame) {
					return false;
				}
				// Return the last valid frame if we seek too far.
				_stream.decodedFrame = std::move(_initialSkippingFrame);
				return processFirstFrame();
			} else if (error.code() != AVERROR(EAGAIN) || _readTillEnd) {
				return false;
			} else {
				// Waiting for more packets.
				return true;
			}
		} else if (!fillStateFromFrame()) {
			return false;
		} else if (_startedPosition >= _options.position) {
			return processFirstFrame();
		}

		// Seek was with AVSEEK_FLAG_BACKWARD so first we get old frames.
		// Try skipping frames until one is after the requested position.
		std::swap(_initialSkippingFrame, _stream.decodedFrame);
		if (!_stream.decodedFrame) {
			_stream.decodedFrame = FFmpeg::MakeFramePointer();
		}
	}
}

bool AudioTrack::processFirstFrame() {
	if (!FFmpeg::FrameHasData(_stream.decodedFrame.get())) {
		return false;
	}
	mixerInit();
	callReady();
	return true;
}

bool AudioTrack::fillStateFromFrame() {
	const auto position = FramePosition(_stream);
	if (position == kTimeUnknown) {
		return false;
	}
	_startedPosition = position;
	return true;
}

void AudioTrack::mixerInit() {
	Expects(!initialized());

	auto data = std::make_unique<ExternalSoundData>();
	data->frame = std::move(_stream.decodedFrame);
	data->codec = std::move(_stream.codec);
	data->frequency = _stream.frequency;
	data->length = (_stream.duration * data->frequency) / 1000LL;
	data->speed = _options.speed;

	Media::Player::mixer()->play(
		_audioId,
		std::move(data),
		_startedPosition);
}

void AudioTrack::callReady() {
	Expects(_ready != nullptr);

	auto data = AudioInformation();
	data.state.duration = _stream.duration;
	data.state.position = _startedPosition;
	data.state.receivedTill = _readTillEnd
		? _stream.duration
		: _startedPosition;
	base::take(_ready)({ VideoInformation(), data });
}

void AudioTrack::mixerEnqueue(gsl::span<FFmpeg::Packet> packets) {
	Media::Player::mixer()->feedFromExternal({
		_audioId,
		packets
	});
}

void AudioTrack::mixerForceToBuffer() {
	Media::Player::mixer()->forceToBufferExternal(_audioId);
}

void AudioTrack::pause(crl::time time) {
	Expects(initialized());

	Media::Player::mixer()->pause(_audioId, true);
}

void AudioTrack::resume(crl::time time) {
	Expects(initialized());

	Media::Player::mixer()->resume(_audioId, true);
}

void AudioTrack::stop() {
	if (_audioId.externalPlayId()) {
		Media::Player::mixer()->stop(_audioId);
	}
}

void AudioTrack::setSpeed(float64 speed) {
	_options.speed = speed;
	Media::Player::mixer()->setSpeedFromExternal(_audioId, speed);
}

rpl::producer<> AudioTrack::waitingForData() const {
	return _waitingForData.events();
}

rpl::producer<crl::time> AudioTrack::playPosition() {
	Expects(_ready == nullptr);

	if (!_subscription) {
		_subscription = Media::Player::Updated(
		) | rpl::start_with_next([=](const AudioMsgId &id) {
			using State = Media::Player::State;
			if (id != _audioId) {
				return;
			}
			const auto state = Media::Player::mixer()->currentState(
				_audioId.type());
			if (state.id != _audioId) {
				// #TODO streaming later muted by other
				return;
			} else switch (state.state) {
			case State::Stopped:
			case State::StoppedAtEnd:
			case State::PausedAtEnd:
				_playPosition.reset();
				return;
			case State::StoppedAtError:
			case State::StoppedAtStart:
				_error(Error::InvalidData);
				return;
			case State::Starting:
			case State::Playing:
			case State::Stopping:
			case State::Pausing:
			case State::Resuming:
				if (state.waitingForData) {
					_waitingForData.fire({});
				}
				_playPosition = std::clamp(
					crl::time((state.position * 1000 + (state.frequency / 2))
						/ state.frequency),
					crl::time(0),
					_stream.duration - 1);
				return;
			case State::Paused:
				return;
			}
		});
	}
	return _playPosition.value();
}

AudioTrack::~AudioTrack() {
	stop();
}

} // namespace Streaming
} // namespace Media
