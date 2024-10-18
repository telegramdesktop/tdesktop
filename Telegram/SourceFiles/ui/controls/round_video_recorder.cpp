/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/round_video_recorder.h"

#include "base/debug_log.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "media/audio/media_audio_capture.h"
#include "ui/image/image_prepare.h"
#include "ui/arc_angles.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

constexpr auto kSide = 400;
constexpr auto kUpdateEach = crl::time(100);
constexpr auto kAudioFrequency = 48'000;
constexpr auto kAudioBitRate = 32'000;
constexpr auto kVideoBitRate = 3 * 1024 * 1024;
constexpr auto kMaxDuration = 10 * crl::time(1000); AssertIsDebug();

using namespace FFmpeg;

} // namespace

class RoundVideoRecorder::Private final {
public:
	Private(crl::weak_on_queue<Private> weak);
	~Private();

	void push(int64 mcstimestamp, const QImage &frame);
	void push(const Media::Capture::Chunk &chunk);

	using Update = Media::Capture::Update;
	[[nodiscard]] rpl::producer<Update, rpl::empty_error> updated() const;

	[[nodiscard]] RoundVideoResult finish();

private:
	static int Write(void *opaque, uint8_t *buf, int buf_size);
	static int64_t Seek(void *opaque, int64_t offset, int whence);

	int write(uint8_t *buf, int buf_size);
	int64_t seek(int64_t offset, int whence);

	void initEncoding();
	bool initVideo();
	bool initAudio();
	void notifyFinished();
	void deinitEncoding();
	void finishEncoding();
	void fail();

	void encodeVideoFrame(int64 mcstimestamp, const QImage &frame);
	void encodeAudioFrame(const Media::Capture::Chunk &chunk);
	bool writeFrame(
		const FramePointer &frame,
		const CodecPointer &codec,
		AVStream *stream);

	void updateMaxLevel(const Media::Capture::Chunk &chunk);
	void updateResultDuration(int64 pts, AVRational timeBase);

	void cutCircleFromYUV420P(not_null<AVFrame*> frame);
	void initCircleMask();

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

	QByteArray _result;
	int64_t _resultOffset = 0;
	crl::time _resultDuration = 0;
	bool _finished = false;

	ushort _maxLevelSinceLastUpdate = 0;
	crl::time _lastUpdateDuration = 0;
	rpl::event_stream<Update, rpl::empty_error> _updates;

	std::vector<bool> _circleMask; // Always nice to use vector<bool>! :D

};

RoundVideoRecorder::Private::Private(crl::weak_on_queue<Private> weak)
: _weak(std::move(weak)) {
	initEncoding();
	initCircleMask();
}

RoundVideoRecorder::Private::~Private() {
	finishEncoding();
}

int RoundVideoRecorder::Private::Write(void *opaque, uint8_t *buf, int buf_size) {
	return static_cast<Private*>(opaque)->write(buf, buf_size);
}

int64_t RoundVideoRecorder::Private::Seek(void *opaque, int64_t offset, int whence) {
	return static_cast<Private*>(opaque)->seek(offset, whence);
}

int RoundVideoRecorder::Private::write(uint8_t *buf, int buf_size) {
	if (const auto total = _resultOffset + int64(buf_size)) {
		const auto size = int64(_result.size());
		constexpr auto kReserve = 1024 * 1024;
		_result.reserve((total / kReserve) * kReserve);
		const auto overwrite = std::min(
			size - _resultOffset,
			int64(buf_size));
		if (overwrite) {
			memcpy(_result.data() + _resultOffset, buf, overwrite);
		}
		if (const auto append = buf_size - overwrite) {
			_result.append(
				reinterpret_cast<const char*>(buf) + overwrite,
				append);
		}
		_resultOffset += buf_size;
	}
	return buf_size;
}

int64_t RoundVideoRecorder::Private::seek(int64_t offset, int whence) {
	const auto checkedSeek = [&](int64_t offset) {
		if (offset < 0 || offset > int64(_result.size())) {
			return int64(-1);
		}
		return (_resultOffset = offset);
	};
	switch (whence) {
	case SEEK_SET: return checkedSeek(offset);
	case SEEK_CUR: return checkedSeek(_resultOffset + offset);
	case SEEK_END: return checkedSeek(int64(_result.size()) + offset);
	case AVSEEK_SIZE: return int64(_result.size());
	}
	return -1;
}

void RoundVideoRecorder::Private::initEncoding() {
	_format = MakeWriteFormatPointer(
		static_cast<void*>(this),
		nullptr,
		&Private::Write,
		&Private::Seek,
		"mp4"_q);

	if (!initVideo() || !initAudio()) {
		fail();
		return;
	}

	const auto error = AvErrorWrap(avformat_write_header(
		_format.get(),
		nullptr));
	if (error) {
		LogError("avformat_write_header", error);
		fail();
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
	_audioCodec->channels = _audioCodec->ch_layout.nb_channels;
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
		&_audioCodec->ch_layout,
		AV_SAMPLE_FMT_S16,
		_audioCodec->sample_rate,
		&_audioCodec->ch_layout,
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
		av_write_trailer(_format.get());
	}
	deinitEncoding();
}

auto RoundVideoRecorder::Private::updated() const
-> rpl::producer<Update, rpl::empty_error> {
	return _updates.events();
}

RoundVideoResult RoundVideoRecorder::Private::finish() {
	if (!_format) {
		return {};
	}
	finishEncoding();
	return {
		.content = _result,
		.waveform = QByteArray(),
		.duration = _resultDuration,
	};
};

void RoundVideoRecorder::Private::fail() {
	deinitEncoding();
	_updates.fire_error({});
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
	_audioPts = 0;
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
		fail();
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

	cutCircleFromYUV420P(_videoFrame.get());

	_videoFrame->pts = mcstimestamp - _videoFirstTimestamp;
	if (_videoFrame->pts >= kMaxDuration * int64(1000)) {
		notifyFinished();
		return;
	} else if (!writeFrame(_videoFrame, _videoCodec, _videoStream)) {
		return;
	}
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
			fail();
			return;
		}

		// Update the actual number of samples in the frame
		_audioFrame->nb_samples = error.code();

		_audioFrame->pts = _audioPts;
		_audioPts += _audioFrame->nb_samples;
		if (_audioPts >= kMaxDuration * int64(kAudioFrequency) / 1000) {
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
		.samples = int(_resultDuration * 48),
		.level = base::take(_maxLevelSinceLastUpdate),
		.finished = true,
	});
}

bool RoundVideoRecorder::Private::writeFrame(
		const FramePointer &frame,
		const CodecPointer &codec,
		AVStream *stream) {
	if (frame) {
		updateResultDuration(frame->pts, codec->time_base);
	}

	auto error = AvErrorWrap(avcodec_send_frame(codec.get(), frame.get()));
	if (error) {
		LogError("avcodec_send_frame", error);
		fail();
		return false;
	}

	auto pkt = av_packet_alloc();
	const auto guard = gsl::finally([&] {
		av_packet_free(&pkt);
	});
	while (true) {
		error = AvErrorWrap(avcodec_receive_packet(codec.get(), pkt));
		if (error.code() == AVERROR(EAGAIN)) {
			return true;  // Need more input
		} else if (error.code() == AVERROR_EOF) {
			return true;  // Encoding finished
		} else if (error) {
			LogError("avcodec_receive_packet", error);
			fail();
			return false;
		}

		pkt->stream_index = stream->index;
		av_packet_rescale_ts(pkt, codec->time_base, stream->time_base);

		updateResultDuration(pkt->pts, stream->time_base);

		error = AvErrorWrap(av_interleaved_write_frame(_format.get(), pkt));
		if (error) {
			LogError("av_interleaved_write_frame", error);
			fail();
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

	if (_lastUpdateDuration + kUpdateEach >= _resultDuration) {
		_lastUpdateDuration = _resultDuration;
		_updates.fire({
			.samples = int(_resultDuration * 48),
			.level = base::take(_maxLevelSinceLastUpdate),
		});
	}
}

RoundVideoRecorder::RoundVideoRecorder(
	RoundVideoRecorderDescriptor &&descriptor)
: _descriptor(std::move(descriptor))
, _preview(std::make_unique<RpWidget>(_descriptor.container))
, _private() {
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

auto RoundVideoRecorder::updated()
-> rpl::producer<Update, rpl::empty_error> {
	return _private.producer_on_main([](const Private &that) {
		return that.updated();
	}) | rpl::before_next([=](const Update &update) {
		const auto duration = (update.samples * crl::time(1000))
			/ kAudioFrequency;
		progressTo(duration / (1. * kMaxDuration));
	});
}

void RoundVideoRecorder::hide(Fn<void(RoundVideoResult)> done) {
	if (done) {
		_private.with([done = std::move(done)](Private &that) {
			done(that.finish());
		});
	}

	setPaused(true);

	_preview->hide();
	if (const auto onstack = _descriptor.hidden) {
		onstack(this);
	}
}

void RoundVideoRecorder::progressTo(float64 progress) {
	if (_progress == progress) {
		return;
	}
	_progressAnimation.start(
		[=] { _preview->update(); },
		progress,
		_progress,
		kUpdateEach);
	_progress = progress;
	_preview->update();
}

void RoundVideoRecorder::prepareFrame() {
	if (_frameOriginal.isNull() || _preparedIndex == _lastAddedIndex) {
		return;
	}
	_preparedIndex = _lastAddedIndex;

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
	_framePrepared = Images::Circle(copy.scaled(
		_side * ratio,
		_side * ratio,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation));
	_framePrepared.setDevicePixelRatio(ratio);
}

void RoundVideoRecorder::createImages() {
	const auto ratio = style::DevicePixelRatio();
	_framePrepared = QImage(
		QSize(_side, _side) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_framePrepared.fill(Qt::transparent);
	_framePrepared.setDevicePixelRatio(ratio);
	auto p = QPainter(&_framePrepared);
	auto hq = PainterHighQualityEnabler(p);

	p.setPen(Qt::NoPen);
	p.setBrush(Qt::black);
	p.drawEllipse(0, 0, _side, _side);

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

	raw->paintRequest() | rpl::start_with_next([=] {
		prepareFrame();

		auto p = QPainter(raw);
		p.drawImage(raw->rect(), _shadow);
		const auto inner = QRect(_extent, _extent, _side, _side);
		p.drawImage(inner, _framePrepared);
		if (_progress > 0.) {
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
	}, raw->lifetime());

	_descriptor.track->renderNextFrame() | rpl::start_with_next([=] {
		const auto info = _descriptor.track->frameWithInfo(true);
		if (!info.original.isNull() && _lastAddedIndex != info.index) {
			_lastAddedIndex = info.index;
			_frameOriginal = info.original;
			const auto ts = info.mcstimestamp;
			_private.with([copy = info.original, ts](Private &that) {
				that.push(ts, copy);
			});
		}
		_descriptor.track->markFrameShown();
		raw->update();
	}, raw->lifetime());
	_descriptor.track->markFrameShown();

	raw->show();
	raw->raise();
}

void RoundVideoRecorder::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	_descriptor.track->setState(paused
		? Webrtc::VideoState::Inactive
		: Webrtc::VideoState::Active);
	_preview->update();
}


} // namespace Ui
