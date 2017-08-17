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
#include "media/media_audio_capture.h"

#include "media/media_audio_ffmpeg_loader.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <numeric>

namespace Media {
namespace Capture {
namespace {

constexpr auto kCaptureFrequency = Player::kDefaultFrequency;
constexpr auto kCaptureSkipDuration = TimeMs(400);
constexpr auto kCaptureFadeInDuration = TimeMs(300);

Instance *CaptureInstance = nullptr;

bool ErrorHappened(ALCdevice *device) {
	ALenum errCode;
	if ((errCode = alcGetError(device)) != ALC_NO_ERROR) {
		LOG(("Audio Capture Error: %1, %2").arg(errCode).arg((const char *)alcGetString(device, errCode)));
		return true;
	}
	return false;
}

} // namespace

void Start() {
	Assert(CaptureInstance == nullptr);
	CaptureInstance = new Instance();
	instance()->check();
}

void Finish() {
	delete base::take(CaptureInstance);
}

Instance::Instance() : _inner(new Inner(&_thread)) {
	CaptureInstance = this;
	connect(this, SIGNAL(start()), _inner, SLOT(onStart()));
	connect(this, SIGNAL(stop(bool)), _inner, SLOT(onStop(bool)));
	connect(_inner, SIGNAL(done(QByteArray, VoiceWaveform, qint32)), this, SIGNAL(done(QByteArray, VoiceWaveform, qint32)));
	connect(_inner, SIGNAL(updated(quint16, qint32)), this, SIGNAL(updated(quint16, qint32)));
	connect(_inner, SIGNAL(error()), this, SIGNAL(error()));
	connect(&_thread, SIGNAL(started()), _inner, SLOT(onInit()));
	connect(&_thread, SIGNAL(finished()), _inner, SLOT(deleteLater()));
	_thread.start();
}

void Instance::check() {
	_available = false;
	if (auto defaultDevice = alcGetString(0, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER)) {
		if (auto device = alcCaptureOpenDevice(defaultDevice, kCaptureFrequency, AL_FORMAT_MONO16, kCaptureFrequency / 5)) {
			auto error = ErrorHappened(device);
			alcCaptureCloseDevice(device);
			_available = !error;
		}
	}
}

Instance::~Instance() {
	_inner = nullptr;
	_thread.quit();
	_thread.wait();
}

Instance *instance() {
	return CaptureInstance;
}

struct Instance::Inner::Private {
	ALCdevice *device = nullptr;
	AVOutputFormat *fmt = nullptr;
	uchar *ioBuffer = nullptr;
	AVIOContext *ioContext = nullptr;
	AVFormatContext *fmtContext = nullptr;
	AVStream *stream = nullptr;
	AVCodec *codec = nullptr;
	AVCodecContext *codecContext = nullptr;
	bool opened = false;

	int srcSamples = 0;
	int dstSamples = 0;
	int maxDstSamples = 0;
	int dstSamplesSize = 0;
	int fullSamples = 0;
	uint8_t **srcSamplesData = nullptr;
	uint8_t **dstSamplesData = nullptr;
	SwrContext *swrContext = nullptr;

	int32 lastUpdate = 0;
	uint16 levelMax = 0;

	QByteArray data;
	int32 dataPos = 0;

	int64 waveformMod = 0;
	int64 waveformEach = (kCaptureFrequency / 100);
	uint16 waveformPeak = 0;
	QVector<uchar> waveform;

	static int _read_data(void *opaque, uint8_t *buf, int buf_size) {
		auto l = reinterpret_cast<Private*>(opaque);

		int32 nbytes = qMin(l->data.size() - l->dataPos, int32(buf_size));
		if (nbytes <= 0) {
			return 0;
		}

		memcpy(buf, l->data.constData() + l->dataPos, nbytes);
		l->dataPos += nbytes;
		return nbytes;
	}

	static int _write_data(void *opaque, uint8_t *buf, int buf_size) {
		auto l = reinterpret_cast<Private*>(opaque);

		if (buf_size <= 0) return 0;
		if (l->dataPos + buf_size > l->data.size()) l->data.resize(l->dataPos + buf_size);
		memcpy(l->data.data() + l->dataPos, buf, buf_size);
		l->dataPos += buf_size;
		return buf_size;
	}

	static int64_t _seek_data(void *opaque, int64_t offset, int whence) {
		auto l = reinterpret_cast<Private*>(opaque);

		int32 newPos = -1;
		switch (whence) {
		case SEEK_SET: newPos = offset; break;
		case SEEK_CUR: newPos = l->dataPos + offset; break;
		case SEEK_END: newPos = l->data.size() + offset; break;
		case AVSEEK_SIZE: {
			// Special whence for determining filesize without any seek.
			return l->data.size();
		} break;
		}
		if (newPos < 0) {
			return -1;
		}
		l->dataPos = newPos;
		return l->dataPos;
	}
};

Instance::Inner::Inner(QThread *thread) : d(new Private()) {
	moveToThread(thread);
	_timer.moveToThread(thread);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(onTimeout()));
}

Instance::Inner::~Inner() {
	onStop(false);
	delete d;
}

void Instance::Inner::onInit() {
}

void Instance::Inner::onStart() {

	// Start OpenAL Capture
	const ALCchar *dName = alcGetString(0, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
	DEBUG_LOG(("Audio Info: Capture device name '%1'").arg(dName));
	d->device = alcCaptureOpenDevice(dName, kCaptureFrequency, AL_FORMAT_MONO16, kCaptureFrequency / 5);
	if (!d->device) {
		LOG(("Audio Error: capture device not present!"));
		emit error();
		return;
	}
	alcCaptureStart(d->device);
	if (ErrorHappened(d->device)) {
		alcCaptureCloseDevice(d->device);
		d->device = nullptr;
		emit error();
		return;
	}

	// Create encoding context

	d->ioBuffer = (uchar*)av_malloc(AVBlockSize);

	d->ioContext = avio_alloc_context(d->ioBuffer, AVBlockSize, 1, static_cast<void*>(d), &Private::_read_data, &Private::_write_data, &Private::_seek_data);
	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
	AVOutputFormat *fmt = 0;
	while ((fmt = av_oformat_next(fmt))) {
		if (fmt->name == qstr("opus")) {
			break;
		}
	}
	if (!fmt) {
		LOG(("Audio Error: Unable to find opus AVOutputFormat for capture"));
		onStop(false);
		emit error();
		return;
	}

	if ((res = avformat_alloc_output_context2(&d->fmtContext, fmt, 0, 0)) < 0) {
		LOG(("Audio Error: Unable to avformat_alloc_output_context2 for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}
	d->fmtContext->pb = d->ioContext;
	d->fmtContext->flags |= AVFMT_FLAG_CUSTOM_IO;
	d->opened = true;

	// Add audio stream
	d->codec = avcodec_find_encoder(fmt->audio_codec);
	if (!d->codec) {
		LOG(("Audio Error: Unable to avcodec_find_encoder for capture"));
		onStop(false);
		emit error();
		return;
	}
	d->stream = avformat_new_stream(d->fmtContext, d->codec);
	if (!d->stream) {
		LOG(("Audio Error: Unable to avformat_new_stream for capture"));
		onStop(false);
		emit error();
		return;
	}
	d->stream->id = d->fmtContext->nb_streams - 1;
	d->codecContext = avcodec_alloc_context3(d->codec);
	if (!d->codecContext) {
		LOG(("Audio Error: Unable to avcodec_alloc_context3 for capture"));
		onStop(false);
		emit error();
		return;
	}

	av_opt_set_int(d->codecContext, "refcounted_frames", 1, 0);

	d->codecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
	d->codecContext->bit_rate = 64000;
	d->codecContext->channel_layout = AV_CH_LAYOUT_MONO;
	d->codecContext->sample_rate = kCaptureFrequency;
	d->codecContext->channels = 1;

	if (d->fmtContext->oformat->flags & AVFMT_GLOBALHEADER) {
		d->codecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}

	// Open audio stream
	if ((res = avcodec_open2(d->codecContext, d->codec, nullptr)) < 0) {
		LOG(("Audio Error: Unable to avcodec_open2 for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	// Alloc source samples

	d->srcSamples = (d->codecContext->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE) ? 10000 : d->codecContext->frame_size;
	//if ((res = av_samples_alloc_array_and_samples(&d->srcSamplesData, 0, d->codecContext->channels, d->srcSamples, d->codecContext->sample_fmt, 0)) < 0) {
	//	LOG(("Audio Error: Unable to av_samples_alloc_array_and_samples for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
	//	onStop(false);
	//	emit error();
	//	return;
	//}
	// Using _captured directly

	// Prepare resampling
	d->swrContext = swr_alloc();
	if (!d->swrContext) {
		fprintf(stderr, "Could not allocate resampler context\n");
		exit(1);
	}

	av_opt_set_int(d->swrContext, "in_channel_count", d->codecContext->channels, 0);
	av_opt_set_int(d->swrContext, "in_sample_rate", d->codecContext->sample_rate, 0);
	av_opt_set_sample_fmt(d->swrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
	av_opt_set_int(d->swrContext, "out_channel_count", d->codecContext->channels, 0);
	av_opt_set_int(d->swrContext, "out_sample_rate", d->codecContext->sample_rate, 0);
	av_opt_set_sample_fmt(d->swrContext, "out_sample_fmt", d->codecContext->sample_fmt, 0);

	if ((res = swr_init(d->swrContext)) < 0) {
		LOG(("Audio Error: Unable to swr_init for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	d->maxDstSamples = d->srcSamples;
	if ((res = av_samples_alloc_array_and_samples(&d->dstSamplesData, 0, d->codecContext->channels, d->maxDstSamples, d->codecContext->sample_fmt, 0)) < 0) {
		LOG(("Audio Error: Unable to av_samples_alloc_array_and_samples for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}
	d->dstSamplesSize = av_samples_get_buffer_size(0, d->codecContext->channels, d->maxDstSamples, d->codecContext->sample_fmt, 0);

	if ((res = avcodec_parameters_from_context(d->stream->codecpar, d->codecContext)) < 0) {
		LOG(("Audio Error: Unable to avcodec_parameters_from_context for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	// Write file header
	if ((res = avformat_write_header(d->fmtContext, 0)) < 0) {
		LOG(("Audio Error: Unable to avformat_write_header for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	_timer.start(50);
	_captured.clear();
	_captured.reserve(AudioVoiceMsgBufferSize);
	DEBUG_LOG(("Audio Capture: started!"));
}

void Instance::Inner::onStop(bool needResult) {
	if (!_timer.isActive()) return; // in onStop() already
	_timer.stop();

	if (d->device) {
		alcCaptureStop(d->device);
		onTimeout(); // get last data
	}

	// Write what is left
	if (!_captured.isEmpty()) {
		auto fadeSamples = kCaptureFadeInDuration * kCaptureFrequency / 1000;
		auto capturedSamples = static_cast<int>(_captured.size() / sizeof(short));
		if ((_captured.size() % sizeof(short)) || (d->fullSamples + capturedSamples < kCaptureFrequency) || (capturedSamples < fadeSamples)) {
			d->fullSamples = 0;
			d->dataPos = 0;
			d->data.clear();
			d->waveformMod = 0;
			d->waveformPeak = 0;
			d->waveform.clear();
		} else {
			float64 coef = 1. / fadeSamples, fadedFrom = 0;
			for (short *ptr = ((short*)_captured.data()) + capturedSamples, *end = ptr - fadeSamples; ptr != end; ++fadedFrom) {
				--ptr;
				*ptr = qRound(fadedFrom * coef * *ptr);
			}
			if (capturedSamples % d->srcSamples) {
				int32 s = _captured.size();
				_captured.resize(s + (d->srcSamples - (capturedSamples % d->srcSamples)) * sizeof(short));
				memset(_captured.data() + s, 0, _captured.size() - s);
			}

			int32 framesize = d->srcSamples * d->codecContext->channels * sizeof(short), encoded = 0;
			while (_captured.size() >= encoded + framesize) {
				processFrame(encoded, framesize);
				encoded += framesize;
			}
			writeFrame(nullptr); // drain the codec
			if (encoded != _captured.size()) {
				d->fullSamples = 0;
				d->dataPos = 0;
				d->data.clear();
				d->waveformMod = 0;
				d->waveformPeak = 0;
				d->waveform.clear();
			}
		}
	}
	DEBUG_LOG(("Audio Capture: stopping (need result: %1), size: %2, samples: %3").arg(Logs::b(needResult)).arg(d->data.size()).arg(d->fullSamples));
	_captured = QByteArray();

	// Finish stream
	if (d->device) {
		av_write_trailer(d->fmtContext);
	}

	QByteArray result = d->fullSamples ? d->data : QByteArray();
	VoiceWaveform waveform;
	qint32 samples = d->fullSamples;
	if (samples && !d->waveform.isEmpty()) {
		int64 count = d->waveform.size(), sum = 0;
		if (count >= Player::kWaveformSamplesCount) {
			QVector<uint16> peaks;
			peaks.reserve(Player::kWaveformSamplesCount);

			uint16 peak = 0;
			for (int32 i = 0; i < count; ++i) {
				uint16 sample = uint16(d->waveform.at(i)) * 256;
				if (peak < sample) {
					peak = sample;
				}
				sum += Player::kWaveformSamplesCount;
				if (sum >= count) {
					sum -= count;
					peaks.push_back(peak);
					peak = 0;
				}
			}

			auto sum = std::accumulate(peaks.cbegin(), peaks.cend(), 0LL);
			peak = qMax(int32(sum * 1.8 / peaks.size()), 2500);

			waveform.resize(peaks.size());
			for (int32 i = 0, l = peaks.size(); i != l; ++i) {
				waveform[i] = char(qMin(31U, uint32(qMin(peaks.at(i), peak)) * 31 / peak));
			}
		}
	}
	if (d->device) {
		alcCaptureStop(d->device);
		alcCaptureCloseDevice(d->device);
		d->device = nullptr;

		if (d->codecContext) {
			avcodec_free_context(&d->codecContext);
			d->codecContext = nullptr;
		}
		if (d->srcSamplesData) {
			if (d->srcSamplesData[0]) {
				av_freep(&d->srcSamplesData[0]);
			}
			av_freep(&d->srcSamplesData);
		}
		if (d->dstSamplesData) {
			if (d->dstSamplesData[0]) {
				av_freep(&d->dstSamplesData[0]);
			}
			av_freep(&d->dstSamplesData);
		}
		d->fullSamples = 0;
		if (d->swrContext) {
			swr_free(&d->swrContext);
			d->swrContext = nullptr;
		}
		if (d->opened) {
			avformat_close_input(&d->fmtContext);
			d->opened = false;
		}
		if (d->ioContext) {
			av_freep(&d->ioContext->buffer);
			av_freep(&d->ioContext);
			d->ioBuffer = nullptr;
		} else if (d->ioBuffer) {
			av_freep(&d->ioBuffer);
		}
		if (d->fmtContext) {
			avformat_free_context(d->fmtContext);
			d->fmtContext = nullptr;
		}
		d->fmt = nullptr;
		d->stream = nullptr;
		d->codec = nullptr;

		d->lastUpdate = 0;
		d->levelMax = 0;

		d->dataPos = 0;
		d->data.clear();

		d->waveformMod = 0;
		d->waveformPeak = 0;
		d->waveform.clear();
	}
	if (needResult) emit done(result, waveform, samples);
}

void Instance::Inner::onTimeout() {
	if (!d->device) {
		_timer.stop();
		return;
	}
	ALint samples;
	alcGetIntegerv(d->device, ALC_CAPTURE_SAMPLES, sizeof(samples), &samples);
	if (ErrorHappened(d->device)) {
		onStop(false);
		emit error();
		return;
	}
	if (samples > 0) {
		// Get samples from OpenAL
		auto s = _captured.size();
		auto news = s + static_cast<int>(samples * sizeof(short));
		if (news / AudioVoiceMsgBufferSize > s / AudioVoiceMsgBufferSize) {
			_captured.reserve(((news / AudioVoiceMsgBufferSize) + 1) * AudioVoiceMsgBufferSize);
		}
		_captured.resize(news);
		alcCaptureSamples(d->device, (ALCvoid *)(_captured.data() + s), samples);
		if (ErrorHappened(d->device)) {
			onStop(false);
			emit error();
			return;
		}

		// Count new recording level and update view
		auto skipSamples = kCaptureSkipDuration * kCaptureFrequency / 1000;
		auto fadeSamples = kCaptureFadeInDuration * kCaptureFrequency / 1000;
		auto levelindex = d->fullSamples + static_cast<int>(s / sizeof(short));
		for (auto ptr = (const short*)(_captured.constData() + s), end = (const short*)(_captured.constData() + news); ptr < end; ++ptr, ++levelindex) {
			if (levelindex > skipSamples) {
				uint16 value = qAbs(*ptr);
				if (levelindex < skipSamples + fadeSamples) {
					value = qRound(value * float64(levelindex - skipSamples) / fadeSamples);
				}
				if (d->levelMax < value) {
					d->levelMax = value;
				}
			}
		}
		qint32 samplesFull = d->fullSamples + _captured.size() / sizeof(short), samplesSinceUpdate = samplesFull - d->lastUpdate;
		if (samplesSinceUpdate > AudioVoiceMsgUpdateView * kCaptureFrequency / 1000) {
			emit updated(d->levelMax, samplesFull);
			d->lastUpdate = samplesFull;
			d->levelMax = 0;
		}
		// Write frames
		int32 framesize = d->srcSamples * d->codecContext->channels * sizeof(short), encoded = 0;
		while (uint32(_captured.size()) >= encoded + framesize + fadeSamples * sizeof(short)) {
			processFrame(encoded, framesize);
			encoded += framesize;
		}

		// Collapse the buffer
		if (encoded > 0) {
			int32 goodSize = _captured.size() - encoded;
			memmove(_captured.data(), _captured.constData() + encoded, goodSize);
			_captured.resize(goodSize);
		}
	} else {
		DEBUG_LOG(("Audio Capture: no samples to capture."));
	}
}

void Instance::Inner::processFrame(int32 offset, int32 framesize) {
	// Prepare audio frame

	if (framesize % sizeof(short)) { // in the middle of a sample
		LOG(("Audio Error: Bad framesize in writeFrame() for capture, framesize %1, %2").arg(framesize));
		onStop(false);
		emit error();
		return;
	}
	auto samplesCnt = static_cast<int>(framesize / sizeof(short));

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	auto srcSamplesDataChannel = (short*)(_captured.data() + offset);
	auto srcSamplesData = &srcSamplesDataChannel;

	//	memcpy(d->srcSamplesData[0], _captured.constData() + offset, framesize);
	auto skipSamples = static_cast<int>(kCaptureSkipDuration * kCaptureFrequency / 1000);
	auto fadeSamples = static_cast<int>(kCaptureFadeInDuration * kCaptureFrequency / 1000);
	if (d->fullSamples < skipSamples + fadeSamples) {
		int32 fadedCnt = qMin(samplesCnt, skipSamples + fadeSamples - d->fullSamples);
		float64 coef = 1. / fadeSamples, fadedFrom = d->fullSamples - skipSamples;
		short *ptr = srcSamplesDataChannel, *zeroEnd = ptr + qMin(samplesCnt, qMax(0, skipSamples - d->fullSamples)), *end = ptr + fadedCnt;
		for (; ptr != zeroEnd; ++ptr, ++fadedFrom) {
			*ptr = 0;
		}
		for (; ptr != end; ++ptr, ++fadedFrom) {
			*ptr = qRound(fadedFrom * coef * *ptr);
		}
	}

	d->waveform.reserve(d->waveform.size() + (samplesCnt / d->waveformEach) + 1);
	for (short *ptr = srcSamplesDataChannel, *end = ptr + samplesCnt; ptr != end; ++ptr) {
		uint16 value = qAbs(*ptr);
		if (d->waveformPeak < value) {
			d->waveformPeak = value;
		}
		if (++d->waveformMod == d->waveformEach) {
			d->waveformMod -= d->waveformEach;
			d->waveform.push_back(uchar(d->waveformPeak / 256));
			d->waveformPeak = 0;
		}
	}

	// Convert to final format

	d->dstSamples = av_rescale_rnd(swr_get_delay(d->swrContext, d->codecContext->sample_rate) + d->srcSamples, d->codecContext->sample_rate, d->codecContext->sample_rate, AV_ROUND_UP);
	if (d->dstSamples > d->maxDstSamples) {
		d->maxDstSamples = d->dstSamples;
		av_freep(&d->dstSamplesData[0]);
		if ((res = av_samples_alloc(d->dstSamplesData, 0, d->codecContext->channels, d->dstSamples, d->codecContext->sample_fmt, 1)) < 0) {
			LOG(("Audio Error: Unable to av_samples_alloc for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			onStop(false);
			emit error();
			return;
		}
		d->dstSamplesSize = av_samples_get_buffer_size(0, d->codecContext->channels, d->maxDstSamples, d->codecContext->sample_fmt, 0);
	}

	if ((res = swr_convert(d->swrContext, d->dstSamplesData, d->dstSamples, (const uint8_t **)srcSamplesData, d->srcSamples)) < 0) {
		LOG(("Audio Error: Unable to swr_convert for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	// Write audio frame

	AVFrame *frame = av_frame_alloc();

	frame->nb_samples = d->dstSamples;
	frame->pts = av_rescale_q(d->fullSamples, AVRational { 1, d->codecContext->sample_rate }, d->codecContext->time_base);

	avcodec_fill_audio_frame(frame, d->codecContext->channels, d->codecContext->sample_fmt, d->dstSamplesData[0], d->dstSamplesSize, 0);

	writeFrame(frame);

	d->fullSamples += samplesCnt;

	av_frame_free(&frame);
}

void Instance::Inner::writeFrame(AVFrame *frame) {
	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	res = avcodec_send_frame(d->codecContext, frame);
	if (res == AVERROR(EAGAIN)) {
		int packetsWritten = writePackets();
		if (packetsWritten < 0) {
			if (frame && packetsWritten == AVERROR_EOF) {
				LOG(("Audio Error: EOF in packets received when EAGAIN was got in avcodec_send_frame()"));
				onStop(false);
				emit error();
			}
			return;
		} else if (!packetsWritten) {
			LOG(("Audio Error: No packets received when EAGAIN was got in avcodec_send_frame()"));
			onStop(false);
			emit error();
			return;
		}
		res = avcodec_send_frame(d->codecContext, frame);
	}
	if (res < 0) {
		LOG(("Audio Error: Unable to avcodec_send_frame for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		onStop(false);
		emit error();
		return;
	}

	if (!frame) { // drain
		if ((res = writePackets()) != AVERROR_EOF) {
			LOG(("Audio Error: not EOF in packets received when draining the codec, result %1").arg(res));
			onStop(false);
			emit error();
		}
	}
}

int Instance::Inner::writePackets() {
	AVPacket pkt;
	memset(&pkt, 0, sizeof(pkt)); // data and size must be 0;

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	int written = 0;
	do {
		av_init_packet(&pkt);
		if ((res = avcodec_receive_packet(d->codecContext, &pkt)) < 0) {
			if (res == AVERROR(EAGAIN)) {
				return written;
			} else if (res == AVERROR_EOF) {
				return res;
			}
			LOG(("Audio Error: Unable to avcodec_receive_packet for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			onStop(false);
			emit error();
			return res;
		}

		av_packet_rescale_ts(&pkt, d->codecContext->time_base, d->stream->time_base);
		pkt.stream_index = d->stream->index;
		if ((res = av_interleaved_write_frame(d->fmtContext, &pkt)) < 0) {
			LOG(("Audio Error: Unable to av_interleaved_write_frame for capture, error %1, %2").arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			onStop(false);
			emit error();
			return -1;
		}

		++written;
		av_packet_unref(&pkt);
	} while (true);
	return written;
}

} // namespace Capture
} // namespace Media
