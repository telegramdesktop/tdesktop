/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/media_video_encode.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_frame_generator.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "lottie/lottie_frame_generator.h"

#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryFile>
#include <QtGui/QPainter>

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/samplefmt.h>
} // extern "C"

namespace Media::Encode {
namespace {

using namespace FFmpeg;

constexpr auto kVideoTimeBase = AVRational{ 1, 1'000'000 };
constexpr auto kMinBitrate = 600'000;
constexpr auto kMaxBitrate = 6'800'000;
constexpr auto kWebmStickerCrf = 28;
constexpr auto kWebmStickerCrfLadder = std::array<std::optional<int>, 6>{
	std::nullopt,
	34,
	40,
	47,
	55,
	63,
};
constexpr auto kMaxSourceSize = 1000 * int64(1024) * 1024;
constexpr auto kMaxTranscodeArea = 4096 * int64(4096);
constexpr auto kAudioFrequency = 48'000;
constexpr auto kAudioBitratePerChannel = 64'000;
constexpr auto kStaleTempTimeout = 24 * 60 * 60;

constexpr auto kMp3InMp4MinFrequency = 16'000;

// MAX_FRAME_RATE in openh264, a faster source overshoots by the ratio.
constexpr auto kRateControlFps = 60.;

// Measured: silence costs this much of the nominal 64 kbps.
constexpr auto kSilentAudioBitrate = 7'000;

// Measured mp4 overhead: fixed boxes plus per-sample tables.
constexpr auto kContainerBytes = 2'000;
constexpr auto kContainerBytesPerVideoFrame = 12;
constexpr auto kContainerBytesPerAudioFrame = 10;
constexpr auto kAudioFrameSamples = 1'024;

// Broken source timestamps would grow silent track without bound.
constexpr auto kMaxSilentAudioFill = crl::time(4 * 60 * 60 * 1000);
constexpr auto kSilentAudioFillMargin = crl::time(1000);

[[nodiscard]] QString TempDirectory() {
	return QDir::tempPath() + u"/tdtranscode"_q;
}

[[nodiscard]] QString TempFileTemplate(const QString &extension) {
	const auto directory = TempDirectory();
	QDir().mkpath(directory);
	QFile::setPermissions(
		directory,
		QFileDevice::ReadUser
			| QFileDevice::WriteUser
			| QFileDevice::ExeUser);
	return directory + u"/XXXXXX."_q + extension;
}

[[nodiscard]] QString TempFileTemplate() {
	return TempFileTemplate(u"mp4"_q);
}

[[nodiscard]] int EvenDown(int value) {
	return value & ~1;
}

[[nodiscard]] float64 SanitizedFps(float64 fps) {
	return (fps > 1. && fps < 121.) ? fps : 30.;
}

[[nodiscard]] int TargetBitrate(QSize size, float64 fps) {
	const auto pixels = int64(size.width()) * size.height();
	const auto bits = float64(pixels) * SanitizedFps(fps) * 0.07;
	return int(std::clamp(bits, float64(kMinBitrate), float64(kMaxBitrate)));
}

void CopyDisplayMatrix(not_null<AVStream*> from, not_null<AVStream*> to) {
	const auto display = av_packet_side_data_get(
		from->codecpar->coded_side_data,
		from->codecpar->nb_coded_side_data,
		AV_PKT_DATA_DISPLAYMATRIX);
	if (!display || !display->size) {
		return;
	}
	const auto copy = av_memdup(display->data, display->size);
	if (!copy) {
		return;
	}
	const auto added = av_packet_side_data_add(
		&to->codecpar->coded_side_data,
		&to->codecpar->nb_coded_side_data,
		AV_PKT_DATA_DISPLAYMATRIX,
		copy,
		display->size,
		0);
	if (!added) {
		av_free(copy);
	}
}

struct FileFormatDeleter {
	void operator()(AVFormatContext *value) {
		if (value) {
			if (value->pb) {
				avio_closep(&value->pb);
			}
			avformat_free_context(value);
		}
	}
};
using FileFormatPointer = std::unique_ptr<AVFormatContext, FileFormatDeleter>;

struct VideoEncoder {
	AVStream *stream = nullptr;
	CodecPointer codec;
};

struct ColorDescription {
	AVColorRange range = AVCOL_RANGE_MPEG;
	AVColorPrimaries primaries = AVCOL_PRI_UNSPECIFIED;
	AVColorTransferCharacteristic transfer = AVCOL_TRC_UNSPECIFIED;
	AVColorSpace space = AVCOL_SPC_UNSPECIFIED;
};

[[nodiscard]] ColorDescription ReadColorDescription(
		not_null<AVCodecParameters*> from,
		bool baked) {
	return {
		.range = (!baked && from->color_range == AVCOL_RANGE_JPEG)
			? AVCOL_RANGE_JPEG
			: AVCOL_RANGE_MPEG,
		.primaries = from->color_primaries,
		.transfer = from->color_trc,
		.space = from->color_space,
	};
}

[[nodiscard]] VideoEncoder CreateVideoEncoder(
		not_null<AVFormatContext*> output,
		not_null<const AVCodec*> encoderCodec,
		QSize size,
		int64 bitrate,
		float64 fps,
		ColorDescription color,
		AVPixelFormat pixelFormat,
		float64 avgFps,
		Fn<bool(not_null<AVCodecContext*>)> setup) {
	const auto stream = avformat_new_stream(output, encoderCodec);
	if (!stream) {
		LogError(u"avformat_new_stream"_q, u"video"_q);
		return {};
	}
	auto encoder = CodecPointer(avcodec_alloc_context3(encoderCodec));
	if (!encoder) {
		LogError(u"avcodec_alloc_context3"_q, u"video"_q);
		return {};
	}
	encoder->codec_id = encoderCodec->id;
	encoder->codec_type = AVMEDIA_TYPE_VIDEO;
	encoder->width = size.width();
	encoder->height = size.height();
	const auto rate = SanitizedFps(fps);
	encoder->time_base = kVideoTimeBase;
	// Without this libopenh264 reads time base and caps at 60 fps.
	encoder->framerate = av_d2q(rate, 1000000);
	encoder->pix_fmt = pixelFormat;
	encoder->bit_rate = bitrate;
	encoder->gop_size = int(base::SafeRound(rate));
	encoder->color_range = color.range;
	encoder->color_primaries = color.primaries;
	encoder->color_trc = color.transfer;
	encoder->colorspace = color.space;
	if (setup && !setup(encoder.get())) {
		return {};
	}
	if (output->oformat->flags & AVFMT_GLOBALHEADER) {
		encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	auto error = AvErrorWrap(avcodec_open2(
		encoder.get(),
		encoderCodec,
		nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error, u"video"_q);
		return {};
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		stream->codecpar,
		encoder.get()));
	if (error) {
		LogError(u"avcodec_parameters_from_context"_q, error);
		return {};
	}
	stream->time_base = encoder->time_base;
	if (avgFps > 0.) {
		stream->avg_frame_rate = av_d2q(avgFps, 100'000);
	}
	return { stream, std::move(encoder) };
}

[[nodiscard]] VideoEncoder CreateH264Encoder(
		not_null<AVFormatContext*> output,
		QSize size,
		int64 bitrate,
		float64 fps,
		ColorDescription color) {
	auto encoderCodec = avcodec_find_encoder_by_name("libopenh264");
	if (!encoderCodec) {
		encoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!encoderCodec) {
			LogError(u"avcodec_find_encoder"_q, u"H264"_q);
			return {};
		}
	}
	return CreateVideoEncoder(
		output,
		encoderCodec,
		size,
		bitrate,
		fps,
		color,
		AV_PIX_FMT_YUV420P,
		0.,
		nullptr);
}

[[nodiscard]] bool SetupVp9StickerOptions(
		not_null<AVCodecContext*> encoder,
		int crf) {
	auto error = AvErrorWrap(av_opt_set_int(
		encoder->priv_data,
		"crf",
		crf,
		0));
	if (error) {
		LogError(u"av_opt_set_int"_q, error, u"crf"_q);
		return false;
	}
	error = AvErrorWrap(av_opt_set_int(
		encoder->priv_data,
		"auto-alt-ref",
		0,
		0));
	if (error) {
		LogError(u"av_opt_set_int"_q, error, u"auto-alt-ref"_q);
		return false;
	}
	return true;
}

[[nodiscard]] VideoEncoder CreateVp9WebmEncoder(
		not_null<AVFormatContext*> output,
		QSize size,
		ColorDescription color,
		int crf,
		float64 fps) {
	const auto encoderCodec = avcodec_find_encoder_by_name("libvpx-vp9");
	if (!encoderCodec) {
		LogError(u"avcodec_find_encoder"_q, u"VP9"_q);
		return {};
	}
	return CreateVideoEncoder(
		output,
		encoderCodec,
		size,
		0,
		fps,
		color,
		AV_PIX_FMT_YUVA420P,
		SanitizedFps(fps),
		[crf](not_null<AVCodecContext*> encoder) {
			return SetupVp9StickerOptions(encoder, crf);
		});
}

[[nodiscard]] QString MoveMoovToFront(const QString &sourcePath) {
	if (sourcePath.isEmpty()) {
		return {};
	}
	auto inWrap = ReadFileWrap();
	inWrap.file.setFileName(sourcePath);
	if (!inWrap.file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto input = MakeFormatPointer(
		&inWrap,
		&ReadFileWrap::Read,
		nullptr,
		&ReadFileWrap::Seek);
	if (!input
		|| AvErrorWrap(avformat_find_stream_info(input.get(), nullptr))) {
		return {};
	}

	auto temp = QTemporaryFile(TempFileTemplate());
	if (!temp.open()) {
		return {};
	}
	const auto path = temp.fileName();
	temp.close();
	const auto pathUtf8 = path.toUtf8();

	auto output = (AVFormatContext*)nullptr;
	if (AvErrorWrap(avformat_alloc_output_context2(
			&output,
			nullptr,
			"mp4",
			pathUtf8.constData()))
		|| !output) {
		return {};
	}
	const auto cleanup = gsl::finally([&] {
		if (output->pb) {
			avio_closep(&output->pb);
		}
		avformat_free_context(output);
	});
	for (auto i = 0; i != int(input->nb_streams); ++i) {
		const auto in = input->streams[i];
		const auto out = avformat_new_stream(output, nullptr);
		if (!out
			|| AvErrorWrap(avcodec_parameters_copy(
				out->codecpar,
				in->codecpar))) {
			return {};
		}
		out->codecpar->codec_tag = 0;
		out->time_base = in->time_base;
	}
	if (AvErrorWrap(avio_open(
			&output->pb,
			pathUtf8.constData(),
			AVIO_FLAG_WRITE))) {
		return {};
	}

	auto options = (AVDictionary*)nullptr;
	av_dict_set(&options, "movflags", "faststart", 0);
	const auto header = AvErrorWrap(avformat_write_header(output, &options));
	av_dict_free(&options);
	if (header) {
		return {};
	}

	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] {
		av_packet_free(&packet);
	});
	while (av_read_frame(input.get(), packet) >= 0) {
		const auto unref = gsl::finally([&] {
			av_packet_unref(packet);
		});
		const auto index = packet->stream_index;
		av_packet_rescale_ts(
			packet,
			input->streams[index]->time_base,
			output->streams[index]->time_base);
		packet->pos = -1;
		if (AvErrorWrap(av_interleaved_write_frame(output, packet))) {
			return {};
		}
	}
	if (AvErrorWrap(av_write_trailer(output))) {
		return {};
	}
	avio_closep(&output->pb);

	temp.setAutoRemove(false);
	return path;
}

[[nodiscard]] QByteArray MoveMoovToFrontBytes(const QByteArray &mp4) {
	if (mp4.isEmpty()) {
		return {};
	}
	auto temp = QTemporaryFile(TempFileTemplate());
	if (!temp.open() || temp.write(mp4) != mp4.size()) {
		return {};
	}
	temp.close();
	const auto produced = MoveMoovToFront(temp.fileName());
	if (produced.isEmpty()) {
		return {};
	}
	auto file = QFile(produced);
	auto result = file.open(QIODevice::ReadOnly)
		? file.readAll()
		: QByteArray();
	file.close();
	QFile::remove(produced);
	return result;
}

[[nodiscard]] bool EncodeAndWrite(
		AVCodecContext *encoder,
		AVStream *stream,
		AVFormatContext *format,
		AVFrame *frame) {
	auto sent = AvErrorWrap(avcodec_send_frame(encoder, frame));
	if (sent) {
		LogError(u"avcodec_send_frame"_q, sent);
		return false;
	}
	auto encoded = av_packet_alloc();
	const auto encodedGuard = gsl::finally([&] {
		av_packet_free(&encoded);
	});
	while (true) {
		auto received = AvErrorWrap(avcodec_receive_packet(
			encoder,
			encoded));
		if (received.code() == AVERROR(EAGAIN)
			|| received.code() == AVERROR_EOF) {
			return true;
		} else if (received) {
			LogError(u"avcodec_receive_packet"_q, received);
			return false;
		}
		encoded->stream_index = stream->index;
		av_packet_rescale_ts(
			encoded,
			encoder->time_base,
			stream->time_base);
		auto written = AvErrorWrap(av_interleaved_write_frame(
			format,
			encoded));
		if (written) {
			LogError(u"av_interleaved_write_frame"_q, written);
			return false;
		}
	}
}

struct AudioFifoDeleter {
	void operator()(AVAudioFifo *value) {
		av_audio_fifo_free(value);
	}
};
using AudioFifoPointer = std::unique_ptr<AVAudioFifo, AudioFifoDeleter>;

class AudioTranscoder final {
public:
	[[nodiscard]] bool init(
		not_null<AVFormatContext*> output,
		not_null<AVStream*> inStream);
	[[nodiscard]] bool process(
		not_null<AVFormatContext*> output,
		AVPacket *packet);
	[[nodiscard]] bool finish(not_null<AVFormatContext*> output);

private:
	[[nodiscard]] bool drainDecoder(not_null<AVFormatContext*> output);
	[[nodiscard]] bool pushToFifo(AVFrame *frame);
	[[nodiscard]] bool encodeFromFifo(
		not_null<AVFormatContext*> output,
		bool flushing);

	CodecPointer _decoder;
	CodecPointer _encoder;
	SwresamplePointer _swr;
	AudioFifoPointer _fifo;
	FramePointer _decodedFrame;
	FramePointer _encodeFrame;
	AVStream *_stream = nullptr;
	AVRational _inTimeBase = AVRational{ 0, 1 };
	int64 _pts = 0;
	bool _ptsSeeded = false;

};

bool AudioTranscoder::init(
		not_null<AVFormatContext*> output,
		not_null<AVStream*> inStream) {
	_decoder = MakeCodecPointer({ .stream = inStream });
	if (!_decoder) {
		return false;
	}
	_inTimeBase = inStream->time_base;
	const auto codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec) {
		LogError(u"avcodec_find_encoder"_q, u"AAC"_q);
		return false;
	}
	_stream = avformat_new_stream(output, codec);
	if (!_stream) {
		LogError(u"avformat_new_stream"_q, u"AAC"_q);
		return false;
	}
	_encoder = CodecPointer(avcodec_alloc_context3(codec));
	if (!_encoder) {
		LogError(u"avcodec_alloc_context3"_q, u"AAC"_q);
		return false;
	}
	const auto channels = std::clamp(_decoder->ch_layout.nb_channels, 1, 2);
	av_channel_layout_default(&_encoder->ch_layout, channels);
	_encoder->codec_type = AVMEDIA_TYPE_AUDIO;
	_encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
	_encoder->sample_rate = kAudioFrequency;
	_encoder->time_base = AVRational{ 1, kAudioFrequency };
	_encoder->bit_rate = kAudioBitratePerChannel * channels;
	if (output->oformat->flags & AVFMT_GLOBALHEADER) {
		_encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	auto error = AvErrorWrap(avcodec_open2(_encoder.get(), codec, nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error, u"AAC"_q);
		return false;
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		_stream->codecpar,
		_encoder.get()));
	if (error) {
		LogError(u"avcodec_parameters_from_context"_q, error, u"AAC"_q);
		return false;
	}
	_stream->time_base = _encoder->time_base;
	_fifo = AudioFifoPointer(av_audio_fifo_alloc(
		_encoder->sample_fmt,
		channels,
		_encoder->frame_size * 4));
	_decodedFrame = MakeFramePointer();
	_encodeFrame = MakeFramePointer();
	return _fifo && _decodedFrame && _encodeFrame;
}

bool AudioTranscoder::process(
		not_null<AVFormatContext*> output,
		AVPacket *packet) {
	const auto sent = AvErrorWrap(avcodec_send_packet(
		_decoder.get(),
		packet));
	if (sent) {
		LogError(u"avcodec_send_packet"_q, sent, u"audio"_q);
		return (sent.code() == AVERROR_INVALIDDATA);
	}
	return drainDecoder(output);
}

bool AudioTranscoder::drainDecoder(not_null<AVFormatContext*> output) {
	while (true) {
		const auto got = AvErrorWrap(avcodec_receive_frame(
			_decoder.get(),
			_decodedFrame.get()));
		if (got.code() == AVERROR(EAGAIN) || got.code() == AVERROR_EOF) {
			return true;
		} else if (got) {
			LogError(u"avcodec_receive_frame"_q, got, u"audio"_q);
			return (got.code() == AVERROR_INVALIDDATA);
		}
		_swr = MakeSwresamplePointer(
			&_decodedFrame->ch_layout,
			AVSampleFormat(_decodedFrame->format),
			_decodedFrame->sample_rate,
			&_encoder->ch_layout,
			_encoder->sample_fmt,
			_encoder->sample_rate,
			&_swr);
		if (!_swr) {
			return false;
		}
		if (!_ptsSeeded) {
			_ptsSeeded = true;
			const auto raw = (_decodedFrame->best_effort_timestamp
				!= AV_NOPTS_VALUE)
				? _decodedFrame->best_effort_timestamp
				: _decodedFrame->pts;
			if (raw != AV_NOPTS_VALUE && _inTimeBase.den > 0) {
				_pts = std::max(
					int64(av_rescale_q(
						raw,
						_inTimeBase,
						AVRational{ 1, kAudioFrequency })),
					int64(0));
			}
		}
		if (!pushToFifo(_decodedFrame.get())
			|| !encodeFromFifo(output, false)) {
			return false;
		}
	}
}

bool AudioTranscoder::pushToFifo(AVFrame *frame) {
	const auto in = frame ? frame->nb_samples : 0;
	const auto upper = int(swr_get_out_samples(_swr.get(), in));
	if (upper <= 0) {
		return true;
	}
	auto converted = MakeFramePointer();
	if (!converted) {
		return false;
	}
	converted->nb_samples = upper;
	converted->format = _encoder->sample_fmt;
	converted->sample_rate = _encoder->sample_rate;
	av_channel_layout_copy(&converted->ch_layout, &_encoder->ch_layout);
	const auto error = AvErrorWrap(av_frame_get_buffer(converted.get(), 0));
	if (error) {
		LogError(u"av_frame_get_buffer"_q, error, u"audio"_q);
		return false;
	}
	const auto samples = swr_convert(
		_swr.get(),
		converted->extended_data,
		upper,
		frame ? const_cast<const uint8_t**>(frame->extended_data) : nullptr,
		in);
	if (samples < 0) {
		LogError(u"swr_convert"_q, AvErrorWrap(samples));
		return false;
	} else if (!samples) {
		return true;
	}
	const auto written = av_audio_fifo_write(
		_fifo.get(),
		reinterpret_cast<void**>(converted->extended_data),
		samples);
	if (written < samples) {
		LogError(u"av_audio_fifo_write"_q);
		return false;
	}
	return true;
}

bool AudioTranscoder::encodeFromFifo(
		not_null<AVFormatContext*> output,
		bool flushing) {
	const auto frameSize = _encoder->frame_size;
	while (true) {
		const auto available = av_audio_fifo_size(_fifo.get());
		if (available <= 0 || (!flushing && available < frameSize)) {
			return true;
		}
		const auto take = std::min(available, frameSize);
		av_frame_unref(_encodeFrame.get());
		_encodeFrame->nb_samples = frameSize;
		_encodeFrame->format = _encoder->sample_fmt;
		_encodeFrame->sample_rate = _encoder->sample_rate;
		av_channel_layout_copy(
			&_encodeFrame->ch_layout,
			&_encoder->ch_layout);
		const auto error = AvErrorWrap(av_frame_get_buffer(
			_encodeFrame.get(),
			0));
		if (error) {
			LogError(u"av_frame_get_buffer"_q, error, u"audio"_q);
			return false;
		}
		const auto read = av_audio_fifo_read(
			_fifo.get(),
			reinterpret_cast<void**>(_encodeFrame->extended_data),
			take);
		if (read < take) {
			LogError(u"av_audio_fifo_read"_q);
			return false;
		}
		if (take < frameSize) {
			av_samples_set_silence(
				_encodeFrame->extended_data,
				take,
				frameSize - take,
				_encoder->ch_layout.nb_channels,
				_encoder->sample_fmt);
		}
		_encodeFrame->pts = _pts;
		_pts += take;
		if (!EncodeAndWrite(
				_encoder.get(),
				_stream,
				output,
				_encodeFrame.get())) {
			return false;
		}
	}
}

bool AudioTranscoder::finish(not_null<AVFormatContext*> output) {
	const auto sent = AvErrorWrap(avcodec_send_packet(
		_decoder.get(),
		nullptr));
	if (!sent && !drainDecoder(output)) {
		return false;
	}
	if (_swr && !pushToFifo(nullptr)) {
		return false;
	}
	if (!encodeFromFifo(output, true)) {
		return false;
	}
	return EncodeAndWrite(_encoder.get(), _stream, output, nullptr);
}

class SilentAudioWriter final {
public:
	[[nodiscard]] bool init(
		not_null<AVFormatContext*> output,
		crl::time limit);
	[[nodiscard]] bool writeUntil(
		not_null<AVFormatContext*> output,
		crl::time position);
	[[nodiscard]] bool finish(not_null<AVFormatContext*> output);

private:
	CodecPointer _encoder;
	FramePointer _frame;
	AVStream *_stream = nullptr;
	crl::time _limit = 0;
	int64 _pts = 0;

};

bool SilentAudioWriter::init(
		not_null<AVFormatContext*> output,
		crl::time limit) {
	_limit = limit;
	const auto codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec) {
		LogError(u"avcodec_find_encoder"_q, u"AAC"_q);
		return false;
	}
	_stream = avformat_new_stream(output, codec);
	if (!_stream) {
		LogError(u"avformat_new_stream"_q, u"silent"_q);
		return false;
	}
	_encoder = CodecPointer(avcodec_alloc_context3(codec));
	if (!_encoder) {
		LogError(u"avcodec_alloc_context3"_q, u"silent"_q);
		return false;
	}
	av_channel_layout_default(&_encoder->ch_layout, 1);
	_encoder->codec_type = AVMEDIA_TYPE_AUDIO;
	_encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
	_encoder->sample_rate = kAudioFrequency;
	_encoder->time_base = AVRational{ 1, kAudioFrequency };
	_encoder->bit_rate = kAudioBitratePerChannel;
	if (output->oformat->flags & AVFMT_GLOBALHEADER) {
		_encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	auto error = AvErrorWrap(avcodec_open2(_encoder.get(), codec, nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error, u"silent"_q);
		return false;
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		_stream->codecpar,
		_encoder.get()));
	if (error) {
		LogError(u"avcodec_parameters_from_context"_q, error, u"silent"_q);
		return false;
	}
	_stream->time_base = _encoder->time_base;
	_frame = MakeFramePointer();
	if (!_frame) {
		return false;
	}
	_frame->nb_samples = _encoder->frame_size;
	_frame->format = _encoder->sample_fmt;
	_frame->sample_rate = _encoder->sample_rate;
	av_channel_layout_copy(&_frame->ch_layout, &_encoder->ch_layout);
	error = AvErrorWrap(av_frame_get_buffer(_frame.get(), 0));
	if (error) {
		LogError(u"av_frame_get_buffer"_q, error, u"silent"_q);
		return false;
	}
	return true;
}

bool SilentAudioWriter::writeUntil(
		not_null<AVFormatContext*> output,
		crl::time position) {
	const auto samples = av_rescale_q(
		std::clamp(position, crl::time(0), _limit),
		AVRational{ 1, 1000 },
		_encoder->time_base);
	const auto size = _encoder->frame_size;
	while (_pts < samples) {
		auto writable = AvErrorWrap(av_frame_make_writable(_frame.get()));
		if (writable) {
			LogError(u"av_frame_make_writable"_q, writable, u"silent"_q);
			return false;
		}
		av_samples_set_silence(
			_frame->extended_data,
			0,
			size,
			_encoder->ch_layout.nb_channels,
			_encoder->sample_fmt);
		_frame->pts = _pts;
		if (!EncodeAndWrite(_encoder.get(), _stream, output, _frame.get())) {
			return false;
		}
		_pts += size;
	}
	return true;
}

bool SilentAudioWriter::finish(not_null<AVFormatContext*> output) {
	return EncodeAndWrite(_encoder.get(), _stream, output, nullptr);
}

class EntityPlayer final {
public:
	explicit EntityPlayer(const AnimatedEntity &entity)
	: _size(entity.geometry.size().toSize()) {
		if (_size.isEmpty() || entity.bytes.isEmpty()) {
			return;
		}
		if (entity.kind == AnimatedEntity::Kind::Lottie) {
			_generator = std::make_unique<Lottie::FrameGenerator>(
				entity.bytes);
		} else {
			_generator = std::make_unique<FFmpeg::FrameGenerator>(
				entity.bytes);
		}
	}

	[[nodiscard]] QImage frameAt(crl::time position) {
		while (_generator && (_covered <= position)) {
			auto frame = _generator->renderNext(std::move(_storage), _size);
			if (frame.image.isNull()) {
				if (std::exchange(_restarted, true)) {
					_generator = nullptr;
					break;
				}
				_generator->jumpToStart();
				continue;
			}
			_restarted = false;
			_storage = std::move(_current);
			_current = std::move(frame.image);
			_covered += std::max(frame.duration, crl::time(1));
			if (frame.last) {
				_generator->jumpToStart();
			}
		}
		return _current;
	}

private:
	std::unique_ptr<Ui::FrameGenerator> _generator;
	QImage _current;
	QImage _storage;
	QSize _size;
	crl::time _covered = 0;
	bool _restarted = false;

};

[[nodiscard]] Result EncodeStill(
		StillSource &&still,
		std::vector<Layer> &&overlay,
		int bitrate,
		Fn<bool(float64)> progress) {
	if (still.base.isNull() || still.duration <= 0) {
		return {};
	}
	const auto fps = std::clamp(still.fps, 1., 60.);
	const auto target = QSize(
		std::max(EvenDown(still.base.width()), 2),
		std::max(EvenDown(still.base.height()), 2));
	auto base = still.base.convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	if (base.size() != target) {
		base = base.scaled(
			target,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}
	const auto framesCount = std::max(
		int(base::SafeRound(still.duration * fps / 1000.)),
		1);

	auto result = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&result),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"mp4"_q);
	if (!output) {
		return {};
	}
	auto video = CreateH264Encoder(
		output.get(),
		target,
		bitrate ? int64(bitrate) : int64(TargetBitrate(target, fps)),
		fps,
		ColorDescription());
	if (!video.codec) {
		return {};
	}
	const auto outVideoStream = video.stream;
	auto encoder = std::move(video.codec);

	auto error = AvErrorWrap(avformat_write_header(output.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	auto players = std::vector<std::unique_ptr<EntityPlayer>>();
	players.reserve(overlay.size());
	for (const auto &layer : overlay) {
		const auto entity = std::get_if<AnimatedEntity>(&layer);
		players.push_back(entity
			? std::make_unique<EntityPlayer>(*entity)
			: nullptr);
	}

	auto encodeFrame = MakeFramePointer();
	if (!encodeFrame) {
		return {};
	}
	encodeFrame->format = AV_PIX_FMT_YUV420P;
	encodeFrame->width = target.width();
	encodeFrame->height = target.height();
	error = AvErrorWrap(av_frame_get_buffer(encodeFrame.get(), 0));
	if (error) {
		LogError(u"av_frame_get_buffer"_q, error);
		return {};
	}
	auto canvas = QImage(target, QImage::Format_ARGB32_Premultiplied);
	auto swscale = MakeSwscalePointer(
		target,
		AV_PIX_FMT_BGRA,
		target,
		AV_PIX_FMT_YUV420P,
		nullptr);
	if (!swscale) {
		return {};
	}

	for (auto i = 0; i != framesCount; ++i) {
		const auto position = crl::time(base::SafeRound(i * 1000. / fps));
		canvas.fill(Qt::black);
		{
			auto p = QPainter(&canvas);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(0, 0, base);
			for (auto index = 0; index != int(overlay.size()); ++index) {
				if (const auto &player = players[index]) {
					const auto &entity = std::get<AnimatedEntity>(
						overlay[index]);
					const auto frame = player->frameAt(position);
					if (frame.isNull()) {
						continue;
					}
					p.save();
					const auto center = entity.geometry.center();
					p.translate(center);
					if (entity.rotation != 0.) {
						p.rotate(entity.rotation);
					}
					if (entity.flipped) {
						p.scale(-1., 1.);
					}
					p.translate(-center);
					p.drawImage(entity.geometry, frame);
					p.restore();
				} else {
					p.drawImage(0, 0, std::get<QImage>(overlay[index]));
				}
			}
		}
		error = AvErrorWrap(av_frame_make_writable(encodeFrame.get()));
		if (error) {
			LogError(u"av_frame_make_writable"_q, error);
			return {};
		}
		const uint8_t *srcData[AV_NUM_DATA_POINTERS] = {
			canvas.constBits(),
			nullptr,
		};
		int srcLinesize[AV_NUM_DATA_POINTERS] = {
			int(canvas.bytesPerLine()),
			0,
		};
		sws_scale(
			swscale.get(),
			srcData,
			srcLinesize,
			0,
			target.height(),
			encodeFrame->data,
			encodeFrame->linesize);
		encodeFrame->pts = int64(base::SafeRound(i * 1'000'000. / fps));
		if (!EncodeAndWrite(
				encoder.get(),
				outVideoStream,
				output.get(),
				encodeFrame.get())) {
			return {};
		}
		if (progress && !progress((i + 1) / float64(framesCount))) {
			return {};
		}
	}
	if (!EncodeAndWrite(
			encoder.get(),
			outVideoStream,
			output.get(),
			nullptr)) {
		return {};
	}
	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}
	auto produced = std::move(result.content);
	auto faststart = MoveMoovToFrontBytes(produced);
	return {
		.bytes = (faststart.isEmpty()
			? std::move(produced)
			: std::move(faststart)),
		.dimensions = target,
		.duration = crl::time(base::SafeRound(framesCount * 1000. / fps)),
	};
}

[[nodiscard]] int NormalizeAngle(int angle) {
	auto result = angle % 360;
	if (result < 0) {
		result += 360;
	}
	return (result % 90) ? 0 : result;
}

[[nodiscard]] QSize EvenSize(QSize size) {
	return QSize(
		std::max(EvenDown(size.width()), 2),
		std::max(EvenDown(size.height()), 2));
}

struct GeometryPlan {
	QSize target;
	QRect crop;
	int fileRotation = 0;
	int userRotation = 0;
	bool flipped = false;
	bool bake = false;
};

[[nodiscard]] GeometryPlan PlanDisplayGeometry(
		const VideoSource &source,
		QSize display) {
	auto plan = GeometryPlan();
	plan.userRotation = NormalizeAngle(source.rotation);
	plan.flipped = source.flipped;

	const auto full = QRect(QPoint(), display);
	plan.crop = source.crop.isValid() ? (source.crop & full) : full;
	if (plan.crop.isEmpty()) {
		plan.crop = full;
	}
	plan.bake = (plan.crop != full)
		|| plan.userRotation
		|| plan.flipped
		|| !source.exactSize.isEmpty()
		|| (source.coverPosition >= 0);

	auto oriented = plan.bake ? plan.crop.size() : display;
	if (plan.bake && RotationSwapWidthHeight(plan.userRotation)) {
		oriented.transpose();
	}
	if (!source.exactSize.isEmpty()) {
		plan.target = EvenSize(source.exactSize);
	} else if (source.targetShorterSide > 0) {
		const auto downscaled = DownscaledSize(
			oriented,
			source.targetShorterSide);
		plan.target = downscaled.isEmpty()
			? EvenSize(oriented)
			: downscaled;
	} else {
		plan.target = EvenSize(oriented);
	}
	return plan;
}

[[nodiscard]] GeometryPlan PlanGeometry(
		const VideoSource &source,
		QSize coded,
		int fileRotation) {
	const auto display = TransposeSizeByRotation(coded, fileRotation);
	auto plan = PlanDisplayGeometry(source, display);
	plan.fileRotation = fileRotation;
	if (!plan.bake) {
		// Frames stay coded and the display matrix is copied across, so the
		// encoder target is coded-oriented as well.
		plan.target = TransposeSizeByRotation(plan.target, fileRotation);
	}
	return plan;
}

[[nodiscard]] QRect CodedCrop(QRect crop, int rotation, QSize coded) {
	switch (rotation) {
	case 90: return QRect(
		crop.y(),
		coded.height() - crop.x() - crop.width(),
		crop.height(),
		crop.width());
	case 180: return QRect(
		coded.width() - crop.x() - crop.width(),
		coded.height() - crop.y() - crop.height(),
		crop.width(),
		crop.height());
	case 270: return QRect(
		coded.width() - crop.y() - crop.height(),
		crop.x(),
		crop.height(),
		crop.width());
	}
	return crop;
}

[[nodiscard]] QImage ComposeFrame(
		not_null<AVFrame*> frame,
		const GeometryPlan &plan,
		SwscalePointer &toRgb,
		QImage &storage) {
	const auto coded = QSize(frame->width, frame->height);
	if (coded.isEmpty()) {
		return {};
	}
	if (!GoodStorageForFrame(storage, coded)) {
		storage = CreateFrameStorage(coded);
		if (storage.isNull()) {
			return {};
		}
	}
	auto &rgb = storage;
	const auto srcFormat = (frame->format == AV_PIX_FMT_NONE)
		? AV_PIX_FMT_YUV420P
		: AVPixelFormat(frame->format);
	toRgb = MakeSwscalePointer(
		coded,
		srcFormat,
		coded,
		AV_PIX_FMT_BGRA,
		&toRgb);
	if (!toRgb) {
		return {};
	}
	uint8_t *dstData[AV_NUM_DATA_POINTERS] = { rgb.bits(), nullptr };
	int dstLinesize[AV_NUM_DATA_POINTERS] = { int(rgb.bytesPerLine()), 0 };
	sws_scale(
		toRgb.get(),
		frame->data,
		frame->linesize,
		0,
		frame->height,
		dstData,
		dstLinesize);
	if (srcFormat == AV_PIX_FMT_BGRA || srcFormat == AV_PIX_FMT_YUVA420P) {
		PremultiplyInplace(rgb);
	}

	const auto display = TransposeSizeByRotation(coded, plan.fileRotation);
	auto crop = plan.crop & QRect(QPoint(), display);
	if (crop.isEmpty()) {
		crop = QRect(QPoint(), display);
	}
	const auto source = CodedCrop(crop, plan.fileRotation, coded);
	const auto rotation = NormalizeAngle(
		plan.userRotation + plan.fileRotation);
	const auto oriented = TransposeSizeByRotation(
		crop.size(),
		plan.userRotation);
	auto scaled = oriented;
	if (scaled != plan.target) {
		scaled = oriented.scaled(plan.target, Qt::KeepAspectRatioByExpanding);
		scaled = QSize(
			std::max(scaled.width(), 1),
			std::max(scaled.height(), 1));
	}
	const auto unrotated = TransposeSizeByRotation(scaled, rotation);

	auto image = rgb;
	if (source != QRect(QPoint(), coded)) {
		image = image.copy(source);
	}
	const auto turn = [&] {
		if (rotation) {
			image = image.transformed(QTransform().rotate(rotation));
		}
		if (plan.flipped) {
			image = image.mirrored(true, false);
		}
	};
	// Turning costs a full pass, so it runs on the smaller size.
	if (int64(unrotated.width()) * unrotated.height()
		< int64(source.width()) * source.height()) {
		if (image.size() != unrotated) {
			image = image.scaled(
				unrotated,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		}
		turn();
	} else {
		turn();
		if (image.size() != scaled) {
			image = image.scaled(
				scaled,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		}
	}
	if (image.size() != plan.target) {
		const auto x = (image.width() - plan.target.width()) / 2;
		const auto y = (image.height() - plan.target.height()) / 2;
		image = image.copy(QRect(QPoint(x, y), plan.target));
	}
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	return image;
}

} // namespace

int64 MaxTranscodeSourceSize() {
	return kMaxSourceSize;
}

QSize DownscaledSize(QSize original, int targetShorterSide) {
	const auto width = original.width();
	const auto height = original.height();
	if (width <= 0 || height <= 0 || targetShorterSide <= 0) {
		return QSize();
	}
	const auto shorter = std::min(width, height);
	if (shorter <= targetShorterSide) {
		return QSize();
	}
	const auto scale = targetShorterSide / float64(shorter);
	return QSize(
		std::max(EvenDown(int(base::SafeRound(width * scale))), 2),
		std::max(EvenDown(int(base::SafeRound(height * scale))), 2));
}

SourceInfo ProbeSource(const QString &path) {
	auto result = SourceInfo();
	result.fileSize = QFileInfo(path).size();
	if (result.fileSize <= 0) {
		return result;
	}
	auto wrap = ReadFileWrap();
	wrap.file.setFileName(path);
	if (!wrap.file.open(QIODevice::ReadOnly)) {
		return result;
	}
	auto input = MakeFormatPointer(
		&wrap,
		&ReadFileWrap::Read,
		nullptr,
		&ReadFileWrap::Seek);
	if (!input) {
		return result;
	} else if (AvErrorWrap(avformat_find_stream_info(input.get(), nullptr))) {
		return result;
	}
	const auto videoId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (videoId < 0) {
		return result;
	}
	const auto videoStream = input->streams[videoId];
	const auto videoParameters = videoStream->codecpar;
	result.coded = QSize(videoParameters->width, videoParameters->height);
	if (result.coded.isEmpty()) {
		return result;
	}
	result.rotation = ReadRotationFromMetadata(videoStream);
	result.display = TransposeSizeByRotation(result.coded, result.rotation);
	result.duration = (input->duration > 0)
		? PtsToTime(input->duration, kUniversalTimeBase)
		: crl::time(0);
	const auto guessed = av_guess_frame_rate(
		input.get(),
		videoStream,
		nullptr);
	result.fps = (guessed.num > 0 && guessed.den > 0) ? av_q2d(guessed) : 0.;
	result.videoBitrate = (videoParameters->bit_rate > 0)
		? videoParameters->bit_rate
		: input->bit_rate;
	result.videoRemuxable = (videoParameters->codec_id == AV_CODEC_ID_H264)
		|| (videoParameters->codec_id == AV_CODEC_ID_HEVC);

	const auto audioId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		videoId,
		nullptr,
		0);
	if (audioId >= 0) {
		const auto audioParameters = input->streams[audioId]->codecpar;
		const auto codecId = audioParameters->codec_id;
		result.hasAudio = true;
		result.audioChannels = audioParameters->ch_layout.nb_channels;
		result.audioBitrate = audioParameters->bit_rate;
		result.audioRemuxable = (codecId == AV_CODEC_ID_AAC)
			|| ((codecId == AV_CODEC_ID_MP3)
				&& (audioParameters->sample_rate >= kMp3InMp4MinFrequency));
	}
	return result;
}

int64 EstimateTranscodedSize(
		const VideoSource &source,
		const SourceInfo &info) {
	if (info.empty()) {
		return 0;
	}
	const auto plan = PlanGeometry(source, info.coded, info.rotation);
	const auto target = plan.target;
	if (target.isEmpty()) {
		return 0;
	}
	const auto limiting = (source.fpsLimit > 0.)
		&& (info.fps > source.fpsLimit * 1.05);
	const auto fps = SanitizedFps(limiting ? source.fpsLimit : info.fps);
	const auto total = info.duration;
	const auto trimmed = (source.from > 0)
		|| ((source.till > 0) && (!total || source.till < total));
	const auto span = TranscodedDuration(source, total);
	if (span <= 0) {
		return 0;
	}
	const auto seconds = span / 1000.;
	const auto copyVideo = !plan.bake
		&& !trimmed
		&& !limiting
		&& (target == info.coded)
		&& info.videoRemuxable;
	if (copyVideo && info.videoBitrate <= 0) {
		// Frames are remuxed as they are, so the file barely changes.
		return info.fileSize;
	}
	const auto videoBitrate = [&] {
		if (copyVideo) {
			return float64(info.videoBitrate);
		}
		const auto wanted = int64(TargetBitrate(target, fps));
		// Quality mode never spends much more than the source did.
		const auto bitrate = (info.videoBitrate > 0)
			? std::min(wanted, info.videoBitrate)
			: wanted;
		return bitrate * std::min(kRateControlFps / fps, 1.);
	}();
	const auto silent = !source.removeAudio
		&& !info.hasAudio
		&& source.silentAudio;
	const auto audioBitrate = [&]() -> float64 {
		if (source.removeAudio || !info.hasAudio) {
			return silent ? kSilentAudioBitrate : 0.;
		} else if (info.audioRemuxable && info.audioBitrate > 0) {
			return float64(info.audioBitrate);
		}
		return kAudioBitratePerChannel
			* std::clamp(info.audioChannels, 1, 2);
	}();
	const auto container = kContainerBytes
		+ kContainerBytesPerVideoFrame * seconds * fps
		+ ((audioBitrate > 0. && !silent)
			? (kContainerBytesPerAudioFrame
				* seconds
				* kAudioFrequency
				/ kAudioFrameSamples)
			: 0.);
	const auto bytes = (videoBitrate + audioBitrate) * seconds / 8.
		+ container;
	return std::max(int64(base::SafeRound(bytes)), int64(1));
}

namespace {

struct ReadPlan {
	VideoSource source;
	bool ignoreEditList = false;
};

struct TranscodeAttempt {
	TranscodeResult result;
	// Demuxer restarts timeline for each edit list entry.
	bool timelineRestarted = false;
};

// Range was measured in the edit list timeline, so it is dropped with it.
[[nodiscard]] ReadPlan SampleTablePlan(VideoSource source) {
	source.from = 0;
	source.till = 0;
	return {
		.source = std::move(source),
		.ignoreEditList = true,
	};
}

[[nodiscard]] TranscodeAttempt TranscodeVideoAttempt(
		const ReadPlan &readPlan,
		const Fn<bool(float64)> &progress) {
	const auto &source = readPlan.source;
	const auto sourceSize = !source.bytes.isEmpty()
		? int64(source.bytes.size())
		: QFileInfo(source.path).size();
	if (sourceSize <= 0 || sourceSize >= kMaxSourceSize) {
		return {};
	}

	auto inBytesWrap = ReadBytesWrap{
		.size = int64(source.bytes.size()),
		.data = reinterpret_cast<const uchar*>(source.bytes.constData()),
	};
	auto inFileWrap = ReadFileWrap();
	auto input = FormatPointer();
	if (!source.bytes.isEmpty()) {
		input = MakeFormatPointer(
			&inBytesWrap,
			&ReadBytesWrap::Read,
			nullptr,
			&ReadBytesWrap::Seek,
			{ .ignoreEditList = readPlan.ignoreEditList });
	} else {
		inFileWrap.file.setFileName(source.path);
		if (!inFileWrap.file.open(QIODevice::ReadOnly)) {
			return {};
		}
		input = MakeFormatPointer(
			&inFileWrap,
			&ReadFileWrap::Read,
			nullptr,
			&ReadFileWrap::Seek,
			{ .ignoreEditList = readPlan.ignoreEditList });
	}
	if (!input) {
		return {};
	}

	auto error = AvErrorWrap(avformat_find_stream_info(input.get(), nullptr));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return {};
	}

	const auto videoId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (videoId < 0) {
		return {};
	}
	const auto webm = (source.mode == VideoSource::Mode::WebmSticker);
	const auto audioId = (source.removeAudio || webm)
		? -1
		: av_find_best_stream(
			input.get(),
			AVMEDIA_TYPE_AUDIO,
			-1,
			videoId,
			nullptr,
			0);

	const auto inVideoStream = input->streams[videoId];
	const auto coded = QSize(
		inVideoStream->codecpar->width,
		inVideoStream->codecpar->height);
	if (coded.isEmpty()) {
		return {};
	}
	const auto fileRotation = ReadRotationFromMetadata(inVideoStream);
	const auto plan = PlanGeometry(source, coded, fileRotation);
	const auto target = plan.target;
	if (target.isEmpty()) {
		return {};
	}

	const auto guessed = av_guess_frame_rate(
		input.get(),
		inVideoStream,
		nullptr);
	const auto sourceFps = (guessed.num > 0 && guessed.den > 0)
		? av_q2d(guessed)
		: 0.;
	const auto limiting = (source.fpsLimit > 0.)
		&& (sourceFps > source.fpsLimit * 1.05);
	const auto minStep = limiting
		? crl::time(base::SafeRound(1000. / source.fpsLimit))
		: crl::time(0);
	const auto fps = limiting ? source.fpsLimit : sourceFps;
	const auto totalDuration = (input->duration > 0)
		? PtsToTime(input->duration, kUniversalTimeBase)
		: crl::time(0);

	// Container duration counts audio tail, editor timeline does not.
	const auto shownDuration = (inVideoStream->duration != AV_NOPTS_VALUE)
		? PtsToTime(inVideoStream->duration, inVideoStream->time_base)
		: totalDuration;

	const auto from = std::max(source.from, crl::time(0));
	const auto till = (source.till > from) ? source.till : crl::time(0);
	const auto trimmed = (from > 0)
		|| (till > 0 && (!shownDuration || till < shownDuration));
	const auto span = till
		? (till - from)
		: ((totalDuration > from) ? (totalDuration - from) : crl::time(0));

	// Only the audio track changes, so the frames are remuxed untouched
	// instead of being decoded and encoded again at a quality loss.
	const auto videoCodecId = inVideoStream->codecpar->codec_id;
	const auto copyVideo = !webm
		&& !plan.bake
		&& !trimmed
		&& !limiting
		&& (plan.target == coded)
		&& ((videoCodecId == AV_CODEC_ID_H264)
			|| (videoCodecId == AV_CODEC_ID_HEVC));

	auto decoder = CodecPointer();
	if (!copyVideo) {
		decoder = MakeCodecPointer({
			.stream = inVideoStream,
			.videoMaxArea = kMaxTranscodeArea,
		});
		if (!decoder) {
			return {};
		}
	}

	auto temp = QTemporaryFile(TempFileTemplate(webm ? u"webm"_q : u"mp4"_q));
	if (!temp.open()) {
		return {};
	}
	const auto path = temp.fileName();
	temp.close();
	const auto pathUtf8 = path.toUtf8();
	auto succeeded = false;
	const auto removeOnFailure = gsl::finally([&] {
		if (!succeeded) {
			QFile::remove(path);
		}
	});

	auto rawOutput = (AVFormatContext*)nullptr;
	if (AvErrorWrap(avformat_alloc_output_context2(
			&rawOutput,
			nullptr,
			webm ? "webm" : "mp4",
			pathUtf8.constData()))
		|| !rawOutput) {
		return {};
	}
	auto output = FileFormatPointer(rawOutput);
	if (AvErrorWrap(avio_open(
			&output->pb,
			pathUtf8.constData(),
			AVIO_FLAG_WRITE))) {
		return {};
	}

	const auto sourceBitrate = int64((inVideoStream->codecpar->bit_rate > 0)
		? inVideoStream->codecpar->bit_rate
		: input->bit_rate);
	const auto targetBitrate = int64(TargetBitrate(target, fps));
	const auto bitrate = (!plan.bake && sourceBitrate > 0)
		? std::min(targetBitrate, sourceBitrate)
		: targetBitrate;
	auto outVideoStream = (AVStream*)nullptr;
	auto encoder = CodecPointer();
	if (copyVideo) {
		outVideoStream = avformat_new_stream(output.get(), nullptr);
		if (!outVideoStream) {
			LogError(u"avformat_new_stream"_q, u"video"_q);
			return {};
		}
		error = AvErrorWrap(avcodec_parameters_copy(
			outVideoStream->codecpar,
			inVideoStream->codecpar));
		if (error) {
			LogError(u"avcodec_parameters_copy"_q, error, u"video"_q);
			return {};
		}
		outVideoStream->codecpar->codec_tag = 0;
		outVideoStream->time_base = inVideoStream->time_base;
	} else {
		const auto color = ReadColorDescription(
			inVideoStream->codecpar,
			plan.bake);
		const auto crf = source.webmCrf
			? std::clamp(*source.webmCrf, 0, 63)
			: kWebmStickerCrf;
		auto video = webm
			? CreateVp9WebmEncoder(output.get(), target, color, crf, fps)
			: CreateH264Encoder(output.get(), target, bitrate, fps, color);
		if (!video.codec) {
			return {};
		}
		outVideoStream = video.stream;
		encoder = std::move(video.codec);
		if (!plan.bake) {
			CopyDisplayMatrix(inVideoStream, outVideoStream);
		}
	}
	// The muxer may change the stream time base while writing the header.
	const auto videoTimeBase = [&] {
		return copyVideo ? outVideoStream->time_base : encoder->time_base;
	};

	const auto inAudioStream = (audioId >= 0)
		? input->streams[audioId]
		: nullptr;
	auto outAudioStream = (AVStream*)nullptr;
	auto audioTranscoder = std::optional<AudioTranscoder>();
	if (inAudioStream) {
		const auto codecId = inAudioStream->codecpar->codec_id;
		const auto frequency = inAudioStream->codecpar->sample_rate;
		const auto copyCompatible = (codecId == AV_CODEC_ID_AAC)
			|| ((codecId == AV_CODEC_ID_MP3)
				&& (frequency >= kMp3InMp4MinFrequency));
		if (!copyCompatible) {
			audioTranscoder.emplace();
			if (!audioTranscoder->init(output.get(), inAudioStream)) {
				return {};
			}
		} else {
			outAudioStream = avformat_new_stream(output.get(), nullptr);
			if (!outAudioStream) {
				LogError(u"avformat_new_stream"_q, u"audio"_q);
				return {};
			}
			error = AvErrorWrap(avcodec_parameters_copy(
				outAudioStream->codecpar,
				inAudioStream->codecpar));
			if (error) {
				LogError(u"avcodec_parameters_copy"_q, error);
				return {};
			}
			outAudioStream->codecpar->codec_tag = 0;
			outAudioStream->time_base = inAudioStream->time_base;
		}
	}

	const auto silentLimit = (span > 0)
		? std::min(span + kSilentAudioFillMargin, kMaxSilentAudioFill)
		: kMaxSilentAudioFill;
	auto silentAudio = std::optional<SilentAudioWriter>();
	if (!inAudioStream && source.silentAudio && !webm) {
		silentAudio.emplace();
		if (!silentAudio->init(output.get(), silentLimit)) {
			return {};
		}
	}

	auto muxOptions = (AVDictionary*)nullptr;
	if (!webm) {
		av_dict_set(&muxOptions, "movflags", "+faststart", 0);
	}
	error = AvErrorWrap(avformat_write_header(output.get(), &muxOptions));
	av_dict_free(&muxOptions);
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	auto swscale = SwscalePointer();
	auto toRgb = SwscalePointer();
	auto fromRgb = SwscalePointer();
	auto encodeFrame = MakeFramePointer();
	auto decodedFrame = MakeFramePointer();
	if (!encodeFrame || !decodedFrame) {
		return {};
	}
	encodeFrame->format = webm ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;
	encodeFrame->width = target.width();
	encodeFrame->height = target.height();
	error = AvErrorWrap(av_frame_get_buffer(encodeFrame.get(), 0));
	if (error) {
		LogError(u"av_frame_get_buffer"_q, error);
		return {};
	}

	if (from > 0) {
		const auto seekTarget = TimeToPts(from, inVideoStream->time_base);
		if (av_seek_frame(
				input.get(),
				videoId,
				seekTarget,
				AVSEEK_FLAG_BACKWARD) < 0) {
			av_seek_frame(input.get(), videoId, 0, AVSEEK_FLAG_BACKWARD);
		}
		avcodec_flush_buffers(decoder.get());
	}
	const auto videoOrigin = TimeToPts(from, inVideoStream->time_base);
	const auto audioOrigin = inAudioStream
		? TimeToPts(from, inAudioStream->time_base)
		: int64(0);

	auto lastVideoPts = int64(-1);
	auto lastMuxedDts = int64(AV_NOPTS_VALUE);
	auto lastEmittedOut = crl::time(-1);
	auto emitted = 0;
	auto failed = false;
	auto reachedEnd = false;
	auto cover = QImage();
	auto coverOffset = crl::time(0);
	auto needCover = (source.coverPosition >= 0);
	auto lastComposed = QImage();
	auto lastComposedPts = int64(AV_NOPTS_VALUE);
	auto composeStorage = QImage();

	auto packet = av_packet_alloc();
	const auto packetGuard = gsl::finally([&] {
		av_packet_free(&packet);
	});

	const auto writeEncoded = [&](AVFrame *frame) {
		return EncodeAndWrite(
			encoder.get(),
			outVideoStream,
			output.get(),
			frame);
	};

	const auto drainDecoder = [&] {
		while (true) {
			auto got = AvErrorWrap(avcodec_receive_frame(
				decoder.get(),
				decodedFrame.get()));
			if (got.code() == AVERROR(EAGAIN) || got.code() == AVERROR_EOF) {
				return true;
			} else if (got) {
				LogError(u"avcodec_receive_frame"_q, got);
				failed = true;
				return false;
			}

			const auto raw = (decodedFrame->best_effort_timestamp
				!= AV_NOPTS_VALUE)
				? decodedFrame->best_effort_timestamp
				: decodedFrame->pts;
			const auto position = (raw != AV_NOPTS_VALUE)
				? PtsToTime(raw, inVideoStream->time_base)
				: crl::time(-1);
			if (position >= 0) {
				if (position < from) {
					continue;
				} else if (till && position >= till) {
					reachedEnd = true;
					return true;
				}
			}
			const auto outPosition = (position >= 0)
				? (position - from)
				: crl::time(-1);
			if (minStep > 0
				&& lastEmittedOut >= 0
				&& outPosition >= 0
				&& (outPosition - lastEmittedOut) < (minStep - minStep / 4)) {
				continue;
			}

			auto composed = QImage();
			if (plan.bake) {
				composed = ComposeFrame(
					decodedFrame.get(),
					plan,
					toRgb,
					composeStorage);
				if (composed.isNull()) {
					failed = true;
					return false;
				}
			}
			auto capturedCover = false;
			if (needCover && !composed.isNull()) {
				lastComposed = composed;
				lastComposedPts = raw;
				if (position >= source.coverPosition) {
					cover = composed.copy();
					needCover = false;
					capturedCover = true;
				}
			}

			auto writable = AvErrorWrap(av_frame_make_writable(
				encodeFrame.get()));
			if (writable) {
				LogError(u"av_frame_make_writable"_q, writable);
				failed = true;
				return false;
			}
			if (plan.bake) {
				if (webm) {
					composed = std::move(composed).convertToFormat(
						QImage::Format_ARGB32);
					if (composed.isNull()) {
						failed = true;
						return false;
					}
				}
				fromRgb = MakeSwscalePointer(
					target,
					AV_PIX_FMT_BGRA,
					target,
					AVPixelFormat(encodeFrame->format),
					&fromRgb);
				if (!fromRgb) {
					failed = true;
					return false;
				}
				const uint8_t *srcData[AV_NUM_DATA_POINTERS] = {
					composed.constBits(),
					nullptr,
				};
				int srcLinesize[AV_NUM_DATA_POINTERS] = {
					int(composed.bytesPerLine()),
					0,
				};
				sws_scale(
					fromRgb.get(),
					srcData,
					srcLinesize,
					0,
					target.height(),
					encodeFrame->data,
					encodeFrame->linesize);
			} else {
				swscale = MakeSwscalePointer(
					QSize(decodedFrame->width, decodedFrame->height),
					decodedFrame->format,
					target,
					AVPixelFormat(encodeFrame->format),
					&swscale);
				if (!swscale) {
					failed = true;
					return false;
				}
				sws_scale(
					swscale.get(),
					decodedFrame->data,
					decodedFrame->linesize,
					0,
					decodedFrame->height,
					encodeFrame->data,
					encodeFrame->linesize);
			}

			auto pts = (raw != AV_NOPTS_VALUE)
				? av_rescale_q(
					raw - videoOrigin,
					inVideoStream->time_base,
					encoder->time_base)
				: (lastVideoPts + 1);
			if (pts <= lastVideoPts) {
				pts = lastVideoPts + 1;
			}
			lastVideoPts = pts;
			encodeFrame->pts = pts;
			if (outPosition >= 0) {
				lastEmittedOut = outPosition;
			}
			if (capturedCover) {
				coverOffset = std::max(
					PtsToTime(pts, encoder->time_base),
					crl::time(0));
			}
			++emitted;

			if (!writeEncoded(encodeFrame.get())) {
				failed = true;
				return false;
			}
			if (silentAudio
				&& !silentAudio->writeUntil(
					output.get(),
					PtsToTime(pts, encoder->time_base))) {
				failed = true;
				return false;
			}
			if (progress) {
				const auto done = PtsToTime(pts, encoder->time_base);
				const auto value = (span > 0)
					? std::clamp(done / float64(span), 0., 1.)
					: 0.;
				if (!progress(value)) {
					failed = true;
					return false;
				}
			}
		}
	};

	while (!reachedEnd) {
		auto read = AvErrorWrap(av_read_frame(input.get(), packet));
		if (read.code() == AVERROR_EOF) {
			break;
		} else if (read) {
			LogError(u"av_read_frame"_q, read);
			return {};
		}
		const auto unref = gsl::finally([&] {
			av_packet_unref(packet);
		});

		if (packet->stream_index == videoId) {
			if (copyVideo) {
				av_packet_rescale_ts(
					packet,
					inVideoStream->time_base,
					outVideoStream->time_base);
				packet->stream_index = outVideoStream->index;
				packet->pos = -1;
				if (packet->dts != AV_NOPTS_VALUE) {
					if (lastMuxedDts != AV_NOPTS_VALUE
						&& packet->dts <= lastMuxedDts) {
						return { .timelineRestarted = true };
					}
					lastMuxedDts = packet->dts;
				}
				if (packet->pts != AV_NOPTS_VALUE) {
					lastVideoPts = std::max(
						lastVideoPts,
						int64(packet->pts));
				}
				++emitted;
				auto written = AvErrorWrap(av_interleaved_write_frame(
					output.get(),
					packet));
				if (written) {
					LogError(u"av_interleaved_write_frame"_q, written);
					return {};
				}
				const auto done = PtsToTime(
					lastVideoPts,
					outVideoStream->time_base);
				if (silentAudio
					&& !silentAudio->writeUntil(output.get(), done)) {
					return {};
				}
				if (progress) {
					const auto value = (span > 0)
						? std::clamp(done / float64(span), 0., 1.)
						: 0.;
					if (!progress(value)) {
						return {};
					}
				}
				continue;
			}
			auto sent = AvErrorWrap(avcodec_send_packet(
				decoder.get(),
				packet));
			if (sent) {
				LogError(u"avcodec_send_packet"_q, sent);
				return {};
			}
			if (!drainDecoder()) {
				return {};
			}
		} else if (packet->stream_index == audioId) {
			if (trimmed) {
				const auto stamp = (packet->pts != AV_NOPTS_VALUE)
					? packet->pts
					: packet->dts;
				if (stamp != AV_NOPTS_VALUE) {
					const auto at = PtsToTime(
						stamp,
						inAudioStream->time_base);
					if (at < from || (till && at >= till)) {
						continue;
					}
				}
				if (packet->pts != AV_NOPTS_VALUE) {
					packet->pts = std::max(
						packet->pts - audioOrigin,
						int64(0));
				}
				if (packet->dts != AV_NOPTS_VALUE) {
					packet->dts = std::max(
						packet->dts - audioOrigin,
						int64(0));
				}
			}
			if (audioTranscoder) {
				if (!audioTranscoder->process(output.get(), packet)) {
					return {};
				}
			} else if (outAudioStream) {
				av_packet_rescale_ts(
					packet,
					inAudioStream->time_base,
					outAudioStream->time_base);
				packet->stream_index = outAudioStream->index;
				packet->pos = -1;
				auto written = AvErrorWrap(av_interleaved_write_frame(
					output.get(),
					packet));
				if (written) {
					LogError(u"av_interleaved_write_frame"_q, written);
					return {};
				}
			}
		}
	}

	if (!copyVideo
		&& !reachedEnd
		&& avcodec_send_packet(decoder.get(), nullptr) >= 0) {
		drainDecoder();
	}
	if (failed || !emitted) {
		return {};
	}
	if (!copyVideo && !writeEncoded(nullptr)) {
		return {};
	}
	if (audioTranscoder && !audioTranscoder->finish(output.get())) {
		return {};
	}
	const auto step = crl::time(base::SafeRound(1000. / SanitizedFps(fps)));
	if (silentAudio) {
		const auto until = PtsToTime(lastVideoPts, videoTimeBase()) + step;
		if (!silentAudio->writeUntil(output.get(), until)
			|| !silentAudio->finish(output.get())) {
			return {};
		}
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	avio_closep(&output->pb);

	if (needCover && !lastComposed.isNull()) {
		cover = lastComposed.copy();
		coverOffset = (lastComposedPts != AV_NOPTS_VALUE)
			? std::max(
				PtsToTime(
					lastComposedPts - videoOrigin,
					inVideoStream->time_base),
				crl::time(0))
			: crl::time(0);
	}
	// What a player shows, which differs from the encoder target when the
	// geometry was not baked in.
	const auto shown = plan.bake
		? target
		: TransposeSizeByRotation(target, plan.fileRotation);
	auto result = TranscodeResult{
		.path = path,
		.cover = std::move(cover),
		.dimensions = shown,
		.duration = PtsToTime(lastVideoPts, videoTimeBase()) + step,
	};
	result.coverOffset = coverOffset;
	temp.setAutoRemove(false);
	succeeded = true;
	return { .result = std::move(result) };
}

} // namespace

TranscodeResult TranscodeVideo(
		const VideoSource &source,
		Fn<bool(float64)> progress) {
	auto attempt = TranscodeVideoAttempt({ .source = source }, progress);
	if (attempt.timelineRestarted) {
		attempt = TranscodeVideoAttempt(SampleTablePlan(source), progress);
	}
	return attempt.result;
}

QSize TranscodedSize(const VideoSource &source, QSize displaySize) {
	return displaySize.isEmpty()
		? QSize()
		: PlanDisplayGeometry(source, displaySize).target;
}

crl::time TranscodedDuration(
		const VideoSource &source,
		crl::time duration) {
	const auto from = std::clamp(source.from, crl::time(0), duration);
	const auto till = (source.till > from)
		? std::min(source.till, duration)
		: duration;
	return std::max(till - from, crl::time(0));
}

Result Run(Job &&job, Fn<bool(float64)> progress) {
	return v::match(job.source, [&](VideoSource &data) {
		auto result = Result();
		auto transcoded = TranscodeVideo(data, std::move(progress));
		if (!transcoded.empty()) {
			auto file = QFile(transcoded.path);
			if (file.open(QIODevice::ReadOnly)) {
				result.bytes = file.readAll();
			}
			file.close();
			QFile::remove(transcoded.path);
			result.dimensions = transcoded.dimensions;
			result.duration = transcoded.duration;
		}
		return result;
	}, [&](StillSource &data) {
		return EncodeStill(
			std::move(data),
			std::move(job.overlay),
			job.bitrate,
			std::move(progress));
	});
}

Result RunWebmSticker(
		VideoSource source,
		int64 maxBytes,
		Fn<bool(float64)> progress) {
	Expects(source.mode == VideoSource::Mode::WebmSticker);

	auto result = Result();
	for (const auto crf : kWebmStickerCrfLadder) {
		source.webmCrf = crf;
		result = Run({ .source = source }, progress);
		if (result.empty()
			|| result.bytes.size() <= maxBytes
			|| (progress && !progress(1.))) {
			break;
		}
	}
	return result;
}

void ClearStaleTempFiles() {
	crl::async([] {
		const auto stale = QDateTime::currentDateTime().addSecs(
			-kStaleTempTimeout);
		const auto entries = QDir(TempDirectory()).entryInfoList(
			QDir::Files | QDir::NoDotAndDotDot);
		for (const auto &entry : entries) {
			if (entry.lastModified() < stale) {
				QFile::remove(entry.absoluteFilePath());
			}
		}
	});
}

} // namespace Media::Encode
