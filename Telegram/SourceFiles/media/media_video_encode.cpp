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
constexpr auto kMaxSourceSize = 1000 * int64(1024) * 1024;
constexpr auto kAudioFrequency = 48'000;
constexpr auto kAudioBitratePerChannel = 64'000;
constexpr auto kStaleTempTimeout = 24 * 60 * 60;

constexpr auto kMp3InMp4MinFrequency = 16'000;

[[nodiscard]] QString TempDirectory() {
	return QDir::tempPath() + u"/tdtranscode"_q;
}

[[nodiscard]] QString TempFileTemplate() {
	const auto directory = TempDirectory();
	QDir().mkpath(directory);
	return directory + u"/XXXXXX.mp4"_q;
}

[[nodiscard]] int EvenDown(int value) {
	return value & ~1;
}

[[nodiscard]] int TargetBitrate(QSize size, float64 fps) {
	const auto pixels = int64(size.width()) * size.height();
	const auto useFps = (fps > 1. && fps < 121.) ? fps : 30.;
	const auto bits = float64(pixels) * useFps * 0.07;
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

struct H264Encoder {
	AVStream *stream = nullptr;
	CodecPointer codec;
};

[[nodiscard]] H264Encoder CreateH264Encoder(
		not_null<AVFormatContext*> output,
		QSize size,
		int64 bitrate,
		int gopSize) {
	auto encoderCodec = avcodec_find_encoder_by_name("libopenh264");
	if (!encoderCodec) {
		encoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!encoderCodec) {
			LogError(u"avcodec_find_encoder"_q, u"H264"_q);
			return {};
		}
	}
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
	encoder->time_base = kVideoTimeBase;
	encoder->framerate = AVRational{ 0, 1 };
	encoder->pix_fmt = AV_PIX_FMT_YUV420P;
	encoder->bit_rate = bitrate;
	encoder->gop_size = gopSize;
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
	return { stream, std::move(encoder) };
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
					av_rescale_q(
						raw,
						_inTimeBase,
						AVRational{ 1, kAudioFrequency }),
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
		int(base::SafeRound(fps)));
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

} // namespace

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

int CompressedShorterSide(QSize original, int64 size) {
	const auto shorter = std::min(original.width(), original.height());
	if (shorter <= 0 || size <= 0 || size >= kMaxSourceSize) {
		return 0;
	} else if (shorter >= 1080) {
		return 1080;
	} else if (shorter >= 720) {
		return 720;
	} else if (shorter >= 480) {
		return 480;
	}
	return shorter;
}

QString TranscodeVideoToMp4(
		const QString &sourcePath,
		const QByteArray &sourceContent,
		int targetShorterSide,
		Fn<bool(float64)> progress) {
	const auto sourceSize = !sourceContent.isEmpty()
		? int64(sourceContent.size())
		: QFileInfo(sourcePath).size();
	if (sourceSize <= 0
		|| sourceSize >= kMaxSourceSize
		|| targetShorterSide <= 0) {
		return {};
	}

	auto inBytesWrap = ReadBytesWrap{
		.size = sourceContent.size(),
		.data = reinterpret_cast<const uchar*>(sourceContent.constData()),
	};
	auto inFileWrap = ReadFileWrap();
	auto input = FormatPointer();
	if (!sourceContent.isEmpty()) {
		input = MakeFormatPointer(
			&inBytesWrap,
			&ReadBytesWrap::Read,
			nullptr,
			&ReadBytesWrap::Seek);
	} else {
		inFileWrap.file.setFileName(sourcePath);
		if (!inFileWrap.file.open(QIODevice::ReadOnly)) {
			return {};
		}
		input = MakeFormatPointer(
			&inFileWrap,
			&ReadFileWrap::Read,
			nullptr,
			&ReadFileWrap::Seek);
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
	const auto audioId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		videoId,
		nullptr,
		0);

	const auto inVideoStream = input->streams[videoId];
	const auto original = QSize(
		inVideoStream->codecpar->width,
		inVideoStream->codecpar->height);
	auto target = DownscaledSize(original, targetShorterSide);
	if (target.isEmpty()) {
		target = QSize(
			std::max(EvenDown(original.width()), 2),
			std::max(EvenDown(original.height()), 2));
	}
	const auto guessed = av_guess_frame_rate(
		input.get(),
		inVideoStream,
		nullptr);
	const auto fps = (guessed.num > 0 && guessed.den > 0)
		? av_q2d(guessed)
		: 0.;
	const auto totalDuration = (input->duration > 0)
		? PtsToTime(input->duration, kUniversalTimeBase)
		: crl::time(0);

	auto decoder = MakeCodecPointer({ .stream = inVideoStream });
	if (!decoder) {
		return {};
	}

	auto temp = QTemporaryFile(TempFileTemplate());
	if (!temp.open()) {
		return {};
	}
	const auto path = temp.fileName();
	temp.close();
	const auto pathUtf8 = path.toUtf8();

	auto rawOutput = (AVFormatContext*)nullptr;
	if (AvErrorWrap(avformat_alloc_output_context2(
			&rawOutput,
			nullptr,
			"mp4",
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
	auto video = CreateH264Encoder(
		output.get(),
		target,
		(sourceBitrate > 0)
			? std::min(targetBitrate, sourceBitrate)
			: targetBitrate,
		int(base::SafeRound((fps > 1. && fps < 121.) ? fps : 30.)));
	if (!video.codec) {
		return {};
	}
	const auto outVideoStream = video.stream;
	auto encoder = std::move(video.codec);
	CopyDisplayMatrix(inVideoStream, outVideoStream);

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

	auto muxOptions = (AVDictionary*)nullptr;
	av_dict_set(&muxOptions, "movflags", "+faststart", 0);
	error = AvErrorWrap(avformat_write_header(output.get(), &muxOptions));
	av_dict_free(&muxOptions);
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	auto swscale = SwscalePointer();
	auto encodeFrame = MakeFramePointer();
	auto decodedFrame = MakeFramePointer();
	if (!encodeFrame || !decodedFrame) {
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

	auto lastVideoPts = int64(-1);
	auto failed = false;

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
			swscale = MakeSwscalePointer(
				QSize(decodedFrame->width, decodedFrame->height),
				decodedFrame->format,
				target,
				AV_PIX_FMT_YUV420P,
				&swscale);
			if (!swscale) {
				failed = true;
				return false;
			}
			auto writable = AvErrorWrap(av_frame_make_writable(
				encodeFrame.get()));
			if (writable) {
				LogError(u"av_frame_make_writable"_q, writable);
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

			const auto source = (decodedFrame->best_effort_timestamp
				!= AV_NOPTS_VALUE)
				? decodedFrame->best_effort_timestamp
				: decodedFrame->pts;
			auto pts = (source != AV_NOPTS_VALUE)
				? av_rescale_q(
					source,
					inVideoStream->time_base,
					encoder->time_base)
				: (lastVideoPts + 1);
			if (pts <= lastVideoPts) {
				pts = lastVideoPts + 1;
			}
			lastVideoPts = pts;
			encodeFrame->pts = pts;

			if (!writeEncoded(encodeFrame.get())) {
				failed = true;
				return false;
			}
			if (progress && totalDuration > 0) {
				const auto done = PtsToTime(pts, encoder->time_base);
				const auto value = std::clamp(
					done / float64(totalDuration),
					0.,
					1.);
				if (!progress(value)) {
					failed = true;
					return false;
				}
			}
		}
	};

	while (true) {
		if (output->pb && avio_tell(output->pb) > sourceSize) {
			LogError(u"transcode"_q, u"Output exceeded source size."_q);
			return {};
		}
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

	if (avcodec_send_packet(decoder.get(), nullptr) >= 0) {
		drainDecoder();
	}
	if (failed) {
		return {};
	}
	if (!writeEncoded(nullptr)) {
		return {};
	}
	if (audioTranscoder && !audioTranscoder->finish(output.get())) {
		return {};
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	avio_closep(&output->pb);

	temp.setAutoRemove(false);
	return path;
}

Result Run(Job &&job, Fn<bool(float64)> progress) {
	return v::match(job.source, [&](VideoSource &data) {
		auto result = Result();
		const auto path = TranscodeVideoToMp4(
			QString(),
			data.bytes,
			data.targetShorterSide,
			std::move(progress));
		if (!path.isEmpty()) {
			auto file = QFile(path);
			if (file.open(QIODevice::ReadOnly)) {
				result.bytes = file.readAll();
			}
			file.close();
			QFile::remove(path);
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
