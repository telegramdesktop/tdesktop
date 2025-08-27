/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio_local_cache.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media::Audio {
namespace {

constexpr auto kMaxDuration = 3 * crl::time(1000);
constexpr auto kMaxStreams = 2;
constexpr auto kFrameSize = 4096;

[[nodiscard]] QByteArray ConvertAndCut(const QByteArray &bytes) {
	using namespace FFmpeg;

	if (bytes.isEmpty()) {
		return {};
	}

	auto wrap = ReadBytesWrap{
		.size = bytes.size(),
		.data = reinterpret_cast<const uchar*>(bytes.constData()),
	};

	auto input = MakeFormatPointer(
		&wrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!input) {
		return {};
	}

	auto error = AvErrorWrap(avformat_find_stream_info(input.get(), 0));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return {};
	}


#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(58, 79, 100)
	auto inCodec = (const AVCodec*)nullptr;
#else
	auto inCodec = (AVCodec*)nullptr;
#endif

	const auto streamId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		-1,
		&inCodec,
		0);
	if (streamId < 0) {
		LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
		return {};
	}

	auto inStream = input->streams[streamId];
	auto inCodecPar = inStream->codecpar;
	auto inCodecContext = CodecPointer(avcodec_alloc_context3(nullptr));
	if (!inCodecContext) {
		return {};
	}

	if (avcodec_parameters_to_context(inCodecContext.get(), inCodecPar) < 0) {
		return {};
	}

	if (avcodec_open2(inCodecContext.get(), inCodec, nullptr) < 0) {
		return {};
	}

	auto result = WriteBytesWrap();
	auto outFormat = MakeWriteFormatPointer(
		static_cast<void*>(&result),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"wav"_q);
	if (!outFormat) {
		return {};
	}

	// Find and open output codec
	auto outCodec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
	if (!outCodec) {
		return {};
	}

	auto outStream = avformat_new_stream(outFormat.get(), outCodec);
	if (!outStream) {
		return {};
	}

	auto outCodecContext = CodecPointer(
		avcodec_alloc_context3(outCodec));
	if (!outCodecContext) {
		return {};
	}

#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	auto mono = AVChannelLayout(AV_CHANNEL_LAYOUT_MONO);
	auto stereo = AVChannelLayout(AV_CHANNEL_LAYOUT_STEREO);
	const auto in = &inCodecContext->ch_layout;
	if (!av_channel_layout_compare(in, &mono)
		|| !av_channel_layout_compare(in, &stereo)) {
		av_channel_layout_copy(&outCodecContext->ch_layout, in);
	} else {
		outCodecContext->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	}
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	const auto in = inCodecContext->channels;
	if (in == 1 || in == 2) {
		outCodecContext->channels = in;
		outCodecContext->channel_layout = inCodecContext->channel_layout;
	} else {
		outCodecContext->channels = 2;
		outCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
	}
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	const auto rate = 44'100;
	outCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
	outCodecContext->time_base = AVRational{ 1, rate };
	outCodecContext->sample_rate = rate;

	error = avcodec_open2(outCodecContext.get(), outCodec, nullptr);
	if (error) {
		LogError("avcodec_open2", error);
		return {};
	}

	error = avcodec_parameters_from_context(
		outStream->codecpar,
		outCodecContext.get());
	if (error) {
		LogError("avcodec_parameters_from_context", error);
		return {};
	}

	error = avformat_write_header(outFormat.get(), nullptr);
	if (error) {
		LogError("avformat_write_header", error);
		return {};
	}

	auto swrContext = MakeSwresamplePointer(
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
		&inCodecContext->ch_layout,
		inCodecContext->sample_fmt,
		inCodecContext->sample_rate,
		&outCodecContext->ch_layout,
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		inCodecContext->channel_layout,
		inCodecContext->sample_fmt,
		inCodecContext->sample_rate,
		outCodecContext->channel_layout,
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		outCodecContext->sample_fmt,
		outCodecContext->sample_rate);
	if (!swrContext) {
		return {};
	}

	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] {
		av_packet_free(&packet);
	});

	auto frame = MakeFramePointer();
	if (!frame) {
		return {};
	}

	auto outFrame = MakeFramePointer();
	if (!outFrame) {
		return {};
	}

	outFrame->nb_samples = kFrameSize;
	outFrame->format = outCodecContext->sample_fmt;
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	av_channel_layout_copy(
		&outFrame->ch_layout,
		&outCodecContext->ch_layout);
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	outFrame->channel_layout = outCodecContext->channel_layout;
	outFrame->channels = outCodecContext->channels;
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	outFrame->sample_rate = outCodecContext->sample_rate;

	error = av_frame_get_buffer(outFrame.get(), 0);
	if (error) {
		LogError("av_frame_get_buffer", error);
		return {};
	}

	auto pts = int64_t(0);
	auto maxPts = int64_t(kMaxDuration) * rate / 1000;
	const auto writeFrame = [&](AVFrame *frame) { // nullptr to flush
		error = avcodec_send_frame(outCodecContext.get(), frame);
		if (error) {
			LogError("avcodec_send_frame", error);
			return error;
		}
		auto pkt = av_packet_alloc();
		const auto guard = gsl::finally([&] {
			av_packet_free(&pkt);
		});
		while (true) {
			error = avcodec_receive_packet(outCodecContext.get(), pkt);
			if (error) {
				if (error.code() != AVERROR(EAGAIN)
					&& error.code() != AVERROR_EOF) {
					LogError("avcodec_receive_packet", error);
				}
				return error;
			}
			pkt->stream_index = outStream->index;
			av_packet_rescale_ts(
				pkt,
				outCodecContext->time_base,
				outStream->time_base);
			error = av_interleaved_write_frame(outFormat.get(), pkt);
			if (error) {
				LogError("av_interleaved_write_frame", error);
				return error;
			}
		}
	};

	while (pts < maxPts) {
		error = av_read_frame(input.get(), packet);
		const auto finished = (error.code() == AVERROR_EOF);
		if (!finished) {
			if (error) {
				LogError("av_read_frame", error);
				return {};
			}
			auto guard = gsl::finally([&] {
				av_packet_unref(packet);
			});
			if (packet->stream_index != streamId) {
				continue;
			}
			error = avcodec_send_packet(inCodecContext.get(), packet);
			if (error) {
				LogError("avcodec_send_packet", error);
				return {};
			}
		}

		while (true) {
			error = avcodec_receive_frame(inCodecContext.get(), frame.get());
			if (error) {
				if (error.code() == AVERROR(EAGAIN)
					|| error.code() == AVERROR_EOF) {
					break;
				} else {
					LogError("avcodec_receive_frame", error);
					return {};
				}
			}
			error = swr_convert(
				swrContext.get(),
				outFrame->data,
				kFrameSize,
				(const uint8_t**)frame->data,
				frame->nb_samples);
			if (error) {
				LogError("swr_convert", error);
				return {};
			}
			const auto samples = error.code();
			if (!samples) {
				continue;
			}

			outFrame->nb_samples = samples;
			outFrame->pts = pts;
			pts += samples;
			if (pts > maxPts) {
				break;
			}

			error = writeFrame(outFrame.get());
			if (error && error.code() != AVERROR(EAGAIN)) {
				return {};
			}
		}

		if (finished) {
			break;
		}
	}
	error = writeFrame(nullptr);
	if (error && error.code() != AVERROR_EOF) {
		return {};
	}
	error = av_write_trailer(outFormat.get());
	if (error) {
		LogError("av_write_trailer", error);
		return {};
	}
	return result.content;
}

} // namespace

LocalSound LocalCache::sound(
		DocumentId id,
		Fn<QByteArray()> resolveOriginalBytes,
		Fn<QByteArray()> fallbackOriginalBytes) {
	auto &result = _cache[id];
	if (!result.isEmpty()) {
		return { id, result };
	}
	result = ConvertAndCut(resolveOriginalBytes());
	return !result.isEmpty()
		? LocalSound{ id, result }
		: fallbackOriginalBytes
		? sound(0, fallbackOriginalBytes, nullptr)
		: LocalSound();
}

LocalDiskCache::LocalDiskCache(const QString &folder)
: _base(folder + '/') {
	QDir().mkpath(_base);
}

QString LocalDiskCache::name(const LocalSound &sound) {
	if (!sound) {
		return {};
	}
	const auto i = _paths.find(sound.id);
	if (i != end(_paths)) {
		return i->second;
	}

	auto result = u"TD_%1"_q.arg(sound.id
		? QString::number(sound.id, 16).toUpper()
		: u"Default"_q);
	const auto path = _base + u"%1.wav"_q.arg(result);

	auto f = QFile(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(sound.wav);
		f.close();
	}

	_paths.emplace(sound.id, result);
	return result;
}

QString LocalDiskCache::path(const LocalSound &sound) {
	const auto part = name(sound);
	return part.isEmpty() ? QString() : _base + part + u".wav"_q;
}

} // namespace Media::Audio
