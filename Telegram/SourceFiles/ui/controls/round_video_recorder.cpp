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
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

constexpr auto kSide = 400;
constexpr auto kOutputFilename = "C:\\Tmp\\TestVideo\\output.mp4";

using namespace FFmpeg;

} // namespace

class RoundVideoRecorder::Private final {
public:
	Private(crl::weak_on_queue<Private> weak);
	~Private();

	void push(int64 mcstimestamp, const QImage &frame);
	void push(const Media::Capture::Chunk &chunk);

	[[nodiscard]] RoundVideoResult finish();

private:
	static int Write(void *opaque, uint8_t *buf, int buf_size);
	static int64_t Seek(void *opaque, int64_t offset, int whence);

	int write(uint8_t *buf, int buf_size);
	int64_t seek(int64_t offset, int whence);

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

	void initEncoding();
	bool initVideo();
	bool initAudio();
	void deinitEncoding();
	void finishEncoding();

	void encodeVideoFrame(int64 mcstimestamp, const QImage &frame);
	void encodeAudioFrame(const Media::Capture::Chunk &chunk);
	bool writeFrame(
		const FramePointer &frame,
		const CodecPointer &codec,
		AVStream *stream);

};

RoundVideoRecorder::Private::Private(crl::weak_on_queue<Private> weak)
: _weak(std::move(weak)) {
	initEncoding();
}

RoundVideoRecorder::Private::~Private() {
	finishEncoding();

	QFile file(kOutputFilename);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(_result);
	}
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
		deinitEncoding();
		return;
	}

	const auto error = AvErrorWrap(avformat_write_header(
		_format.get(),
		nullptr));
	if (error) {
		LogError("avformat_write_header", error);
		deinitEncoding();
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
	_videoCodec->bit_rate = 5 * 1024 * 1024; // 5Mbps

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
	_audioCodec->bit_rate = 32000;
	_audioCodec->sample_rate = 48000;
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
	if (!_format) {
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
	if (!_format) {
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
	_swsContext = MakeSwscalePointer(
		QSize(kSide, kSide),
		AV_PIX_FMT_BGRA,
		QSize(kSide, kSide),
		AV_PIX_FMT_YUV420P,
		&_swsContext);
	if (!_swsContext) {
		deinitEncoding();
		return;
	}

	if (_videoFirstTimestamp == -1) {
		_videoFirstTimestamp = mcstimestamp;
	}

	const auto fwidth = frame.width();
	const auto fheight = frame.height();
	const auto fmin = std::min(fwidth, fheight);
	const auto fx = (fwidth > fheight) ? (fwidth - fheight) / 2 : 0;
	const auto fy = (fwidth < fheight) ? (fheight - fwidth) / 2 : 0;
	const auto crop = QRect(fx, fy, fmin, fmin);
	const auto cropped = frame.copy(crop).scaled(
		kSide,
		kSide,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation);

	// Convert QImage to RGB32 format
//	QImage rgbImage = cropped.convertToFormat(QImage::Format_ARGB32);

	// Prepare source data
	const uint8_t *srcSlice[1] = { cropped.constBits() };
	int srcStride[1] = { cropped.bytesPerLine() };

	// Perform the color space conversion
	sws_scale(
		_swsContext.get(),
		srcSlice,
		srcStride,
		0,
		kSide,
		_videoFrame->data,
		_videoFrame->linesize);

	_videoFrame->pts = mcstimestamp - _videoFirstTimestamp;

	LOG(("Audio At: %1").arg(_videoFrame->pts / 1'000'000.));
	if (!writeFrame(_videoFrame, _videoCodec, _videoStream)) {
		return;
	}
}

void RoundVideoRecorder::Private::encodeAudioFrame(const Media::Capture::Chunk &chunk) {
	if (_audioTail.isEmpty()) {
		_audioTail = chunk.samples;
	} else {
		_audioTail.append(chunk.samples);
	}

	const int inSamples = _audioTail.size() / sizeof(int16_t);
	const uint8_t *inData = reinterpret_cast<const uint8_t*>(_audioTail.constData());
	int samplesProcessed = 0;

	while (samplesProcessed + _audioCodec->frame_size <= inSamples) {
		int remainingSamples = inSamples - samplesProcessed;
		int outSamples = av_rescale_rnd(
			swr_get_delay(_swrContext.get(), 48000) + remainingSamples,
			_audioCodec->sample_rate,
			48000,
			AV_ROUND_UP);

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
			deinitEncoding();
			return;
		}

		// Update the actual number of samples in the frame
		_audioFrame->nb_samples = error.code();

		_audioFrame->pts = _audioPts;
		_audioPts += _audioFrame->nb_samples;

		LOG(("Audio At: %1").arg(_audioFrame->pts / 48'000.));
		if (!writeFrame(_audioFrame, _audioCodec, _audioStream)) {
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

bool RoundVideoRecorder::Private::writeFrame(
		const FramePointer &frame,
		const CodecPointer &codec,
		AVStream *stream) {
	auto error = AvErrorWrap(avcodec_send_frame(codec.get(), frame.get()));
	if (error) {
		LogError("avcodec_send_frame", error);
		deinitEncoding();
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
			deinitEncoding();
			return false;
		}

		pkt->stream_index = stream->index;
		av_packet_rescale_ts(pkt, codec->time_base, stream->time_base);

		accumulate_max(
			_resultDuration,
			PtsToTimeCeil(pkt->pts, stream->time_base));

		error = AvErrorWrap(av_interleaved_write_frame(_format.get(), pkt));
		if (error) {
			LogError("av_interleaved_write_frame", error);
			deinitEncoding();
			return false;
		}
	}

	return true;
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

void RoundVideoRecorder::setup() {
	const auto raw = _preview.get();

	const auto side = style::ConvertScale(kSide * 3 / 4);
	_descriptor.container->sizeValue(
	) | rpl::start_with_next([=](QSize outer) {
		raw->setGeometry(
			style::centerrect(
				QRect(QPoint(), outer),
				QRect(0, 0, side, side)));
	}, raw->lifetime());

	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);

		auto info = _descriptor.track->frameWithInfo(true);
		if (!info.original.isNull()) {
			const auto owidth = info.original.width();
			const auto oheight = info.original.height();
			const auto omin = std::min(owidth, oheight);
			const auto ox = (owidth > oheight) ? (owidth - oheight) / 2 : 0;
			const auto oy = (owidth < oheight) ? (oheight - owidth) / 2 : 0;
			const auto from = QRect(ox, oy, omin, omin);
			p.drawImage(QRect(0, 0, side, side), info.original, from);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(0, 0, 0));
			p.drawEllipse(0, 0, side, side);
		}
		_descriptor.track->markFrameShown();
	}, raw->lifetime());

	_descriptor.track->renderNextFrame() | rpl::start_with_next([=] {
		const auto info = _descriptor.track->frameWithInfo(true);
		if (!info.original.isNull() && _lastAddedIndex != info.index) {
			_lastAddedIndex = info.index;
			const auto ts = info.mcstimestamp;
			_private.with([copy = info.original, ts](Private &that) {
				that.push(ts, copy);
			});
		}
		raw->update();
	}, raw->lifetime());

	raw->show();
	raw->raise();
}

void RoundVideoRecorder::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	_preview->update();
}


} // namespace Ui