/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio_ffmpeg_loader.h"

#include "core/file_location.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "base/bytes.h"

namespace Media {

uint64_t AbstractFFMpegLoader::ComputeChannelLayout(
	uint64_t channel_layout,
	int channels) {
	if (channel_layout) {
		if (av_get_channel_layout_nb_channels(channel_layout) == channels) {
			return channel_layout;
		}
	}
	return av_get_default_channel_layout(channels);
}

int64 AbstractFFMpegLoader::Mul(int64 value, AVRational rational) {
	return value * rational.num / rational.den;
}

bool AbstractFFMpegLoader::open(crl::time positionMs) {
	if (!AudioPlayerLoader::openFile()) {
		return false;
	}

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	ioBuffer = (uchar *)av_malloc(FFmpeg::kAVBlockSize);
	if (!_data.isEmpty()) {
		ioContext = avio_alloc_context(ioBuffer, FFmpeg::kAVBlockSize, 0, reinterpret_cast<void *>(this), &AbstractFFMpegLoader::_read_data, 0, &AbstractFFMpegLoader::_seek_data);
	} else if (!_bytes.empty()) {
		ioContext = avio_alloc_context(ioBuffer, FFmpeg::kAVBlockSize, 0, reinterpret_cast<void *>(this), &AbstractFFMpegLoader::_read_bytes, 0, &AbstractFFMpegLoader::_seek_bytes);
	} else {
		ioContext = avio_alloc_context(ioBuffer, FFmpeg::kAVBlockSize, 0, reinterpret_cast<void *>(this), &AbstractFFMpegLoader::_read_file, 0, &AbstractFFMpegLoader::_seek_file);
	}
	fmtContext = avformat_alloc_context();
	if (!fmtContext) {
		DEBUG_LOG(("Audio Read Error: Unable to avformat_alloc_context for file '%1', data size '%2'").arg(_file.name()).arg(_data.size()));
		return false;
	}
	fmtContext->pb = ioContext;

	if ((res = avformat_open_input(&fmtContext, 0, 0, 0)) < 0) {
		ioBuffer = 0;

		DEBUG_LOG(("Audio Read Error: Unable to avformat_open_input for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}
	_opened = true;

	if ((res = avformat_find_stream_info(fmtContext, 0)) < 0) {
		DEBUG_LOG(("Audio Read Error: Unable to avformat_find_stream_info for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	streamId = av_find_best_stream(fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (streamId < 0) {
		LOG(("Audio Error: Unable to av_find_best_stream for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(streamId).arg(av_make_error_string(err, sizeof(err), streamId)));
		return false;
	}

	const auto stream = fmtContext->streams[streamId];
	const auto params = stream->codecpar;
	_samplesFrequency = params->sample_rate;
	if (stream->duration != AV_NOPTS_VALUE) {
		_samplesCount = Mul(
			stream->duration * _samplesFrequency,
			stream->time_base);

	} else {
		_samplesCount = Mul(
			fmtContext->duration * _samplesFrequency,
			{ 1, AV_TIME_BASE });
	}

	return true;
}

AbstractFFMpegLoader::~AbstractFFMpegLoader() {
	if (_opened) {
		avformat_close_input(&fmtContext);
	}
	if (ioContext) {
		av_freep(&ioContext->buffer);
		av_freep(&ioContext);
	} else if (ioBuffer) {
		av_freep(&ioBuffer);
	}
	if (fmtContext) avformat_free_context(fmtContext);
}

int AbstractFFMpegLoader::_read_data(void *opaque, uint8_t *buf, int buf_size) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);

	auto nbytes = qMin(l->_data.size() - l->_dataPos, int32(buf_size));
	if (nbytes <= 0) {
		return 0;
	}

	memcpy(buf, l->_data.constData() + l->_dataPos, nbytes);
	l->_dataPos += nbytes;
	return nbytes;
}

int64_t AbstractFFMpegLoader::_seek_data(void *opaque, int64_t offset, int whence) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);

	int32 newPos = -1;
	switch (whence) {
	case SEEK_SET: newPos = offset; break;
	case SEEK_CUR: newPos = l->_dataPos + offset; break;
	case SEEK_END: newPos = l->_data.size() + offset; break;
	case AVSEEK_SIZE: {
		// Special whence for determining filesize without any seek.
		return l->_data.size();
	} break;
	}
	if (newPos < 0 || newPos > l->_data.size()) {
		return -1;
	}
	l->_dataPos = newPos;
	return l->_dataPos;
}

int AbstractFFMpegLoader::_read_bytes(void *opaque, uint8_t *buf, int buf_size) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);

	auto nbytes = qMin(static_cast<int>(l->_bytes.size()) - l->_dataPos, buf_size);
	if (nbytes <= 0) {
		return 0;
	}

	memcpy(buf, l->_bytes.data() + l->_dataPos, nbytes);
	l->_dataPos += nbytes;
	return nbytes;
}

int64_t AbstractFFMpegLoader::_seek_bytes(void *opaque, int64_t offset, int whence) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);

	int32 newPos = -1;
	switch (whence) {
	case SEEK_SET: newPos = offset; break;
	case SEEK_CUR: newPos = l->_dataPos + offset; break;
	case SEEK_END: newPos = static_cast<int>(l->_bytes.size()) + offset; break;
	case AVSEEK_SIZE:
	{
		// Special whence for determining filesize without any seek.
		return l->_bytes.size();
	} break;
	}
	if (newPos < 0 || newPos > l->_bytes.size()) {
		return -1;
	}
	l->_dataPos = newPos;
	return l->_dataPos;
}

int AbstractFFMpegLoader::_read_file(void *opaque, uint8_t *buf, int buf_size) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);
	return int(l->_f.read((char *)(buf), buf_size));
}

int64_t AbstractFFMpegLoader::_seek_file(void *opaque, int64_t offset, int whence) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);

	switch (whence) {
	case SEEK_SET: return l->_f.seek(offset) ? l->_f.pos() : -1;
	case SEEK_CUR: return l->_f.seek(l->_f.pos() + offset) ? l->_f.pos() : -1;
	case SEEK_END: return l->_f.seek(l->_f.size() + offset) ? l->_f.pos() : -1;
	case AVSEEK_SIZE:
	{
		// Special whence for determining filesize without any seek.
		return l->_f.size();
	} break;
	}
	return -1;
}

AbstractAudioFFMpegLoader::AbstractAudioFFMpegLoader(
	const Core::FileLocation &file,
	const QByteArray &data,
	bytes::vector &&buffer)
: AbstractFFMpegLoader(file, data, std::move(buffer))
, _frame(FFmpeg::MakeFramePointer()) {
}

bool AbstractAudioFFMpegLoader::initUsingContext(
		not_null<AVCodecContext*> context,
		int64 initialCount,
		int initialFrequency) {
	const auto layout = ComputeChannelLayout(
		context->channel_layout,
		context->channels);
	if (!layout) {
		LOG(("Audio Error: Unknown channel layout %1 for %2 channels."
			).arg(context->channel_layout
			).arg(context->channels
			));
		return false;
	}

	_swrSrcSampleFormat = context->sample_fmt;
	switch (layout) {
	case AV_CH_LAYOUT_MONO:
		switch (_swrSrcSampleFormat) {
		case AV_SAMPLE_FMT_U8:
		case AV_SAMPLE_FMT_U8P:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			_swrDstChannelLayout = layout;
			_outputChannels = 1;
			_outputSampleSize = 1;
			_outputFormat = AL_FORMAT_MONO8;
			break;
		case AV_SAMPLE_FMT_S16:
		case AV_SAMPLE_FMT_S16P:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			_swrDstChannelLayout = layout;
			_outputChannels = 1;
			_outputSampleSize = sizeof(uint16);
			_outputFormat = AL_FORMAT_MONO16;
			break;
		}
		break;
	case AV_CH_LAYOUT_STEREO:
		switch (_swrSrcSampleFormat) {
		case AV_SAMPLE_FMT_U8:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			_swrDstChannelLayout = layout;
			_outputChannels = 2;
			_outputSampleSize = 2;
			_outputFormat = AL_FORMAT_STEREO8;
			break;
		case AV_SAMPLE_FMT_S16:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			_swrDstChannelLayout = layout;
			_outputChannels = 2;
			_outputSampleSize = 2 * sizeof(uint16);
			_outputFormat = AL_FORMAT_STEREO16;
			break;
		}
		break;
	}

	if (_swrDstRate == initialFrequency) {
		_outputSamplesCount = initialCount;
	} else {
		_outputSamplesCount = av_rescale_rnd(
			initialCount,
			_swrDstRate,
			initialFrequency,
			AV_ROUND_UP);
	}
	return true;
}

auto AbstractAudioFFMpegLoader::replaceFrameAndRead(
	FFmpeg::FramePointer frame,
	QByteArray &result,
	int64 &samplesAdded)
-> ReadResult {
	_frame = std::move(frame);
	return readFromReadyFrame(result, samplesAdded);
}

auto AbstractAudioFFMpegLoader::readFromReadyContext(
	not_null<AVCodecContext *> context,
	QByteArray &result,
	int64 &samplesAdded)
-> ReadResult {
	const auto res = avcodec_receive_frame(context, _frame.get());
	if (res >= 0) {
		return readFromReadyFrame(result, samplesAdded);
	}

	if (res == AVERROR_EOF) {
		return ReadResult::EndOfFile;
	} else if (res != AVERROR(EAGAIN)) {
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Audio Error: "
			"Unable to avcodec_receive_frame() file '%1', data size '%2', "
			"error %3, %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(res
			).arg(av_make_error_string(err, sizeof(err), res)
			));
		return ReadResult::Error;
	}
	return ReadResult::Wait;
}

bool AbstractAudioFFMpegLoader::frameHasDesiredFormat() const {
	const auto frameChannelLayout = ComputeChannelLayout(
		_frame->channel_layout,
		_frame->channels);
	return true
		&& (_frame->format == _swrDstSampleFormat)
		&& (frameChannelLayout == _swrDstChannelLayout)
		&& (_frame->sample_rate == _swrDstRate);
}

bool AbstractAudioFFMpegLoader::initResampleForFrame() {
	const auto frameChannelLayout = ComputeChannelLayout(
		_frame->channel_layout,
		_frame->channels);
	if (!frameChannelLayout) {
		LOG(("Audio Error: "
			"Unable to compute channel layout for frame in file '%1', "
			"data size '%2', channel_layout %3, channels %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(_frame->channel_layout
			).arg(_frame->channels
			));
		return false;
	} else if (_frame->format == -1) {
		LOG(("Audio Error: "
			"Unknown frame format in file '%1', data size '%2'"
			).arg(_file.name()
			).arg(_data.size()
			));
		return false;
	} else if (_swrContext) {
		if (true
			&& (_frame->format == _swrSrcSampleFormat)
			&& (frameChannelLayout == _swrSrcChannelLayout)
			&& (_frame->sample_rate == _swrSrcRate)) {
			return true;
		}
		swr_close(_swrContext);
	}

	_swrSrcSampleFormat = static_cast<AVSampleFormat>(_frame->format);
	_swrSrcChannelLayout = frameChannelLayout;
	_swrSrcRate = _frame->sample_rate;
	return initResampleUsingFormat();
}

bool AbstractAudioFFMpegLoader::initResampleUsingFormat() {
	int res = 0;

	_swrContext = swr_alloc_set_opts(
		_swrContext,
		_swrDstChannelLayout,
		_swrDstSampleFormat,
		_swrDstRate,
		_swrSrcChannelLayout,
		_swrSrcSampleFormat,
		_swrSrcRate,
		0,
		nullptr);
	if (!_swrContext) {
		LOG(("Audio Error: "
			"Unable to swr_alloc for file '%1', data size '%2'"
			).arg(_file.name()
			).arg(_data.size()));
		return false;
	} else if ((res = swr_init(_swrContext)) < 0) {
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Audio Error: "
			"Unable to swr_init for file '%1', data size '%2', "
			"error %3, %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(res
			).arg(av_make_error_string(err, sizeof(err), res)
			));
		return false;
	}
	if (_swrDstData) {
		av_freep(&_swrDstData[0]);
		_swrDstDataCapacity = -1;
	}
	return true;
}

bool AbstractAudioFFMpegLoader::ensureResampleSpaceAvailable(int samples) {
	if (_swrDstData != nullptr && _swrDstDataCapacity >= samples) {
		return true;
	}
	const auto allocate = std::max(samples, int(av_rescale_rnd(
		FFmpeg::kAVBlockSize / _outputSampleSize,
		_swrDstRate,
		_swrSrcRate,
		AV_ROUND_UP)));

	if (_swrDstData) {
		av_freep(&_swrDstData[0]);
	}
	const auto res = _swrDstData
		? av_samples_alloc(
			_swrDstData,
			nullptr,
			_outputChannels,
			allocate,
			_swrDstSampleFormat,
			0)
		: av_samples_alloc_array_and_samples(
			&_swrDstData,
			nullptr,
			_outputChannels,
			allocate,
			_swrDstSampleFormat,
			0);
	if (res < 0) {
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Audio Error: "
			"Unable to av_samples_alloc for file '%1', data size '%2', "
			"error %3, %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(res
			).arg(av_make_error_string(err, sizeof(err), res)
			));
		return false;
	}
	_swrDstDataCapacity = allocate;
	return true;
}

void AbstractAudioFFMpegLoader::appendSamples(
	QByteArray & result,
	int64 & samplesAdded,
	uint8_t * *data,
	int count) const {
	result.append(
		reinterpret_cast<const char *>(data[0]),
		count * _outputSampleSize);
	samplesAdded += count;
}

AudioPlayerLoader::ReadResult AbstractAudioFFMpegLoader::readFromReadyFrame(
	QByteArray & result,
	int64 & samplesAdded) {
	if (frameHasDesiredFormat()) {
		appendSamples(
			result,
			samplesAdded,
			_frame->extended_data,
			_frame->nb_samples);
		return ReadResult::Ok;
	} else if (!initResampleForFrame()) {
		return ReadResult::Error;
	}

	const auto maxSamples = av_rescale_rnd(
		swr_get_delay(_swrContext, _swrSrcRate) + _frame->nb_samples,
		_swrDstRate,
		_swrSrcRate,
		AV_ROUND_UP);
	if (!ensureResampleSpaceAvailable(maxSamples)) {
		return ReadResult::Error;
	}
	const auto samples = swr_convert(
		_swrContext,
		_swrDstData,
		maxSamples,
		(const uint8_t * *)_frame->extended_data,
		_frame->nb_samples);
	if (samples < 0) {
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Audio Error: "
			"Unable to swr_convert for file '%1', data size '%2', "
			"error %3, %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(samples
			).arg(av_make_error_string(err, sizeof(err), samples)
			));
		return ReadResult::Error;
	}

	appendSamples(
		result,
		samplesAdded,
		_swrDstData,
		samples);
	return ReadResult::Ok;
}

AbstractAudioFFMpegLoader::~AbstractAudioFFMpegLoader() {
	if (_swrContext) {
		swr_free(&_swrContext);
	}
	if (_swrDstData) {
		if (_swrDstData[0]) {
			av_freep(&_swrDstData[0]);
		}
		av_freep(&_swrDstData);
	}
}

FFMpegLoader::FFMpegLoader(
	const Core::FileLocation & file,
	const QByteArray & data,
	bytes::vector && buffer)
: AbstractAudioFFMpegLoader(file, data, std::move(buffer)) {
}

bool FFMpegLoader::open(crl::time positionMs) {
	if (!AbstractFFMpegLoader::open(positionMs)) {
		return false;
	}
	if (!openCodecContext()) {
		return false;
	}
	if (!initUsingContext(_codecContext, _samplesCount, _samplesFrequency)) {
		return false;
	}
	return seekTo(positionMs);
}

bool FFMpegLoader::openCodecContext() {
	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	_codecContext = avcodec_alloc_context3(nullptr);
	if (!_codecContext) {
		LOG(("Audio Error: "
			"Unable to avcodec_alloc_context3 for file '%1', data size '%2'"
			).arg(_file.name()
			).arg(_data.size()
			));
		return false;
	}

	const auto stream = fmtContext->streams[streamId];
	if ((res = avcodec_parameters_to_context(
		_codecContext,
		stream->codecpar)) < 0) {
		LOG(("Audio Error: "
			"Unable to avcodec_parameters_to_context for file '%1', "
			"data size '%2', error %3, %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(res
			).arg(av_make_error_string(err, sizeof(err), res)
			));
		return false;
	}
	av_codec_set_pkt_timebase(_codecContext, stream->time_base);
	av_opt_set_int(_codecContext, "refcounted_frames", 1, 0);

	if ((res = avcodec_open2(_codecContext, codec, 0)) < 0) {
		LOG(("Audio Error: "
			"Unable to avcodec_open2 for file '%1', data size '%2', "
			"error %3, %4"
			).arg(_file.name()
			).arg(_data.size()
			).arg(res
			).arg(av_make_error_string(err, sizeof(err), res)
			));
		return false;
	}
	return true;
}

bool FFMpegLoader::seekTo(crl::time positionMs) {
	if (positionMs) {
		const auto stream = fmtContext->streams[streamId];
		const auto timeBase = stream->time_base;
		const auto timeStamp = (positionMs * timeBase.den)
			/ (1000LL * timeBase.num);
		const auto flags1 = AVSEEK_FLAG_ANY;
		if (av_seek_frame(fmtContext, streamId, timeStamp, flags1) < 0) {
			const auto flags2 = 0;
			if (av_seek_frame(fmtContext, streamId, timeStamp, flags2) < 0) {
			}
		}
	}

	return true;
}

AudioPlayerLoader::ReadResult FFMpegLoader::readMore(
	QByteArray & result,
	int64 & samplesAdded) {
	const auto readResult = readFromReadyContext(
		_codecContext,
		result,
		samplesAdded);
	if (readResult != ReadResult::Wait) {
		return readResult;
	}

	auto res = 0;
	if ((res = av_read_frame(fmtContext, &_packet)) < 0) {
		if (res != AVERROR_EOF) {
			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Audio Error: "
				"Unable to av_read_frame() file '%1', data size '%2', "
				"error %3, %4"
				).arg(_file.name()
				).arg(_data.size()
				).arg(res
				).arg(av_make_error_string(err, sizeof(err), res)
				));
			return ReadResult::Error;
		}
		avcodec_send_packet(_codecContext, nullptr); // drain
		return ReadResult::Ok;
	}

	if (_packet.stream_index == streamId) {
		res = avcodec_send_packet(_codecContext, &_packet);
		if (res < 0) {
			av_packet_unref(&_packet);

			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Audio Error: "
				"Unable to avcodec_send_packet() file '%1', data size '%2', "
				"error %3, %4"
				).arg(_file.name()
				).arg(_data.size()
				).arg(res
				).arg(av_make_error_string(err, sizeof(err), res)
				));
			// There is a sample voice message where skipping such packet
			// results in a crash (read_access to nullptr) in swr_convert().
			//if (res == AVERROR_INVALIDDATA) {
			//	return ReadResult::NotYet; // try to skip bad packet
			//}
			return ReadResult::Error;
		}
	}
	av_packet_unref(&_packet);
	return ReadResult::Ok;
}

FFMpegLoader::~FFMpegLoader() {
	if (_codecContext) {
		avcodec_free_context(&_codecContext);
	}
}

} // namespace Media
