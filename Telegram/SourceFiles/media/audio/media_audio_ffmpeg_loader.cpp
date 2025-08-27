/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio_ffmpeg_loader.h"

#include "base/bytes.h"
#include "core/file_location.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "media/media_common.h"

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
} // extern "C"

namespace Media {
namespace {

using FFmpeg::AvErrorWrap;
using FFmpeg::LogError;

} // namespace

#if !DA_FFMPEG_NEW_CHANNEL_LAYOUT
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
#endif // !DA_FFMPEG_NEW_CHANNEL_LAYOUT

int64 AbstractFFMpegLoader::Mul(int64 value, AVRational rational) {
	return value * rational.num / rational.den;
}

bool AbstractFFMpegLoader::open(crl::time positionMs, float64 speed) {
	if (!AudioPlayerLoader::openFile()) {
		return false;
	}

	ioBuffer = (uchar *)av_malloc(FFmpeg::kAVBlockSize);
	if (!_data.isEmpty()) {
		ioContext = avio_alloc_context(ioBuffer, FFmpeg::kAVBlockSize, 0, reinterpret_cast<void *>(this), &AbstractFFMpegLoader::ReadData, 0, &AbstractFFMpegLoader::SeekData);
	} else if (!_bytes.empty()) {
		ioContext = avio_alloc_context(ioBuffer, FFmpeg::kAVBlockSize, 0, reinterpret_cast<void *>(this), &AbstractFFMpegLoader::ReadBytes, 0, &AbstractFFMpegLoader::SeekBytes);
	} else {
		ioContext = avio_alloc_context(ioBuffer, FFmpeg::kAVBlockSize, 0, reinterpret_cast<void *>(this), &AbstractFFMpegLoader::ReadFile, 0, &AbstractFFMpegLoader::SeekFile);
	}
	fmtContext = avformat_alloc_context();
	if (!fmtContext) {
		LogError(u"avformat_alloc_context"_q);
		return false;
	}
	fmtContext->pb = ioContext;

	if (AvErrorWrap error = avformat_open_input(&fmtContext, 0, 0, 0)) {
		ioBuffer = nullptr;
		LogError(u"avformat_open_input"_q, error);
		return false;
	}
	_opened = true;

	if (AvErrorWrap error = avformat_find_stream_info(fmtContext, 0)) {
		LogError(u"avformat_find_stream_info"_q, error);
		return false;
	}

	streamId = av_find_best_stream(fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (streamId < 0) {
		FFmpeg::LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
		return false;
	}

	const auto stream = fmtContext->streams[streamId];
	const auto params = stream->codecpar;
	_samplesFrequency = params->sample_rate;
	if (stream->duration != AV_NOPTS_VALUE) {
		_duration = Mul(stream->duration * 1000, stream->time_base);
	} else {
		_duration = Mul(fmtContext->duration * 1000, { 1, AV_TIME_BASE });
	}
	_startedAtSample = (positionMs * _samplesFrequency) / 1000LL;

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

int AbstractFFMpegLoader::ReadData(void *opaque, uint8_t *buf, int buf_size) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);

	auto nbytes = qMin(l->_data.size() - l->_dataPos, int32(buf_size));
	if (nbytes <= 0) {
		return AVERROR_EOF;
	}

	memcpy(buf, l->_data.constData() + l->_dataPos, nbytes);
	l->_dataPos += nbytes;
	return nbytes;
}

int64_t AbstractFFMpegLoader::SeekData(void *opaque, int64_t offset, int whence) {
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

int AbstractFFMpegLoader::ReadBytes(void *opaque, uint8_t *buf, int buf_size) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);

	auto nbytes = qMin(static_cast<int>(l->_bytes.size()) - l->_dataPos, buf_size);
	if (nbytes <= 0) {
		return AVERROR_EOF;
	}

	memcpy(buf, l->_bytes.data() + l->_dataPos, nbytes);
	l->_dataPos += nbytes;
	return nbytes;
}

int64_t AbstractFFMpegLoader::SeekBytes(void *opaque, int64_t offset, int whence) {
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

int AbstractFFMpegLoader::ReadFile(void *opaque, uint8_t *buf, int buf_size) {
	auto l = reinterpret_cast<AbstractFFMpegLoader *>(opaque);
	int ret = l->_f.read((char *)(buf), buf_size);
	switch (ret) {
	case -1: return AVERROR_EXTERNAL;
	case 0: return AVERROR_EOF;
	default: return ret;
	}
}

int64_t AbstractFFMpegLoader::SeekFile(void *opaque, int64_t offset, int whence) {
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

void AbstractAudioFFMpegLoader::dropFramesTill(int64 samples) {
	const auto isAfter = [&](const EnqueuedFrame &frame) {
		return frame.position > samples;
	};
	const auto from = begin(_framesQueued);
	const auto after = ranges::find_if(_framesQueued, isAfter);
	if (from == after) {
		return;
	}
	const auto till = after - 1;
	const auto erasing = till - from;
	if (erasing > 0) {
		if (_framesQueuedIndex >= 0) {
			Assert(_framesQueuedIndex >= erasing);
			_framesQueuedIndex -= erasing;
		}
		_framesQueued.erase(from, till);
		if (_framesQueued.empty()) {
			_framesQueuedIndex = -1;
		}
	}
}

int64 AbstractAudioFFMpegLoader::startReadingQueuedFrames(float64 newSpeed) {
	changeSpeedFilter(newSpeed);
	if (_framesQueued.empty()) {
		_framesQueuedIndex = -1;
		return -1;
	}
	_framesQueuedIndex = 0;
	return _framesQueued.front().position;
}

bool AbstractAudioFFMpegLoader::initUsingContext(
		not_null<AVCodecContext*> context,
		float64 speed) {
	_swrSrcSampleFormat = context->sample_fmt;
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	const AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
	const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	const auto useMono = !av_channel_layout_compare(
		&context->ch_layout,
		&mono);
	const auto useStereo = !av_channel_layout_compare(
		&context->ch_layout,
		&stereo);
	const auto copyDstChannelLayout = [&] {
		av_channel_layout_copy(&_swrDstChannelLayout, &context->ch_layout);
	};
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
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
	const auto useMono = (layout == AV_CH_LAYOUT_MONO);
	const auto useStereo = (layout == AV_CH_LAYOUT_STEREO);
	const auto copyDstChannelLayout = [&] {
		_swrDstChannelLayout = layout;
	};
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	if (useMono) {
		switch (_swrSrcSampleFormat) {
		case AV_SAMPLE_FMT_U8:
		case AV_SAMPLE_FMT_U8P:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			copyDstChannelLayout();
			_outputChannels = 1;
			_outputSampleSize = 1;
			_outputFormat = AL_FORMAT_MONO8;
			break;
		case AV_SAMPLE_FMT_S16:
		case AV_SAMPLE_FMT_S16P:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			copyDstChannelLayout();
			_outputChannels = 1;
			_outputSampleSize = sizeof(uint16);
			_outputFormat = AL_FORMAT_MONO16;
			break;
		}
	} else if (useStereo) {
		switch (_swrSrcSampleFormat) {
		case AV_SAMPLE_FMT_U8:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			copyDstChannelLayout();
			_outputChannels = 2;
			_outputSampleSize = 2;
			_outputFormat = AL_FORMAT_STEREO8;
			break;
		case AV_SAMPLE_FMT_S16:
			_swrDstSampleFormat = _swrSrcSampleFormat;
			copyDstChannelLayout();
			_outputChannels = 2;
			_outputSampleSize = 2 * sizeof(uint16);
			_outputFormat = AL_FORMAT_STEREO16;
			break;
		}
	}

	createSpeedFilter(speed);

	return true;
}

auto AbstractAudioFFMpegLoader::replaceFrameAndRead(
	FFmpeg::FramePointer frame)
-> ReadResult {
	_frame = std::move(frame);
	return readFromReadyFrame();
}

auto AbstractAudioFFMpegLoader::readFromReadyContext(
	not_null<AVCodecContext*> context)
-> ReadResult {
	if (_filterGraph) {
		AvErrorWrap error = av_buffersink_get_frame(
			_filterSink,
			_filteredFrame.get());
		if (!error) {
			if (!_filteredFrame->nb_samples) {
				return ReadError::Retry;
			}
			return bytes::const_span(
				reinterpret_cast<const bytes::type*>(
					_filteredFrame->extended_data[0]),
				_filteredFrame->nb_samples * _outputSampleSize);
		} else if (error.code() == AVERROR_EOF) {
			return ReadError::EndOfFile;
		} else if (error.code() != AVERROR(EAGAIN)) {
			LogError(u"av_buffersink_get_frame"_q, error);
			return ReadError::Other;
		}
	}
	using Enqueued = not_null<const EnqueuedFrame*>;
	const auto queueResult = fillFrameFromQueued();
	if (queueResult == ReadError::RetryNotQueued) {
		return ReadError::RetryNotQueued;
	} else if (const auto enqueued = std::get_if<Enqueued>(&queueResult)) {
		const auto raw = (*enqueued)->frame.get();
		Assert(frameHasDesiredFormat(raw));
		return readOrBufferForFilter(raw, (*enqueued)->samples);
	}

	const auto queueError = v::get<ReadError>(queueResult);
	AvErrorWrap error = (queueError == ReadError::EndOfFile)
		? AVERROR_EOF
		: avcodec_receive_frame(context, _frame.get());
	if (!error) {
		return readFromReadyFrame();
	}

	if (error.code() == AVERROR_EOF) {
		enqueueFramesFinished();
		if (!_filterGraph) {
			return ReadError::EndOfFile;
		}
		AvErrorWrap error = av_buffersrc_add_frame(_filterSrc, nullptr);
		if (!error) {
			return ReadError::Retry;
		}
		LogError(u"av_buffersrc_add_frame"_q, error);
		return ReadError::Other;
	} else if (error.code() != AVERROR(EAGAIN)) {
		LogError(u"avcodec_receive_frame"_q, error);
		return ReadError::Other;
	}
	return ReadError::Wait;
}

auto AbstractAudioFFMpegLoader::fillFrameFromQueued()
-> std::variant<not_null<const EnqueuedFrame*>, ReadError> {
	if (_framesQueuedIndex == _framesQueued.size()) {
		_framesQueuedIndex = -1;
		return ReadError::RetryNotQueued;
	} else if (_framesQueuedIndex < 0) {
		return ReadError::Wait;
	}
	const auto &queued = _framesQueued[_framesQueuedIndex];
	++_framesQueuedIndex;

	if (!queued.frame) {
		return ReadError::EndOfFile;
	}
	return &queued;
}

bool AbstractAudioFFMpegLoader::frameHasDesiredFormat(
		not_null<AVFrame*> frame) const {
	const auto sameChannelLayout = [&] {
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
		return !av_channel_layout_compare(
			&frame->ch_layout,
			&_swrDstChannelLayout);
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		const auto frameChannelLayout = ComputeChannelLayout(
			frame->channel_layout,
			frame->channels);
		return (frameChannelLayout == _swrDstChannelLayout);
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	};
	return true
		&& (frame->format == _swrDstSampleFormat)
		&& (frame->sample_rate == _swrDstRate)
		&& sameChannelLayout();
}

bool AbstractAudioFFMpegLoader::initResampleForFrame() {
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	const auto bad = !_frame->ch_layout.nb_channels;
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	const auto frameChannelLayout = ComputeChannelLayout(
		_frame->channel_layout,
		_frame->channels);
	const auto bad = !frameChannelLayout;
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	if (bad) {
		LOG(("Audio Error: "
			"Unknown channel layout for frame in file '%1', "
			"data size '%2'"
			).arg(_file.name()
			).arg(_data.size()
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
		const auto sameChannelLayout = [&] {
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
			return !av_channel_layout_compare(
				&_frame->ch_layout,
				&_swrSrcChannelLayout);
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
			return (frameChannelLayout == _swrSrcChannelLayout);
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		};
		if (true
			&& (_frame->format == _swrSrcSampleFormat)
			&& (_frame->sample_rate == _swrSrcRate)
			&& sameChannelLayout()) {
			return true;
		}
		swr_close(_swrContext);
	}

	_swrSrcSampleFormat = static_cast<AVSampleFormat>(_frame->format);
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	av_channel_layout_copy(&_swrSrcChannelLayout, &_frame->ch_layout);
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	_swrSrcChannelLayout = frameChannelLayout;
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	_swrSrcRate = _frame->sample_rate;
	return initResampleUsingFormat();
}

bool AbstractAudioFFMpegLoader::initResampleUsingFormat() {
	AvErrorWrap error = 0;
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	error = swr_alloc_set_opts2(
		&_swrContext,
		&_swrDstChannelLayout,
		_swrDstSampleFormat,
		_swrDstRate,
		&_swrSrcChannelLayout,
		_swrSrcSampleFormat,
		_swrSrcRate,
		0,
		nullptr);
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
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
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	if (error || !_swrContext) {
		LogError(u"swr_alloc_set_opts2"_q, error);
		return false;
	} else if (AvErrorWrap error = swr_init(_swrContext)) {
		LogError(u"swr_init"_q, error);
		return false;
	}
	_resampledFrame = nullptr;
	_resampledFrameCapacity = 0;
	return true;
}

bool AbstractAudioFFMpegLoader::ensureResampleSpaceAvailable(int samples) {
	const auto enlarge = (_resampledFrameCapacity < samples);
	if (!_resampledFrame) {
		_resampledFrame = FFmpeg::MakeFramePointer();
	} else if (enlarge || !av_frame_is_writable(_resampledFrame.get())) {
		av_frame_unref(_resampledFrame.get());
	} else {
		return true;
	}
	const auto allocate = std::max(samples, int(av_rescale_rnd(
		FFmpeg::kAVBlockSize / _outputSampleSize,
		_swrDstRate,
		_swrSrcRate,
		AV_ROUND_UP)));
	_resampledFrame->sample_rate = _swrDstRate;
	_resampledFrame->format = _swrDstSampleFormat;
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	av_channel_layout_copy(
		&_resampledFrame->ch_layout,
		&_swrDstChannelLayout);
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	_resampledFrame->channel_layout = _swrDstChannelLayout;
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	_resampledFrame->nb_samples = allocate;
	if (AvErrorWrap error = av_frame_get_buffer(_resampledFrame.get(), 0)) {
		LogError(u"av_frame_get_buffer"_q, error);
		return false;
	}
	_resampledFrameCapacity = allocate;
	return true;
}

bool AbstractAudioFFMpegLoader::changeSpeedFilter(float64 speed) {
	speed = std::clamp(speed, kSpeedMin, kSpeedMax);
	if (EqualSpeeds(_filterSpeed, speed)) {
		return false;
	}
	avfilter_graph_free(&_filterGraph);
	const auto guard = gsl::finally([&] {
		if (!_filterGraph) {
			_filteredFrame = nullptr;
			_filterSpeed = 1.;
		}
	});
	createSpeedFilter(speed);
	return true;
}

void AbstractAudioFFMpegLoader::createSpeedFilter(float64 speed) {
	Expects(!_filterGraph);

	if (EqualSpeeds(speed, 1.)) {
		return;
	}
	const auto abuffer = avfilter_get_by_name("abuffer");
	const auto abuffersink = avfilter_get_by_name("abuffersink");
	const auto atempo = avfilter_get_by_name("atempo");
	if (!abuffer || !abuffersink || !atempo) {
		LOG(("FFmpeg Error: Could not find abuffer / abuffersink /atempo."));
		return;
	}

	auto graph = avfilter_graph_alloc();
	if (!graph) {
		LOG(("FFmpeg Error: Unable to create filter graph."));
		return;
	}
	const auto guard = gsl::finally([&] {
		avfilter_graph_free(&graph);
	});

	_filterSrc = avfilter_graph_alloc_filter(graph, abuffer, "src");
	_atempo = avfilter_graph_alloc_filter(graph, atempo, "atempo");
	_filterSink = avfilter_graph_alloc_filter(graph, abuffersink, "sink");
	if (!_filterSrc || !atempo || !_filterSink) {
		LOG(("FFmpeg Error: "
			"Could not allocate abuffer / abuffersink /atempo."));
		return;
	}

	char layout[64] = { 0 };
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	av_channel_layout_describe(
		&_swrDstChannelLayout,
		layout,
		sizeof(layout));
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	av_get_channel_layout_string(
		layout,
		sizeof(layout),
		0,
		_swrDstChannelLayout);
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT

	av_opt_set(
		_filterSrc,
		"channel_layout",
		layout,
		AV_OPT_SEARCH_CHILDREN);
	av_opt_set_sample_fmt(
		_filterSrc,
		"sample_fmt",
		_swrDstSampleFormat,
		AV_OPT_SEARCH_CHILDREN);
	av_opt_set_q(
		_filterSrc,
		"time_base",
		AVRational{ 1, _swrDstRate },
		AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(
		_filterSrc,
		"sample_rate",
		_swrDstRate,
		AV_OPT_SEARCH_CHILDREN);
	av_opt_set_double(
		_atempo,
		"tempo",
		speed,
		AV_OPT_SEARCH_CHILDREN);

	AvErrorWrap error = 0;
	if ((error = avfilter_init_str(_filterSrc, nullptr))) {
		LogError(u"avfilter_init_str(src)"_q, error);
		return;
	} else if ((error = avfilter_init_str(_atempo, nullptr))) {
		LogError(u"avfilter_init_str(atempo)"_q, error);
		avfilter_graph_free(&graph);
		return;
	} else if ((error = avfilter_init_str(_filterSink, nullptr))) {
		LogError(u"avfilter_init_str(sink)"_q, error);
		avfilter_graph_free(&graph);
		return;
	} else if ((error = avfilter_link(_filterSrc, 0, _atempo, 0))) {
		LogError(u"avfilter_link(src->atempo)"_q, error);
		avfilter_graph_free(&graph);
		return;
	} else if ((error = avfilter_link(_atempo, 0, _filterSink, 0))) {
		LogError(u"avfilter_link(atempo->sink)"_q, error);
		avfilter_graph_free(&graph);
		return;
	} else if ((error = avfilter_graph_config(graph, nullptr))) {
		LogError("avfilter_link(atempo->sink)"_q, error);
		avfilter_graph_free(&graph);
		return;
	}
	_filterGraph = base::take(graph);
	_filteredFrame = FFmpeg::MakeFramePointer();
	_filterSpeed = speed;
}

void AbstractAudioFFMpegLoader::enqueueNormalFrame(
		not_null<AVFrame*> frame,
		int64 samples) {
	if (_framesQueuedIndex >= 0) {
		return;
	}
	if (!samples) {
		samples = frame->nb_samples;
	}
	_framesQueued.push_back({
		.position = startedAtSample() + _framesQueuedSamples,
		.samples = samples,
		.frame = FFmpeg::DuplicateFramePointer(frame),
	});
	_framesQueuedSamples += samples;
}

void AbstractAudioFFMpegLoader::enqueueFramesFinished() {
	if (_framesQueuedIndex >= 0) {
		return;
	}
	_framesQueued.push_back({
		.position = startedAtSample() + _framesQueuedSamples,
	});
}

auto AbstractAudioFFMpegLoader::readFromReadyFrame()
-> ReadResult {
	const auto raw = _frame.get();
	if (frameHasDesiredFormat(raw)) {
		if (!raw->nb_samples) {
			return ReadError::Retry;
		}
		return readOrBufferForFilter(raw, raw->nb_samples);
	} else if (!initResampleForFrame()) {
		return ReadError::Other;
	}

	const auto maxSamples = av_rescale_rnd(
		swr_get_delay(_swrContext, _swrSrcRate) + _frame->nb_samples,
		_swrDstRate,
		_swrSrcRate,
		AV_ROUND_UP);
	if (!ensureResampleSpaceAvailable(maxSamples)) {
		return ReadError::Other;
	}
	const auto samples = swr_convert(
		_swrContext,
		(uint8_t**)_resampledFrame->extended_data,
		maxSamples,
		(const uint8_t **)_frame->extended_data,
		_frame->nb_samples);
	if (AvErrorWrap error = samples) {
		LogError(u"swr_convert"_q, error);
		return ReadError::Other;
	} else if (!samples) {
		return ReadError::Retry;
	}
	return readOrBufferForFilter(_resampledFrame.get(), samples);
}

auto AbstractAudioFFMpegLoader::readOrBufferForFilter(
	not_null<AVFrame*> frame,
	int64 samplesOverride)
-> ReadResult {
	enqueueNormalFrame(frame, samplesOverride);

	const auto was = frame->nb_samples;
	frame->nb_samples = samplesOverride;
	const auto guard = gsl::finally([&] {
		frame->nb_samples = was;
	});

	if (!_filterGraph) {
		return bytes::const_span(
			reinterpret_cast<const bytes::type*>(frame->extended_data[0]),
			frame->nb_samples * _outputSampleSize);
	}
	AvErrorWrap error = av_buffersrc_add_frame_flags(
		_filterSrc,
		frame,
		AV_BUFFERSRC_FLAG_KEEP_REF);
	if (error) {
		LogError(u"av_buffersrc_add_frame_flags"_q, error);
		return ReadError::Other;
	}
	return ReadError::Retry;
}

AbstractAudioFFMpegLoader::~AbstractAudioFFMpegLoader() {
	if (_filterGraph) {
		avfilter_graph_free(&_filterGraph);
	}
	if (_swrContext) {
		swr_free(&_swrContext);
	}
}

FFMpegLoader::FFMpegLoader(
	const Core::FileLocation &file,
	const QByteArray &data,
	bytes::vector &&buffer)
: AbstractAudioFFMpegLoader(file, data, std::move(buffer)) {
}

bool FFMpegLoader::open(crl::time positionMs, float64 speed) {
	return AbstractFFMpegLoader::open(positionMs)
		&& openCodecContext()
		&& initUsingContext(_codecContext, speed)
		&& seekTo(positionMs);
}

bool FFMpegLoader::openCodecContext() {
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
	AvErrorWrap error = avcodec_parameters_to_context(
		_codecContext,
		stream->codecpar);
	if (error) {
		LogError(u"avcodec_parameters_to_context"_q, error);
		return false;
	}
	_codecContext->pkt_timebase = stream->time_base;
	av_opt_set_int(_codecContext, "refcounted_frames", 1, 0);

	if (AvErrorWrap error = avcodec_open2(_codecContext, codec, 0)) {
		LogError(u"avcodec_open2"_q, error);
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

FFMpegLoader::ReadResult FFMpegLoader::readMore() {
	if (_readTillEnd) {
		return ReadError::EndOfFile;
	}
	const auto readResult = readFromReadyContext(_codecContext);
	if (readResult != ReadError::Wait) {
		if (readResult == ReadError::EndOfFile) {
			_readTillEnd = true;
		}
		return readResult;
	}

	if (AvErrorWrap error = av_read_frame(fmtContext, &_packet)) {
		if (error.code() != AVERROR_EOF) {
			LogError(u"av_read_frame"_q, error);
			return ReadError::Other;
		}
		error = avcodec_send_packet(_codecContext, nullptr); // drain
		if (!error) {
			return ReadError::Retry;
		}
		LogError(u"avcodec_send_packet"_q, error);
		return ReadError::Other;
	}

	if (_packet.stream_index == streamId) {
		AvErrorWrap error = avcodec_send_packet(_codecContext, &_packet);
		if (error) {
			av_packet_unref(&_packet);
			LogError(u"avcodec_send_packet"_q, error);
			// There is a sample voice message where skipping such packet
			// results in a crash (read_access to nullptr) in swr_convert().
			//if (error.code() == AVERROR_INVALIDDATA) {
			//	return ReadResult::Retry; // try to skip bad packet
			//}
			return ReadError::Other;
		}
	}
	av_packet_unref(&_packet);
	return ReadError::Retry;
}

FFMpegLoader::~FFMpegLoader() {
	if (_codecContext) {
		avcodec_free_context(&_codecContext);
	}
}

} // namespace Media
