/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/round_video_recorder.h"

#include "base/concurrent_timer.h"
#include "base/debug_log.h"
#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "media/audio/media_audio_capture.h"
#include "ui/image/image_prepare.h"
#include "ui/arc_angles.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

constexpr auto kSide = 400;
constexpr auto kUpdateEach = crl::time(100);
constexpr auto kAudioFrequency = 48'000;
constexpr auto kAudioBitRate = 64 * 1024;
constexpr auto kVideoBitRate = 2 * 1024 * 1024;
constexpr auto kMinDuration = crl::time(200);
constexpr auto kMaxDuration = 60 * crl::time(1000);
constexpr auto kInitTimeout = 5 * crl::time(1000);
constexpr auto kBlurredSize = 64;
constexpr auto kMinithumbsPerSecond = 5;
constexpr auto kMinithumbsInRow = 16;
constexpr auto kFadeDuration = crl::time(150);
constexpr auto kSkipFrames = 8;
constexpr auto kMinScale = 0.7;

using namespace FFmpeg;

[[nodiscard]] int MinithumbSize() {
	const auto full = st::historySendSize.height();
	const auto margin = st::historyRecordWaveformBgMargins;
	const auto outer = full - margin.top() - margin.bottom();
	const auto inner = outer - 2 * st::msgWaveformMin;
	return inner * style::DevicePixelRatio();
}

} // namespace

class RoundVideoRecorder::Private final {
public:
	Private(crl::weak_on_queue<Private> weak, int minithumbSize);
	~Private();

	void push(int64 mcstimestamp, const QImage &frame);
	void push(const Media::Capture::Chunk &chunk);

	using Update = Media::Capture::Update;
	[[nodiscard]] rpl::producer<Update, Error> updated() const;

	[[nodiscard]] RoundVideoResult finish();
	void restart(RoundVideoPartial partial);

private:
	static constexpr auto kMaxStreams = 2;

	struct CopyContext {
		CopyContext();

		std::array<int64, kMaxStreams> lastPts = { 0 };
		std::array<int64, kMaxStreams> lastDts = { 0 };
	};

	void initEncoding();
	void initCircleMask();
	void initMinithumbsCanvas();
	void maybeSaveMinithumb(
		not_null<AVFrame*> frame,
		const QImage &original,
		QRect crop);

	bool initVideo();
	bool initAudio();
	void notifyFinished();
	void deinitEncoding();
	void finishEncoding();
	void fail(Error error);
	void timeout();

	void encodeVideoFrame(int64 mcstimestamp, const QImage &frame);
	void encodeAudioFrame(const Media::Capture::Chunk &chunk);
	bool writeFrame(
		const FramePointer &frame,
		const CodecPointer &codec,
		AVStream *stream);

	void updateMaxLevel(const Media::Capture::Chunk &chunk);
	void updateResultDuration(int64 pts, AVRational timeBase);

	void mirrorYUV420P(not_null<AVFrame*> frame);
	void cutCircleFromYUV420P(not_null<AVFrame*> frame);

	[[nodiscard]] RoundVideoResult appendToPrevious(RoundVideoResult video);
	[[nodiscard]] static FormatPointer OpenInputContext(
		not_null<const QByteArray*> data,
		not_null<ReadBytesWrap*> wrap);
	[[nodiscard]] bool copyPackets(
		not_null<AVFormatContext*> input,
		not_null<AVFormatContext*> output,
		CopyContext &context,
		crl::time offset = 0);

	const crl::weak_on_queue<Private> _weak;

	FormatPointer _format;

	AVStream *_videoStream = nullptr;
	CodecPointer _videoCodec;
	FramePointer _videoFrame;
	SwscalePointer _swsContext;
	int64_t _videoPts = 0;

	// This is the first recorded frame timestamp in microseconds.
	int64_t _videoFirstTimestamp = -1;

	// Audio-related members
	AVStream *_audioStream = nullptr;
	CodecPointer _audioCodec;
	FramePointer _audioFrame;
	SwresamplePointer _swrContext;
	QByteArray _audioTail;
	int64_t _audioPts = 0;
	int _audioChannels = 0;

	// Those timestamps are in 'ms' used for sync between audio and video.
	crl::time _firstAudioChunkFinished = 0;
	crl::time _firstVideoFrameTime = 0;

	WriteBytesWrap _result;
	crl::time _resultDuration = 0;
	bool _finished = false;

	ushort _maxLevelSinceLastUpdate = 0;
	crl::time _lastUpdateDuration = 0;
	rpl::event_stream<Update, Error> _updates;

	crl::time _minithumbNextTimestamp = 0;
	const int _minithumbSize = 0;
	int _minithumbsCount = 0;
	QImage _minithumbs;

	crl::time _maxDuration = 0;
	RoundVideoResult _previous;

	ReadBytesWrap _forConcat1, _forConcat2;

	std::vector<bool> _circleMask; // Always nice to use vector<bool>! :D

	base::ConcurrentTimer _timeoutTimer;

};

RoundVideoRecorder::Private::CopyContext::CopyContext() {
	ranges::fill(lastPts, std::numeric_limits<int64>::min());
	ranges::fill(lastDts, std::numeric_limits<int64>::min());
}

RoundVideoRecorder::Private::Private(
	crl::weak_on_queue<Private> weak,
	int minithumbSize)
: _weak(std::move(weak))
, _minithumbSize(minithumbSize)
, _maxDuration(kMaxDuration)
, _timeoutTimer(_weak, [=] { timeout(); }) {
	initEncoding();
	initCircleMask();
	initMinithumbsCanvas();

	_timeoutTimer.callOnce(kInitTimeout);
}

RoundVideoRecorder::Private::~Private() {
	finishEncoding();
}

void RoundVideoRecorder::Private::initEncoding() {
	_format = MakeWriteFormatPointer(
		static_cast<void*>(&_result),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"mp4"_q);

	if (!initVideo()) {
		fail(Error::VideoInit);
		return;
	} else if (!initAudio()) {
		fail(Error::AudioInit);
		return;
	}

	const auto error = AvErrorWrap(avformat_write_header(
		_format.get(),
		nullptr));
	if (error) {
		LogError("avformat_write_header", error);
		fail(Error::Encoding);
	}
}

bool RoundVideoRecorder::Private::initVideo() {
	if (!_format) {
		return false;
	}

	const auto videoCodec = avcodec_find_encoder_by_name("libopenh264");
	if (!videoCodec) {
		LogError("avcodec_find_encoder_by_name", "libopenh264");
		return false;
	}

	_videoStream = avformat_new_stream(_format.get(), videoCodec);
	if (!_videoStream) {
		LogError("avformat_new_stream", "libopenh264");
		return false;
	}

	_videoCodec = CodecPointer(avcodec_alloc_context3(videoCodec));
	if (!_videoCodec) {
		LogError("avcodec_alloc_context3", "libopenh264");
		return false;
	}

	_videoCodec->codec_id = videoCodec->id;
	_videoCodec->codec_type = AVMEDIA_TYPE_VIDEO;
	_videoCodec->width = kSide;
	_videoCodec->height = kSide;
	_videoCodec->time_base = AVRational{ 1, 1'000'000 }; // Microseconds.
	_videoCodec->framerate = AVRational{ 0, 1 }; // Variable frame rate.
	_videoCodec->pix_fmt = AV_PIX_FMT_YUV420P;
	_videoCodec->bit_rate = kVideoBitRate;

	auto error = AvErrorWrap(avcodec_open2(
		_videoCodec.get(),
		videoCodec,
		nullptr));
	if (error) {
		LogError("avcodec_open2", error, "libopenh264");
		return false;
	}

	error = AvErrorWrap(avcodec_parameters_from_context(
		_videoStream->codecpar,
		_videoCodec.get()));
	if (error) {
		LogError("avcodec_parameters_from_context", error, "libopenh264");
		return false;
	}

	_videoFrame = MakeFramePointer();
	if (!_videoFrame) {
		return false;
	}

	_videoFrame->format = _videoCodec->pix_fmt;
	_videoFrame->width = _videoCodec->width;
	_videoFrame->height = _videoCodec->height;

	error = AvErrorWrap(av_frame_get_buffer(_videoFrame.get(), 0));
	if (error) {
		LogError("av_frame_get_buffer", error, "libopenh264");
		return false;
	}

	return true;
}

bool RoundVideoRecorder::Private::initAudio() {
	if (!_format) {
		return false;
	}

	const auto audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!audioCodec) {
		LogError("avcodec_find_encoder", "AAC");
		return false;
	}

	_audioStream = avformat_new_stream(_format.get(), audioCodec);
	if (!_audioStream) {
		LogError("avformat_new_stream", "AAC");
		return false;
	}

	_audioCodec = CodecPointer(avcodec_alloc_context3(audioCodec));
	if (!_audioCodec) {
		LogError("avcodec_alloc_context3", "AAC");
		return false;
	}

	_audioChannels = 1;
	_audioCodec->sample_fmt = AV_SAMPLE_FMT_FLTP;
	_audioCodec->bit_rate = kAudioBitRate;
	_audioCodec->sample_rate = kAudioFrequency;
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	_audioCodec->ch_layout = AV_CHANNEL_LAYOUT_MONO;
#else
	_audioCodec->channel_layout = AV_CH_LAYOUT_MONO;
	_audioCodec->channels = _audioChannels;
#endif

	auto error = AvErrorWrap(avcodec_open2(
		_audioCodec.get(),
		audioCodec,
		nullptr));
	if (error) {
		LogError("avcodec_open2", error, "AAC");
		return false;
	}

	error = AvErrorWrap(avcodec_parameters_from_context(
		_audioStream->codecpar,
		_audioCodec.get()));
	if (error) {
		LogError("avcodec_parameters_from_context", error, "AAC");
		return false;
	}

#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	_swrContext = MakeSwresamplePointer(
		&_audioCodec->ch_layout,
		AV_SAMPLE_FMT_S16,
		_audioCodec->sample_rate,
		&_audioCodec->ch_layout,
		_audioCodec->sample_fmt,
		_audioCodec->sample_rate,
		&_swrContext);
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	_swrContext = MakeSwresamplePointer(
		_audioCodec->channel_layout,
		AV_SAMPLE_FMT_S16,
		_audioCodec->sample_rate,
		_audioCodec->channel_layout,
		_audioCodec->sample_fmt,
		_audioCodec->sample_rate,
		&_swrContext);
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	if (!_swrContext) {
		return false;
	}

	_audioFrame = MakeFramePointer();
	if (!_audioFrame) {
		return false;
	}

	_audioFrame->nb_samples = _audioCodec->frame_size;
	_audioFrame->format = _audioCodec->sample_fmt;
	_audioFrame->sample_rate = _audioCodec->sample_rate;
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	av_channel_layout_copy(&_audioFrame->ch_layout, &_audioCodec->ch_layout);
#else
	_audioFrame->channel_layout = _audioCodec->channel_layout;
	_audioFrame->channels = _audioCodec->channels;
#endif

	error = AvErrorWrap(av_frame_get_buffer(_audioFrame.get(), 0));
	if (error) {
		LogError("av_frame_get_buffer", error, "AAC");
		return false;
	}

	return true;
}

void RoundVideoRecorder::Private::finishEncoding() {
	if (_format
		&& writeFrame(nullptr, _videoCodec, _videoStream)
		&& writeFrame(nullptr, _audioCodec, _audioStream)) {
		const auto error = AvErrorWrap(av_write_trailer(_format.get()));
		if (error) {
			LogError("av_write_trailer", error);
			fail(Error::Encoding);
		}
	}
	deinitEncoding();
}

auto RoundVideoRecorder::Private::updated() const
-> rpl::producer<Update, Error> {
	return _updates.events();
}

RoundVideoResult RoundVideoRecorder::Private::finish() {
	if (!_format) {
		return {};
	}
	finishEncoding();
	auto result = appendToPrevious({
		.content = base::take(_result.content),
		.duration = base::take(_resultDuration),
		//.waveform = {},
		.minithumbs = base::take(_minithumbs),
		.minithumbsCount = base::take(_minithumbsCount),
		.minithumbSize = _minithumbSize,
	});
	if (result.duration < kMinDuration) {
		return {};
	}
	return result;
}

RoundVideoResult RoundVideoRecorder::Private::appendToPrevious(
		RoundVideoResult video) {
	if (!_previous.duration) {
		return video;
	}
	const auto cleanup = gsl::finally([&] {
		_forConcat1 = {};
		_forConcat2 = {};
		deinitEncoding();
	});

	auto input1 = OpenInputContext(&_previous.content, &_forConcat1);
	auto input2 = OpenInputContext(&video.content, &_forConcat2);
	if (!input1 || !input2) {
		return video;
	}

	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&_result),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"mp4"_q);

	for (auto i = 0; i != input1->nb_streams; ++i) {
		AVStream *inStream = input1->streams[i];
		AVStream *outStream = avformat_new_stream(output.get(), nullptr);
		if (!outStream) {
			LogError("avformat_new_stream");
			fail(Error::Encoding);
			return {};
		}
		const auto error = AvErrorWrap(avcodec_parameters_copy(
			outStream->codecpar,
			inStream->codecpar));
		if (error) {
			LogError("avcodec_parameters_copy", error);
			fail(Error::Encoding);
			return {};
		}
		outStream->time_base = inStream->time_base;
	}

	const auto offset = _previous.duration;
	auto context = CopyContext();
	auto error = AvErrorWrap(avformat_write_header(
		output.get(),
		nullptr));
	if (error) {
		LogError("avformat_write_header", error);
		fail(Error::Encoding);
		return {};
	} else if (!copyPackets(input1.get(), output.get(), context)
		|| !copyPackets(input2.get(), output.get(), context, offset)) {
		return {};
	}
	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError("av_write_trailer", error);
		fail(Error::Encoding);
		return {};
	}
	video.content = base::take(_result.content);
	video.duration += _previous.duration;
	return video;
}

FormatPointer RoundVideoRecorder::Private::OpenInputContext(
		not_null<const QByteArray*> data,
		not_null<ReadBytesWrap*> wrap) {
	*wrap = ReadBytesWrap{
		.size = data->size(),
		.data = reinterpret_cast<const uchar*>(data->constData()),
	};
	return MakeFormatPointer(
		wrap.get(),
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
}

bool RoundVideoRecorder::Private::copyPackets(
		not_null<AVFormatContext*> input,
		not_null<AVFormatContext*> output,
		CopyContext &context,
		crl::time offset) {
	AVPacket packet;
	av_init_packet(&packet);

	auto offsets = std::array<int64, kMaxStreams>{ 0 };
	while (av_read_frame(input, &packet) >= 0) {
		const auto index = packet.stream_index;
		Assert(index >= 0 && index < kMaxStreams);
		Assert(index < output->nb_streams);

		if (offset) {
			auto &scaled = offsets[index];
			if (!scaled) {
				scaled = av_rescale_q(
					offset,
					AVRational{ 1, 1000 },
					input->streams[index]->time_base);
			}
			if (packet.pts != AV_NOPTS_VALUE) {
				packet.pts += scaled;
			}
			if (packet.dts != AV_NOPTS_VALUE) {
				packet.dts += scaled;
			}
		}

		if (packet.pts <= context.lastPts[index]) {
			packet.pts = context.lastPts[index] + 1;
		}
		context.lastPts[index] = packet.pts;

		if (packet.dts <= context.lastDts[index]) {
			packet.dts = context.lastDts[index] + 1;
		}
		context.lastDts[index] = packet.dts;

		const auto error = AvErrorWrap(av_interleaved_write_frame(
			output,
			&packet));
		if (error) {
			LogError("av_interleaved_write_frame", error);
			av_packet_unref(&packet);
			return false;
		}
		av_packet_unref(&packet);
	}
	return true;
}

void RoundVideoRecorder::Private::restart(RoundVideoPartial partial) {
	if (_format) {
		return;
	} else if (_maxDuration <= 0) {
		notifyFinished();
		return;
	}
	_previous = std::move(partial.video);
	_minithumbs = std::move(_previous.minithumbs);
	_minithumbsCount = _previous.minithumbsCount;
	Assert(_minithumbSize == _previous.minithumbSize);
	_maxDuration = kMaxDuration - _previous.duration;
	_minithumbNextTimestamp = 0;
	_finished = false;
	initEncoding();
	_timeoutTimer.callOnce(kInitTimeout);
}

void RoundVideoRecorder::Private::fail(Error error) {
	deinitEncoding();
	_updates.fire_error_copy(error);
}

void RoundVideoRecorder::Private::timeout() {
	if (!_firstAudioChunkFinished) {
		fail(Error::AudioTimeout);
	} else if (!_firstVideoFrameTime) {
		fail(Error::VideoTimeout);
	}
}

void RoundVideoRecorder::Private::deinitEncoding() {
	_swsContext = nullptr;
	_videoCodec = nullptr;
	_videoStream = nullptr;
	_videoFrame = nullptr;
	_swrContext = nullptr;
	_audioCodec = nullptr;
	_audioStream = nullptr;
	_audioFrame = nullptr;
	_format = nullptr;

	_videoFirstTimestamp = -1;
	_videoPts = 0;
	_audioTail = QByteArray();
	_audioPts = 0;
	_audioChannels = 0;

	_firstAudioChunkFinished = 0;
	_firstVideoFrameTime = 0;

	_result.offset = 0;

	_maxLevelSinceLastUpdate = 0;
	_lastUpdateDuration = 0;
}

void RoundVideoRecorder::Private::push(
		int64 mcstimestamp,
		const QImage &frame) {
	if (!_format || _finished) {
		return;
	} else if (!_firstAudioChunkFinished) {
		// Skip frames while we didn't start receiving audio.
		return;
	} else if (!_firstVideoFrameTime) {
		_firstVideoFrameTime = crl::now();
	}
	encodeVideoFrame(mcstimestamp, frame);
}

void RoundVideoRecorder::Private::push(const Media::Capture::Chunk &chunk) {
	if (!_format || _finished) {
		return;
	} else if (!_firstAudioChunkFinished || !_firstVideoFrameTime) {
		_firstAudioChunkFinished = chunk.finished;
		return;
	}
	// We get a chunk roughly every 50ms and need to encode it interleaved.
	encodeAudioFrame(chunk);
}

void RoundVideoRecorder::Private::encodeVideoFrame(
		int64 mcstimestamp,
		const QImage &frame) {
	Expects(!_finished);

	if (_videoFirstTimestamp == -1) {
		_videoFirstTimestamp = mcstimestamp;
	}
	const auto fwidth = frame.width();
	const auto fheight = frame.height();
	const auto fmin = std::min(fwidth, fheight);
	const auto fx = (fwidth > fheight) ? (fwidth - fheight) / 2 : 0;
	const auto fy = (fwidth < fheight) ? (fheight - fwidth) / 2 : 0;
	const auto crop = QRect(fx, fy, fmin, fmin);

	_swsContext = MakeSwscalePointer(
		QSize(fmin, fmin),
		AV_PIX_FMT_BGRA,
		QSize(kSide, kSide),
		AV_PIX_FMT_YUV420P,
		&_swsContext);
	if (!_swsContext) {
		fail(Error::Encoding);
		return;
	}

	const auto cdata = frame.constBits()
		+ (frame.bytesPerLine() * fy)
		+ (fx * frame.depth() / 8);

	const uint8_t *srcSlice[1] = { cdata };
	int srcStride[1] = { int(frame.bytesPerLine()) };

	sws_scale(
		_swsContext.get(),
		srcSlice,
		srcStride,
		0,
		fmin,
		_videoFrame->data,
		_videoFrame->linesize);

	mirrorYUV420P(_videoFrame.get());
	cutCircleFromYUV420P(_videoFrame.get());

	_videoFrame->pts = mcstimestamp - _videoFirstTimestamp;
	maybeSaveMinithumb(_videoFrame.get(), frame, crop);
	if (_videoFrame->pts >= _maxDuration * int64(1000)) {
		notifyFinished();
		return;
	} else if (!writeFrame(_videoFrame, _videoCodec, _videoStream)) {
		return;
	}
}

void RoundVideoRecorder::Private::maybeSaveMinithumb(
		not_null<AVFrame*> frame,
		const QImage &original,
		QRect crop) {
	if (frame->pts < _minithumbNextTimestamp * 1000) {
		return;
	}
	_minithumbNextTimestamp += crl::time(1000) / kMinithumbsPerSecond;
	const auto perline = original.bytesPerLine();
	const auto perpixel = original.depth() / 8;
	const auto cropped = QImage(
		original.constBits() + (crop.y() * perline) + (crop.x() * perpixel),
		crop.width(),
		crop.height(),
		perline,
		original.format()
	).scaled(
		_minithumbSize,
		_minithumbSize,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);

	const auto row = _minithumbsCount / kMinithumbsInRow;
	const auto column = _minithumbsCount % kMinithumbsInRow;
	const auto fromPerLine = cropped.bytesPerLine();
	auto from = cropped.constBits();
	const auto toPerLine = _minithumbs.bytesPerLine();
	const auto toPerPixel = _minithumbs.depth() / 8;
	auto to = _minithumbs.bits()
		+ (row * _minithumbSize * toPerLine)
		+ (column * _minithumbSize * toPerPixel);

	Assert(toPerPixel == perpixel);
	for (auto y = 0; y != _minithumbSize; ++y) {
		Assert(to + toPerLine - _minithumbs.constBits()
			<= _minithumbs.bytesPerLine() * _minithumbs.height());
		memcpy(to, from, _minithumbSize * toPerPixel);
		from += fromPerLine;
		to += toPerLine;
	}
	++_minithumbsCount;
}

void RoundVideoRecorder::Private::initCircleMask() {
	const auto width = kSide;
	const auto height = kSide;
	const auto centerX = width / 2;
	const auto centerY = height / 2;
	const auto radius = std::min(centerX, centerY) + 3; // Add some padding.
	const auto radiusSquared = radius * radius;

	_circleMask.resize(width * height);
	auto index = 0;
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != width; ++x) {
			const auto dx = x - centerX;
			const auto dy = y - centerY;
			_circleMask[index++] = (dx * dx + dy * dy > radiusSquared);
		}
	}
}

void RoundVideoRecorder::Private::initMinithumbsCanvas() {
	const auto width = kMinithumbsInRow * _minithumbSize;
	const auto seconds = (kMaxDuration + 999) / 1000;
	const auto persecond = kMinithumbsPerSecond;
	const auto frames = (seconds + persecond - 1) * persecond;
	const auto rows = (frames + kMinithumbsInRow - 1) / kMinithumbsInRow;
	const auto height = rows * _minithumbSize;
	_minithumbs = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
}

void RoundVideoRecorder::Private::mirrorYUV420P(not_null<AVFrame*> frame) {
	for (auto p = 0; p < 3; ++p) {
		const auto size = p ? (kSide / 2) : kSide;
		const auto linesize = _videoFrame->linesize[p];
		auto data = _videoFrame->data[p];
		for (auto y = 0; y != size; ++y) {
			auto left = data + y * linesize;
			auto right = left + size - 1;
			while (left < right) {
				std::swap(*left++, *right--);
			}
		}
	}
}

void RoundVideoRecorder::Private::cutCircleFromYUV420P(
		not_null<AVFrame*> frame) {
	const auto width = frame->width;
	const auto height = frame->height;

	auto yMaskIndex = 0;
	auto yData = frame->data[0];
	const auto ySkip = frame->linesize[0] - width;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (_circleMask[yMaskIndex]) {
				*yData = 255;
			}
			++yData;
			++yMaskIndex;
		}
		yData += ySkip;
	}

	const auto whalf = width / 2;
	const auto hhalf = height / 2;

	auto uvMaskIndex = 0;
	auto uData = frame->data[1];
	auto vData = frame->data[2];
	const auto uSkip = frame->linesize[1] - whalf;
	for (auto y = 0; y != hhalf; ++y) {
		for (auto x = 0; x != whalf; ++x) {
			if (_circleMask[uvMaskIndex]) {
				*uData = 128;
				*vData = 128;
			}
			++uData;
			++vData;
			uvMaskIndex += 2;
		}
		uData += uSkip;
		vData += uSkip;
		uvMaskIndex += width;
	}
}

void RoundVideoRecorder::Private::encodeAudioFrame(
		const Media::Capture::Chunk &chunk) {
	Expects(!_finished);

	updateMaxLevel(chunk);

	if (_audioTail.isEmpty()) {
		_audioTail = chunk.samples;
	} else {
		_audioTail.append(chunk.samples);
	}

	const auto inSamples = int(_audioTail.size() / sizeof(int16_t));
	const auto inData = reinterpret_cast<const uint8_t*>(
		_audioTail.constData());
	auto samplesProcessed = 0;

	while (samplesProcessed + _audioCodec->frame_size <= inSamples) {
		const auto remainingSamples = inSamples - samplesProcessed;
		auto outSamples = int(av_rescale_rnd(
			swr_get_delay(_swrContext.get(), kAudioFrequency) + remainingSamples,
			_audioCodec->sample_rate,
			kAudioFrequency,
			AV_ROUND_UP));

		// Ensure we don't exceed the frame's capacity
		outSamples = std::min(outSamples, _audioCodec->frame_size);

		const auto process = std::min(remainingSamples, outSamples);
		auto dataptr = inData + samplesProcessed * sizeof(int16_t);
		auto error = AvErrorWrap(swr_convert(
			_swrContext.get(),
			_audioFrame->data,
			outSamples,
			&dataptr,
			process));

		if (error) {
			LogError("swr_convert", error);
			fail(Error::Encoding);
			return;
		}

		// Update the actual number of samples in the frame
		_audioFrame->nb_samples = error.code();

		_audioFrame->pts = _audioPts;
		_audioPts += _audioFrame->nb_samples;
		if (_audioPts >= _maxDuration * int64(kAudioFrequency) / 1000) {
			notifyFinished();
			return;
		} else if (!writeFrame(_audioFrame, _audioCodec, _audioStream)) {
			return;
		}

		samplesProcessed += process;
	}
	const auto left = inSamples - samplesProcessed;
	if (left > 0) {
		memmove(_audioTail.data(), _audioTail.data() + samplesProcessed * sizeof(int16_t), left * sizeof(int16_t));
		_audioTail.resize(left * sizeof(int16_t));
	} else {
		_audioTail.clear();
	}
}

void RoundVideoRecorder::Private::notifyFinished() {
	_finished = true;
	_updates.fire({
		.samples = int((_previous.duration + _resultDuration) * 48),
		.level = base::take(_maxLevelSinceLastUpdate),
		.finished = true,
	});
}

bool RoundVideoRecorder::Private::writeFrame(
		const FramePointer &frame,
		const CodecPointer &codec,
		AVStream *stream) {
	_timeoutTimer.cancel();

	if (frame) {
		updateResultDuration(frame->pts, codec->time_base);
	}

	auto error = AvErrorWrap(avcodec_send_frame(codec.get(), frame.get()));
	if (error) {
		LogError("avcodec_send_frame", error);
		fail(Error::Encoding);
		return false;
	}

	auto pkt = av_packet_alloc();
	const auto guard = gsl::finally([&] {
		av_packet_free(&pkt);
	});
	while (true) {
		error = AvErrorWrap(avcodec_receive_packet(codec.get(), pkt));
		if (error.code() == AVERROR(EAGAIN)) {
			return true; // Need more input
		} else if (error.code() == AVERROR_EOF) {
			return true; // Encoding finished
		} else if (error) {
			LogError("avcodec_receive_packet", error);
			fail(Error::Encoding);
			return false;
		}

		pkt->stream_index = stream->index;
		av_packet_rescale_ts(pkt, codec->time_base, stream->time_base);

		updateResultDuration(pkt->pts, stream->time_base);

		error = AvErrorWrap(av_interleaved_write_frame(_format.get(), pkt));
		if (error) {
			LogError("av_interleaved_write_frame", error);
			fail(Error::Encoding);
			return false;
		}
	}

	return true;
}

void RoundVideoRecorder::Private::updateMaxLevel(
		const Media::Capture::Chunk &chunk) {
	const auto &list = chunk.samples;
	const auto samples = int(list.size() / sizeof(ushort));
	const auto data = reinterpret_cast<const ushort*>(list.constData());
	for (const auto value : gsl::make_span(data, samples)) {
		accumulate_max(_maxLevelSinceLastUpdate, value);
	}
}

void RoundVideoRecorder::Private::updateResultDuration(
		int64 pts,
		AVRational timeBase) {
	accumulate_max(_resultDuration, PtsToTimeCeil(pts, timeBase));

	const auto initial = !_lastUpdateDuration;
	if (initial) {
		accumulate_max(_resultDuration, crl::time(1));
	}
	if (initial || (_lastUpdateDuration + kUpdateEach < _resultDuration)) {
		_lastUpdateDuration = _resultDuration;
		_updates.fire({
			.samples = int((_previous.duration + _resultDuration) * 48),
			.level = base::take(_maxLevelSinceLastUpdate),
		});
	}
}

RoundVideoRecorder::RoundVideoRecorder(
	RoundVideoRecorderDescriptor &&descriptor)
: _descriptor(std::move(descriptor))
, _gradientBg(QColor(255, 255, 255, 0))
, _gradientFg(QColor(255, 255, 255, 48))
, _gradient(
	_gradientBg.color(),
	_gradientFg.color(),
	[=] { _preview->update(); })
, _preview(std::make_unique<RpWidget>(_descriptor.container))
, _private(MinithumbSize()) {
	setup();
}

RoundVideoRecorder::~RoundVideoRecorder() = default;

Fn<void(Media::Capture::Chunk)> RoundVideoRecorder::audioChunkProcessor() {
	return [weak = _private.weak()](Media::Capture::Chunk chunk) {
		weak.with([copy = std::move(chunk)](Private &that) {
			that.push(copy);
		});
	};
}

rpl::producer<QImage> RoundVideoRecorder::placeholderUpdates() const {
	return _placeholderUpdates.events();
}

int RoundVideoRecorder::previewSize() const {
	return _side;
}

auto RoundVideoRecorder::updated() -> rpl::producer<Update, Error> {
	return _private.producer_on_main([](const Private &that) {
		return that.updated();
	}) | rpl::before_next(crl::guard(this, [=](const Update &update) {
		const auto progress = (update.samples * crl::time(1000))
			/ float64(kAudioFrequency * kMaxDuration);
		progressTo(progress);
	}));
}

void RoundVideoRecorder::hide(Fn<void(RoundVideoResult)> done) {
	if (const auto onstack = _descriptor.hiding) {
		onstack(this);
	}
	pause(std::move(done));
	fade(false);
}

void RoundVideoRecorder::progressTo(float64 progress) {
	if (_progress == progress || _paused) {
		return;
	} else if (_progressReceived) {
		_progressAnimation.start(
			[=] { _preview->update(); },
			_progress,
			progress,
			kUpdateEach * 1.1);
	} else {
		_progressReceived = true;
		_fadeContentAnimation.start(
			[=] { _preview->update(); },
			0.,
			1.,
			kFadeDuration);
	}
	_progress = progress;
	_preview->update();
}

void RoundVideoRecorder::preparePlaceholder(const QImage &placeholder) {
	const auto ratio = style::DevicePixelRatio();
	const auto full = QSize(_side, _side) * ratio;
	_framePlaceholder = Images::Circle(
		(placeholder.isNull()
			? QImage(u":/gui/art/round_placeholder.jpg"_q)
			: placeholder).scaled(
				full,
				Qt::KeepAspectRatio,
				Qt::SmoothTransformation));
	_framePlaceholder.setDevicePixelRatio(ratio);
}

void RoundVideoRecorder::prepareFrame(bool blurred) {
	if (_frameOriginal.isNull()) {
		return;
	} else if (!blurred) {
		if (_preparedIndex == _lastAddedIndex) {
			return;
		}
		_preparedIndex = _lastAddedIndex;
	}

	const auto owidth = _frameOriginal.width();
	const auto oheight = _frameOriginal.height();
	const auto omin = std::min(owidth, oheight);
	const auto ox = (owidth > oheight) ? (owidth - oheight) / 2 : 0;
	const auto oy = (owidth < oheight) ? (oheight - owidth) / 2 : 0;
	const auto from = QRect(ox, oy, omin, omin);
	const auto bytesPerLine = _frameOriginal.bytesPerLine();
	const auto depth = _frameOriginal.depth() / 8;
	const auto shift = (bytesPerLine * from.y()) + (from.x() * depth);
	auto copy = QImage(
		_frameOriginal.constBits() + shift,
		omin, omin,
		bytesPerLine,
		_frameOriginal.format());

	const auto ratio = style::DevicePixelRatio();
	if (blurred) {
		static constexpr auto kRadius = 16;
		auto image = Images::BlurLargeImage(
			copy.scaled(
				QSize(kBlurredSize, kBlurredSize),
				Qt::KeepAspectRatio,
				Qt::FastTransformation),
			kRadius).mirrored(true, false);
		preparePlaceholder(image);
		_placeholderUpdates.fire(std::move(image));
	} else {
		auto scaled = copy.scaled(
			QSize(_side, _side) * ratio,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation).mirrored(true, false);
		_framePrepared = Images::Circle(std::move(scaled));
		_framePrepared.setDevicePixelRatio(ratio);
	}
}

void RoundVideoRecorder::createImages() {
	const auto ratio = style::DevicePixelRatio();
	preparePlaceholder(_descriptor.placeholder);

	const auto side = _side + 2 * _extent;
	_shadow = QImage(
		QSize(side, side) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_shadow.fill(Qt::transparent);
	_shadow.setDevicePixelRatio(ratio);

	auto sp = QPainter(&_shadow);
	auto shq = PainterHighQualityEnabler(sp);

	QRadialGradient gradient(
		QPointF(_extent + _side / 2, _extent + _side / 2),
		_side / 2 + _extent);
	gradient.setColorAt(0, QColor(0, 0, 0, 128));
	gradient.setColorAt(0.8, QColor(0, 0, 0, 64));
	gradient.setColorAt(1, QColor(0, 0, 0, 0));

	sp.setPen(Qt::NoPen);
	sp.fillRect(0, 0, side, side, gradient);
	sp.end();
}

void RoundVideoRecorder::setup() {
	const auto raw = _preview.get();

	_side = style::ConvertScale(kSide * 3 / 4);
	_progressStroke = st::radialLine;
	_extent = _progressStroke * 8;
	createImages();

	_descriptor.container->sizeValue(
	) | rpl::start_with_next([=](QSize outer) {
		const auto side = _side + 2 * _extent;
		raw->setGeometry(
			style::centerrect(
				QRect(QPoint(), outer),
				QRect(0, 0, side, side)));
	}, raw->lifetime());

	const auto paintPlaceholder = [=](QPainter &p, QRect inner) {
		p.drawImage(inner, _framePlaceholder);
		if (_paused) {
			return;
		}

		_gradient.startFrame(
			0,
			raw->width(),
			raw->width() * 2 / 3);
		_gradient.paint([&](const Ui::PathShiftGradient::Background &b) {
			if (!v::is<QLinearGradient*>(b)) {
				return true;
			}
			auto hq = PainterHighQualityEnabler(p);
			const auto gradient = v::get<QLinearGradient*>(b);

			auto copy = *gradient;
			auto stops = copy.stops();
			for (auto &pair : stops) {
				if (pair.second.alpha() > 0) {
					pair.second.setAlpha(255);
				}
			}
			copy.setStops(stops);

			const auto stroke = style::ConvertScaleExact(1.);
			const auto sub = stroke / 2.;
			p.setPen(QPen(QBrush(copy), stroke));

			p.setBrush(*gradient);
			const auto innerf = QRectF(inner);
			p.drawEllipse(innerf.marginsRemoved({ sub, sub, sub, sub }));
			return true;
		});
	};

	raw->paintRequest() | rpl::start_with_next([=] {
		prepareFrame();

		auto p = QPainter(raw);
		const auto faded = _fadeAnimation.value(_visible ? 1. : 0.);
		if (_fadeAnimation.animating()) {
			p.setOpacity(faded * faded);

			const auto center = raw->rect().center();
			p.translate(center);
			const auto scale = kMinScale + (1. - kMinScale) * faded;
			p.scale(scale, scale);
			p.translate(-center);
		} else if (!_visible) {
			return;
		}

		p.drawImage(raw->rect(), _shadow);
		const auto inner = QRect(_extent, _extent, _side, _side);
		const auto fading = _fadeContentAnimation.animating();
		if (!_progressReceived && !fading) {
			paintPlaceholder(p, inner);
		} else {
			if (fading) {
				paintPlaceholder(p, inner);

				const auto to = _progressReceived ? 1. : 0.;
				p.setOpacity(faded * _fadeContentAnimation.value(to));
			}
			p.drawImage(inner, _framePrepared);

			auto hq = PainterHighQualityEnabler(p);
			p.setPen(QPen(
				Qt::white,
				_progressStroke,
				Qt::SolidLine,
				Qt::RoundCap));
			p.setBrush(Qt::NoBrush);
			const auto add = _progressStroke * 3 / 2.;
			const auto full = arc::kFullLength;
			const auto length = int(base::SafeRound(
				_progressAnimation.value(_progress) * full));
			p.drawArc(
				QRectF(inner).marginsAdded({ add, add, add, add }),
				(full / 4) - length,
				length);
		}

		const auto preview = _fadePreviewAnimation.value(
			_silentPreview ? 1. : 0.);
		const auto frame = _silentPreview
			? lookupPreviewFrame()
			: _cachedPreviewFrame;
		if (preview > 0. && !frame.image.isNull()) {
			p.setOpacity(preview);
			p.drawImage(inner, frame.image);
			if (frame.silent) {
				const auto iconSize = st::historyVideoMessageMuteSize;
				const auto iconRect = style::rtlrect(
					inner.x() + (inner.width() - iconSize) / 2,
					inner.y() + st::msgDateImgDelta,
					iconSize,
					iconSize,
					raw->width());
				p.setPen(Qt::NoPen);
				p.setBrush(st::msgDateImgBg);
				auto hq = PainterHighQualityEnabler(p);
				p.drawEllipse(iconRect);
				st::historyVideoMessageMute.paintInCenter(p, iconRect);
			}
		}
	}, raw->lifetime());

	// Skip some frames, they are sometimes black :(
	_skipFrames = kSkipFrames;
	_descriptor.track->setState(Webrtc::VideoState::Active);

	_descriptor.track->renderNextFrame() | rpl::start_with_next([=] {
		const auto info = _descriptor.track->frameWithInfo(true);
		if (!info.original.isNull() && _lastAddedIndex != info.index) {
			_lastAddedIndex = info.index;
			if (_skipFrames > 0) {
				--_skipFrames;
			} else {
				_frameOriginal = info.original;
				const auto ts = info.mcstimestamp;
				_private.with([copy = info.original, ts](Private &that) {
					that.push(ts, copy);
				});
			}
		}
		_descriptor.track->markFrameShown();
		raw->update();
	}, raw->lifetime());
	_descriptor.track->markFrameShown();

	fade(true);

	raw->show();
	raw->raise();
}

void RoundVideoRecorder::fade(bool visible) {
	if (_visible == visible) {
		return;
	}
	_visible = visible;
	const auto from = visible ? 0. : 1.;
	const auto to = visible ? 1. : 0.;
	_fadeAnimation.start([=] {
		if (_fadeAnimation.animating() || _visible) {
			_preview->update();
		} else {
			_preview->hide();
			if (const auto onstack = _descriptor.hidden) {
				onstack(this);
			}
		}
	}, from, to, kFadeDuration);
}

auto RoundVideoRecorder::lookupPreviewFrame() const -> PreviewFrame {
	auto sounded = _soundedPreview
		? _soundedPreview->image(_side)
		: QImage();
	const auto silent = (_silentPreview && sounded.isNull());
	return {
		.image = silent ? _silentPreview->image(_side) : std::move(sounded),
		.silent = silent,
	};
}

Fn<void()> RoundVideoRecorder::updater() const {
	return [=] {
		_preview->update();
	};
}

void RoundVideoRecorder::pause(Fn<void(RoundVideoResult)> done) {
	if (_paused) {
		return;
	} else if (done) {
		_private.with([done = std::move(done)](Private &that) {
			done(that.finish());
		});
	}
	_paused = true;
	prepareFrame(true);
	_progressReceived = false;
	_fadeContentAnimation.start(updater(), 1., 0., kFadeDuration);
	_descriptor.track->setState(Webrtc::VideoState::Inactive);
	_preview->update();
}

void RoundVideoRecorder::showPreview(
		std::shared_ptr<Ui::DynamicImage> silent,
		std::shared_ptr<Ui::DynamicImage> sounded) {
	_silentPreview = std::move(silent);
	_soundedPreview = std::move(sounded);
	_silentPreview->subscribeToUpdates(updater());
	_soundedPreview->subscribeToUpdates(updater());
	_fadePreviewAnimation.start(updater(), 0., 1., kFadeDuration);
	_preview->update();
}

void RoundVideoRecorder::resume(RoundVideoPartial partial) {
	if (!_paused) {
		return;
	}
	_private.with([partial = std::move(partial)](Private &that) mutable {
		that.restart(std::move(partial));
	});
	_paused = false;
	_cachedPreviewFrame = lookupPreviewFrame();
	if (const auto preview = base::take(_silentPreview)) {
		preview->subscribeToUpdates(nullptr);
	}
	if (const auto preview = base::take(_soundedPreview)) {
		preview->subscribeToUpdates(nullptr);
	}
	if (!_cachedPreviewFrame.image.isNull()) {
		_fadePreviewAnimation.start(updater(), 1., 0., kFadeDuration);
	}
	// Skip some frames, they are sometimes black :(
	_skipFrames = kSkipFrames;
	_descriptor.track->setState(Webrtc::VideoState::Active);
	_preview->update();
}

} // namespace Ui
