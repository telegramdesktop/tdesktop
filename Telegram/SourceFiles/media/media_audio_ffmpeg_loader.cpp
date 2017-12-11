/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "media/media_audio_ffmpeg_loader.h"

#include "core/crash_reports.h"

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

bool AbstractFFMpegLoader::open(TimeMs positionMs) {
	if (!AudioPlayerLoader::openFile()) {
		return false;
	}

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	ioBuffer = (uchar*)av_malloc(AVBlockSize);
	if (!_data.isEmpty()) {
		ioContext = avio_alloc_context(ioBuffer, AVBlockSize, 0, reinterpret_cast<void*>(this), &AbstractFFMpegLoader::_read_data, 0, &AbstractFFMpegLoader::_seek_data);
	} else if (!_bytes.empty()) {
		ioContext = avio_alloc_context(ioBuffer, AVBlockSize, 0, reinterpret_cast<void*>(this), &AbstractFFMpegLoader::_read_bytes, 0, &AbstractFFMpegLoader::_seek_bytes);
	} else {
		ioContext = avio_alloc_context(ioBuffer, AVBlockSize, 0, reinterpret_cast<void*>(this), &AbstractFFMpegLoader::_read_file, 0, &AbstractFFMpegLoader::_seek_file);
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

	_samplesFrequency = fmtContext->streams[streamId]->codecpar->sample_rate;
	if (fmtContext->streams[streamId]->duration == AV_NOPTS_VALUE) {
		_samplesCount = (fmtContext->duration * _samplesFrequency) / AV_TIME_BASE;
	} else {
		_samplesCount = (fmtContext->streams[streamId]->duration * _samplesFrequency * fmtContext->streams[streamId]->time_base.num) / fmtContext->streams[streamId]->time_base.den;
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
	auto l = reinterpret_cast<AbstractFFMpegLoader*>(opaque);

	auto nbytes = qMin(l->_data.size() - l->_dataPos, int32(buf_size));
	if (nbytes <= 0) {
		return 0;
	}

	memcpy(buf, l->_data.constData() + l->_dataPos, nbytes);
	l->_dataPos += nbytes;
	return nbytes;
}

int64_t AbstractFFMpegLoader::_seek_data(void *opaque, int64_t offset, int whence) {
	auto l = reinterpret_cast<AbstractFFMpegLoader*>(opaque);

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
	auto l = reinterpret_cast<AbstractFFMpegLoader*>(opaque);

	auto nbytes = qMin(static_cast<int>(l->_bytes.size()) - l->_dataPos, buf_size);
	if (nbytes <= 0) {
		return 0;
	}

	memcpy(buf, l->_bytes.data() + l->_dataPos, nbytes);
	l->_dataPos += nbytes;
	return nbytes;
}

int64_t AbstractFFMpegLoader::_seek_bytes(void *opaque, int64_t offset, int whence) {
	auto l = reinterpret_cast<AbstractFFMpegLoader*>(opaque);

	int32 newPos = -1;
	switch (whence) {
	case SEEK_SET: newPos = offset; break;
	case SEEK_CUR: newPos = l->_dataPos + offset; break;
	case SEEK_END: newPos = static_cast<int>(l->_bytes.size()) + offset; break;
	case AVSEEK_SIZE: {
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
	auto l = reinterpret_cast<AbstractFFMpegLoader*>(opaque);
	return int(l->_f.read((char*)(buf), buf_size));
}

int64_t AbstractFFMpegLoader::_seek_file(void *opaque, int64_t offset, int whence) {
	auto l = reinterpret_cast<AbstractFFMpegLoader*>(opaque);

	switch (whence) {
	case SEEK_SET: return l->_f.seek(offset) ? l->_f.pos() : -1;
	case SEEK_CUR: return l->_f.seek(l->_f.pos() + offset) ? l->_f.pos() : -1;
	case SEEK_END: return l->_f.seek(l->_f.size() + offset) ? l->_f.pos() : -1;
	case AVSEEK_SIZE: {
		// Special whence for determining filesize without any seek.
		return l->_f.size();
	} break;
	}
	return -1;
}

FFMpegLoader::FFMpegLoader(const FileLocation &file, const QByteArray &data, base::byte_vector &&bytes) : AbstractFFMpegLoader(file, data, std::move(bytes)) {
	frame = av_frame_alloc();
}

bool FFMpegLoader::open(TimeMs positionMs) {
	if (!AbstractFFMpegLoader::open(positionMs)) {
		return false;
	}

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	auto codecParams = fmtContext->streams[streamId]->codecpar;

	codecContext = avcodec_alloc_context3(nullptr);
	if (!codecContext) {
		LOG(("Audio Error: Unable to avcodec_alloc_context3 for file '%1', data size '%2'").arg(_file.name()).arg(_data.size()));
		return false;
	}
	if ((res = avcodec_parameters_to_context(codecContext, codecParams)) < 0) {
		LOG(("Audio Error: Unable to avcodec_parameters_to_context for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}
	av_codec_set_pkt_timebase(codecContext, fmtContext->streams[streamId]->time_base);
	av_opt_set_int(codecContext, "refcounted_frames", 1, 0);

	if ((res = avcodec_open2(codecContext, codec, 0)) < 0) {
		LOG(("Audio Error: Unable to avcodec_open2 for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	auto layout = codecParams->channel_layout;
	if (!layout) {
		switch (codecParams->channels) {
		case 1: layout = AV_CH_LAYOUT_MONO; break;
		case 2: layout = AV_CH_LAYOUT_STEREO; break;
		default:
			LOG(("Audio Error: Unknown channel layout for %1 channels.").arg(codecParams->channels));
			return false;
		break;
		}
	}
	inputFormat = codecContext->sample_fmt;
	switch (layout) {
	case AV_CH_LAYOUT_MONO:
		switch (inputFormat) {
		case AV_SAMPLE_FMT_U8:
		case AV_SAMPLE_FMT_U8P: fmt = AL_FORMAT_MONO8; sampleSize = 1; break;
		case AV_SAMPLE_FMT_S16:
		case AV_SAMPLE_FMT_S16P: fmt = AL_FORMAT_MONO16; sampleSize = sizeof(uint16); break;
		default:
			sampleSize = -1; // convert needed
		break;
		}
	break;
	case AV_CH_LAYOUT_STEREO:
		switch (inputFormat) {
		case AV_SAMPLE_FMT_U8: fmt = AL_FORMAT_STEREO8; sampleSize = 2; break;
		case AV_SAMPLE_FMT_S16: fmt = AL_FORMAT_STEREO16; sampleSize = 2 * sizeof(uint16); break;
		default:
			sampleSize = -1; // convert needed
		break;
		}
	break;
	default:
		sampleSize = -1; // convert needed
	break;
	}

	if (av_popcount64(layout) != codecParams->channels) {
		LOG(("Audio Error: Bad channel layout %1 for %2 channels."
			).arg(codecParams->channel_layout
			).arg(codecParams->channels
			));
		return false;
	}

	if (_samplesFrequency != Media::Player::kDefaultFrequency) {
		sampleSize = -1; // convert needed
	}

	if (sampleSize < 0) {
		swrContext = swr_alloc();
		if (!swrContext) {
			LOG(("Audio Error: Unable to swr_alloc for file '%1', data size '%2'").arg(_file.name()).arg(_data.size()));
			return false;
		}
		int64_t src_ch_layout = layout, dst_ch_layout = AudioToChannelLayout;
		srcRate = _samplesFrequency;
		AVSampleFormat src_sample_fmt = inputFormat, dst_sample_fmt = AudioToFormat;
		dstRate = Media::Player::kDefaultFrequency;

		av_opt_set_int(swrContext, "in_channel_layout", src_ch_layout, 0);
		av_opt_set_int(swrContext, "in_sample_rate", srcRate, 0);
		av_opt_set_sample_fmt(swrContext, "in_sample_fmt", src_sample_fmt, 0);
		av_opt_set_int(swrContext, "out_channel_layout", dst_ch_layout, 0);
		av_opt_set_int(swrContext, "out_sample_rate", dstRate, 0);
		av_opt_set_sample_fmt(swrContext, "out_sample_fmt", dst_sample_fmt, 0);

		if ((res = swr_init(swrContext)) < 0) {
			LOG(("Audio Error: Unable to swr_init for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}

		sampleSize = AudioToChannels * sizeof(short);
		_samplesFrequency = dstRate;
		_samplesCount = av_rescale_rnd(_samplesCount, dstRate, srcRate, AV_ROUND_UP);
		fmt = AL_FORMAT_STEREO16;

		maxResampleSamples = av_rescale_rnd(AVBlockSize / sampleSize, dstRate, srcRate, AV_ROUND_UP);
		if ((res = av_samples_alloc_array_and_samples(&dstSamplesData, 0, AudioToChannels, maxResampleSamples, AudioToFormat, 0)) < 0) {
			LOG(("Audio Error: Unable to av_samples_alloc for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}
	}
	if (positionMs) {
		const auto timeBase = fmtContext->streams[streamId]->time_base;
		const auto timeStamp = (positionMs * timeBase.den)
			/ (1000LL * timeBase.num);
		if (av_seek_frame(fmtContext, streamId, timeStamp, AVSEEK_FLAG_ANY) < 0) {
			if (av_seek_frame(fmtContext, streamId, timeStamp, 0) < 0) {
			}
		}
	}

	return true;
}

AudioPlayerLoader::ReadResult FFMpegLoader::readMore(QByteArray &result, int64 &samplesAdded) {
	int res;

	av_frame_unref(frame);
	res = avcodec_receive_frame(codecContext, frame);
	if (res >= 0) {
		return readFromReadyFrame(result, samplesAdded);
	}

	if (res == AVERROR_EOF) {
		return ReadResult::EndOfFile;
	} else if (res != AVERROR(EAGAIN)) {
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Audio Error: Unable to avcodec_receive_frame() file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return ReadResult::Error;
	}

	if ((res = av_read_frame(fmtContext, &avpkt)) < 0) {
		if (res != AVERROR_EOF) {
			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Audio Error: Unable to av_read_frame() file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return ReadResult::Error;
		}
		avcodec_send_packet(codecContext, nullptr); // drain
		return ReadResult::Ok;
	}

	if (avpkt.stream_index == streamId) {
		res = avcodec_send_packet(codecContext, &avpkt);
		if (res < 0) {
			av_packet_unref(&avpkt);

			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Audio Error: Unable to avcodec_send_packet() file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			// There is a sample voice message where skipping such packet
			// results in a crash (read_access to nullptr) in swr_convert().
			//if (res == AVERROR_INVALIDDATA) {
			//	return ReadResult::NotYet; // try to skip bad packet
			//}
			return ReadResult::Error;
		}
	}
	av_packet_unref(&avpkt);
	return ReadResult::Ok;
}

AudioPlayerLoader::ReadResult FFMpegLoader::readFromReadyFrame(QByteArray &result, int64 &samplesAdded) {
	int res = 0;

	if (dstSamplesData) { // convert needed
		int64_t dstSamples = av_rescale_rnd(swr_get_delay(swrContext, srcRate) + frame->nb_samples, dstRate, srcRate, AV_ROUND_UP);
		if (dstSamples > maxResampleSamples) {
			maxResampleSamples = dstSamples;
			av_freep(&dstSamplesData[0]);
			if ((res = av_samples_alloc(dstSamplesData, 0, AudioToChannels, maxResampleSamples, AudioToFormat, 1)) < 0) {
				char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
				LOG(("Audio Error: Unable to av_samples_alloc for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
				return ReadResult::Error;
			}
		}

		// There are crash reports of some files with swrContext->in.ch_count
		// equal to 2 and frame with only one channel data provided.
		// I'm not sure what to do with those files, could not get one for testing.
		// Currently just abort the reading because it crashes in swr_convert.
		//
		// Samples included:
		//
		// codecpar->channel_layout = 3
		// codecpar->channels = 1 (but it is 2 by the channel_layout!)
		// frame->channel_layout = 4
		// frame->channels = 1
		//
		// So it looks like codecpar->channel_layout was wrong and frame
		// really had only one channel, but swresample expected data for two channels.
		//
		// codecpar->channel_layout = 3
		// codecpar->channels = 2
		// frame->channel_layout = 4
		// frame->channels = 1
		//
		// So the frame just wasn't consistent with the codec params.
		if (frame->extended_data[1] == nullptr) {
			const auto params = fmtContext->streams[streamId]->codecpar;
			if (IsPlanarFormat(params->format) && params->channels > 1) {
				LOG(("Audio Error: Inconsistent frame layout/channels in file, codec: (%1;%2;%3), frame: (%4;%5;%6)."
					).arg(params->channel_layout
					).arg(params->channels
					).arg(params->format
					).arg(frame->channel_layout
					).arg(frame->channels
					).arg(frame->format
					));
				return ReadResult::Error;
			} else {
				const auto key = "ffmpeg_" + std::to_string(ptrdiff_t(this));
				const auto value = QString("codec: (%1;%2;%3), frame: (%4;%5;%6), ptrs: (%7;%8;%9)"
				).arg(params->channel_layout
				).arg(params->channels
				).arg(params->format
				).arg(frame->channel_layout
				).arg(frame->channels
				).arg(frame->format
				).arg(ptrdiff_t(frame->data[0])
				).arg(ptrdiff_t(frame->extended_data[0])
				).arg(ptrdiff_t(frame->data[1])
				);
				CrashReports::SetAnnotation(key, value);
			}
		}

		if ((res = swr_convert(swrContext, dstSamplesData, dstSamples, (const uint8_t**)frame->extended_data, frame->nb_samples)) < 0) {
			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Audio Error: Unable to swr_convert for file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return ReadResult::Error;
		}

		if (frame->extended_data[1] == nullptr) {
			const auto key = "ffmpeg_" + std::to_string(ptrdiff_t(this));
			CrashReports::ClearAnnotation(key);
		}

		int32 resultLen = av_samples_get_buffer_size(0, AudioToChannels, res, AudioToFormat, 1);
		result.append((const char*)dstSamplesData[0], resultLen);
		samplesAdded += resultLen / sampleSize;
	} else {
		result.append((const char*)frame->extended_data[0], frame->nb_samples * sampleSize);
		samplesAdded += frame->nb_samples;
	}
	return ReadResult::Ok;
}

FFMpegLoader::~FFMpegLoader() {
	if (codecContext) avcodec_free_context(&codecContext);
	if (swrContext) swr_free(&swrContext);
	if (dstSamplesData) {
		if (dstSamplesData[0]) {
			av_freep(&dstSamplesData[0]);
		}
		av_freep(&dstSamplesData);
	}
	av_frame_free(&frame);
}
