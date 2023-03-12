/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_child_ffmpeg_loader.h"

#include "core/crash_reports.h"
#include "core/file_location.h"

namespace Media {
namespace {

using FFmpeg::AvErrorWrap;

} // namespace

ChildFFMpegLoader::ChildFFMpegLoader(
	std::unique_ptr<ExternalSoundData> &&data)
: AbstractAudioFFMpegLoader(
	Core::FileLocation(),
	QByteArray(),
	bytes::vector())
, _parentData(std::move(data)) {
	Expects(_parentData->codec != nullptr);
}

bool ChildFFMpegLoader::open(crl::time positionMs, float64 speed) {
	const auto sample = (positionMs * samplesFrequency()) / 1000LL;
	overrideDuration(sample, _parentData->duration);
	return initUsingContext(_parentData->codec.get(), speed);
}

auto ChildFFMpegLoader::readFromInitialFrame() -> ReadResult {
	if (!_parentData->frame) {
		return ReadError::Wait;
	}
	return replaceFrameAndRead(base::take(_parentData->frame));
}

auto ChildFFMpegLoader::readMore() -> ReadResult {
	if (_readTillEnd) {
		return ReadError::EndOfFile;
	}
	const auto initialFrameResult = readFromInitialFrame();
	if (initialFrameResult != ReadError::Wait) {
		return initialFrameResult;
	}

	const auto readResult = readFromReadyContext(
		_parentData->codec.get());
	if (readResult != ReadError::Wait) {
		return readResult;
	}

	if (_queue.empty()) {
		if (!_eofReached) {
			return ReadError::Wait;
		}
		_readTillEnd = true;
		return ReadError::EndOfFile;
	}

	auto packet = std::move(_queue.front());
	_queue.pop_front();

	_eofReached = packet.empty();
	if (_eofReached) {
		avcodec_send_packet(_parentData->codec.get(), nullptr); // drain
		return ReadError::Retry;
	}

	AvErrorWrap error = avcodec_send_packet(
		_parentData->codec.get(),
		&packet.fields());
	if (error) {
		LogError(u"avcodec_send_packet"_q, error);
		// There is a sample voice message where skipping such packet
		// results in a crash (read_access to nullptr) in swr_convert().
		if (error.code() == AVERROR_INVALIDDATA) {
			return ReadError::Retry; // try to skip bad packet
		}
		return ReadError::Other;
	}
	return ReadError::Retry;
}

void ChildFFMpegLoader::enqueuePackets(
		std::deque<FFmpeg::Packet> &&packets) {
	if (_queue.empty()) {
		_queue = std::move(packets);
	} else {
		_queue.insert(
			end(_queue),
			std::make_move_iterator(packets.begin()),
			std::make_move_iterator(packets.end()));
	}
	packets.clear();
}

void ChildFFMpegLoader::setForceToBuffer(bool force) {
	_forceToBuffer = force;
}

bool ChildFFMpegLoader::forceToBuffer() const {
	return _forceToBuffer;
}

ChildFFMpegLoader::~ChildFFMpegLoader() = default;

} // namespace Media
