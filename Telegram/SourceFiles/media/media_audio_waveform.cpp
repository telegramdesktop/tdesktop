/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/media_audio_waveform.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media::Audio {
namespace {

using namespace FFmpeg;

constexpr auto kMinimalPeak = 2500;
constexpr auto kMaxPeak = int64(std::numeric_limits<uint16>::max());
constexpr auto kPeakAverageRatio = 1.8;
constexpr auto kFineSpansPerSecond = 50;
constexpr auto kMaxFineSpans = 100'000;

using SamplesCallback = Fn<void(const int16 *samples, int count, int rate)>;

class Decoder final {
public:
	[[nodiscard]] bool open(const QString &path, const QByteArray &bytes);
	[[nodiscard]] bool read(
		const SamplesCallback &callback,
		const std::atomic<bool> *cancel);

private:
	[[nodiscard]] bool convert(
		AVFrame *frame,
		const SamplesCallback &callback);

	ReadBytesWrap _bytesWrap;
	ReadFileWrap _fileWrap;
	FormatPointer _input;
	CodecPointer _decoder;
	SwresamplePointer _swr;
	FramePointer _decoded;
	Packet _packet;
	std::vector<int16> _buffer;
	int _streamIndex = -1;

};

bool Decoder::open(const QString &path, const QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		_bytesWrap = ReadBytesWrap{
			.size = int64(bytes.size()),
			.data = reinterpret_cast<const uchar*>(bytes.constData()),
		};
		_input = MakeFormatPointer(
			&_bytesWrap,
			&ReadBytesWrap::Read,
			nullptr,
			&ReadBytesWrap::Seek);
	} else {
		_fileWrap.file.setFileName(path);
		if (!_fileWrap.file.open(QIODevice::ReadOnly)) {
			return false;
		}
		_input = MakeFormatPointer(
			&_fileWrap,
			&ReadFileWrap::Read,
			nullptr,
			&ReadFileWrap::Seek);
	}
	if (!_input) {
		return false;
	}
	if (AvErrorWrap(avformat_find_stream_info(_input.get(), nullptr))) {
		return false;
	}
	_streamIndex = av_find_best_stream(
		_input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		-1,
		nullptr,
		0);
	if (_streamIndex < 0) {
		return false;
	}
	_decoder = MakeCodecPointer({ .stream = _input->streams[_streamIndex] });
	if (!_decoder) {
		return false;
	}
	_decoded = MakeFramePointer();
	return _decoded != nullptr;
}

bool Decoder::convert(AVFrame *frame, const SamplesCallback &callback) {
	auto mono = AVChannelLayout AV_CHANNEL_LAYOUT_MONO;
	_swr = MakeSwresamplePointer(
		&frame->ch_layout,
		AVSampleFormat(frame->format),
		frame->sample_rate,
		&mono,
		AV_SAMPLE_FMT_S16,
		frame->sample_rate,
		&_swr);
	if (!_swr) {
		return false;
	}
	const auto upper = int(swr_get_out_samples(
		_swr.get(),
		frame->nb_samples));
	if (upper <= 0) {
		return true;
	}
	_buffer.resize(upper);
	auto out = reinterpret_cast<uint8_t*>(_buffer.data());
	const auto samples = swr_convert(
		_swr.get(),
		&out,
		upper,
		(const uint8_t**)frame->extended_data,
		frame->nb_samples);
	if (samples < 0) {
		return false;
	} else if (samples > 0) {
		callback(_buffer.data(), samples, frame->sample_rate);
	}
	return true;
}

bool Decoder::read(
		const SamplesCallback &callback,
		const std::atomic<bool> *cancel) {
	auto eof = false;
	while (true) {
		if (cancel && cancel->load()) {
			return false;
		}
		const auto got = AvErrorWrap(avcodec_receive_frame(
			_decoder.get(),
			_decoded.get()));
		if (!got) {
			if (!convert(_decoded.get(), callback)) {
				return false;
			}
			continue;
		} else if (got.code() == AVERROR_EOF) {
			return true;
		} else if (got.code() != AVERROR(EAGAIN)) {
			return false;
		}
		if (eof) {
			const auto sent = AvErrorWrap(avcodec_send_packet(
				_decoder.get(),
				nullptr));
			if (sent && sent.code() != AVERROR_EOF) {
				return false;
			}
			continue;
		}
		av_packet_unref(&_packet.fields());
		const auto read = AvErrorWrap(av_read_frame(
			_input.get(),
			&_packet.fields()));
		if (read.code() == AVERROR_EOF) {
			eof = true;
			continue;
		} else if (read) {
			return false;
		} else if (_packet.fields().stream_index != _streamIndex) {
			continue;
		}
		const auto sent = AvErrorWrap(avcodec_send_packet(
			_decoder.get(),
			&_packet.fields()));
		if (sent && sent.code() != AVERROR_INVALIDDATA) {
			return false;
		}
	}
}

} // namespace

Waveform LoadWaveform(
		const QString &path,
		const QByteArray &bytes,
		int count,
		std::shared_ptr<std::atomic<bool>> cancel) {
	auto result = Waveform();
	if (count <= 0) {
		return result;
	}
	auto decoder = Decoder();
	if (!decoder.open(path, bytes)) {
		return result;
	}
	auto fine = std::vector<uint16>();
	auto current = uint16(0);
	auto inSpan = int64(0);
	auto spanSize = int64(0);
	const auto read = decoder.read([&](
			const int16 *samples,
			int available,
			int rate) {
		if (!spanSize) {
			spanSize = std::max(int64(rate / kFineSpansPerSecond), int64(1));
		}
		for (const auto value : gsl::make_span(samples, available)) {
			accumulate_max(current, uint16(std::abs(int(value))));
			if (++inSpan < spanSize) {
				continue;
			} else if (fine.size() < kMaxFineSpans) {
				fine.push_back(current);
				current = 0;
				inSpan = 0;
				continue;
			}
			for (auto i = 0; i != kMaxFineSpans / 2; ++i) {
				fine[i] = std::max(fine[2 * i], fine[2 * i + 1]);
			}
			fine.resize(kMaxFineSpans / 2);
			spanSize *= 2;
		}
	}, cancel.get());
	if (cancel && cancel->load()) {
		return result;
	} else if (inSpan && fine.size() < kMaxFineSpans) {
		fine.push_back(current);
	}
	if (fine.empty() || (!read && fine.size() < kFineSpansPerSecond)) {
		return result;
	}
	result.bars.resize(count);
	const auto total = int64(fine.size());
	for (auto i = 0; i != count; ++i) {
		const auto from = int64(i) * total / count;
		const auto till = std::max(int64(i + 1) * total / count, from + 1);
		auto peak = uint16(0);
		for (auto j = from; j < till && j < total; ++j) {
			accumulate_max(peak, fine[j]);
		}
		result.bars[i] = peak;
	}
	auto sum = int64(0);
	for (const auto bar : result.bars) {
		sum += bar;
	}
	const auto peak = std::max(
		int64(kMinimalPeak),
		int64(sum * kPeakAverageRatio / count));
	result.peak = uint16(std::min(peak, kMaxPeak));
	for (auto &bar : result.bars) {
		bar = std::min(bar, result.peak);
	}
	return result;
}

} // namespace Media::Audio
