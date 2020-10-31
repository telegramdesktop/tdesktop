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

constexpr AVSampleFormat AudioToFormat = AV_SAMPLE_FMT_S16;
constexpr int64_t AudioToChannelLayout = AV_CH_LAYOUT_STEREO;
constexpr int32 AudioToChannels = 2;

bool IsPlanarFormat(int format) {
	return (format == AV_SAMPLE_FMT_U8P)
		|| (format == AV_SAMPLE_FMT_S16P)
		|| (format == AV_SAMPLE_FMT_S32P)
		|| (format == AV_SAMPLE_FMT_FLTP)
		|| (format == AV_SAMPLE_FMT_DBLP)
		|| (format == AV_SAMPLE_FMT_S64P);
}

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

bool ChildFFMpegLoader::open(crl::time positionMs) {
	return initUsingContext(
		_parentData->codec.get(),
		_parentData->length,
		_parentData->frequency);
}

AudioPlayerLoader::ReadResult ChildFFMpegLoader::readFromInitialFrame(
		QByteArray &result,
		int64 &samplesAdded) {
	if (!_parentData->frame) {
		return ReadResult::Wait;
	}
	return replaceFrameAndRead(
		base::take(_parentData->frame),
		result,
		samplesAdded);
}

AudioPlayerLoader::ReadResult ChildFFMpegLoader::readMore(
	QByteArray & result,
	int64 & samplesAdded) {
	const auto initialFrameResult = readFromInitialFrame(
		result,
		samplesAdded);
	if (initialFrameResult != ReadResult::Wait) {
		return initialFrameResult;
	}

	const auto readResult = readFromReadyContext(
		_parentData->codec.get(),
		result,
		samplesAdded);
	if (readResult != ReadResult::Wait) {
		return readResult;
	}

	if (_queue.empty()) {
		return _eofReached ? ReadResult::EndOfFile : ReadResult::Wait;
	}

	auto packet = std::move(_queue.front());
	_queue.pop_front();

	_eofReached = packet.empty();
	if (_eofReached) {
		avcodec_send_packet(_parentData->codec.get(), nullptr); // drain
		return ReadResult::Ok;
	}

	auto res = avcodec_send_packet(
		_parentData->codec.get(),
		&packet.fields());
	if (res < 0) {
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Audio Error: Unable to avcodec_send_packet() file '%1', "
			"data size '%2', error %3, %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(res
			).arg(av_make_error_string(err, sizeof(err), res)));
		// There is a sample voice message where skipping such packet
		// results in a crash (read_access to nullptr) in swr_convert().
		if (res == AVERROR_INVALIDDATA) {
			return ReadResult::NotYet; // try to skip bad packet
		}
		return ReadResult::Error;
	}
	return ReadResult::Ok;
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
