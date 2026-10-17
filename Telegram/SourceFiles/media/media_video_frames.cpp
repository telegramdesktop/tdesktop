/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/media_video_frames.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media::Video {
namespace {

using namespace FFmpeg;

constexpr auto kMaxDecodeSpan = crl::time(10000);
constexpr auto kForwardDecodeWindow = crl::time(3000);
constexpr auto kMaxDecodeArea = 4096 * int64(4096);

struct Source {
	ReadFileWrap fileWrap;
	ReadBytesWrap bytesWrap;
	QByteArray content;
	FormatPointer format;
	AVStream *videoStream = nullptr;
	CodecPointer codec;
	int videoId = -1;
	int rotation = 0;
};

[[nodiscard]] bool OpenSource(
		Source &source,
		const QString &path,
		const QByteArray &content,
		bool withDecoder) {
	if (!content.isEmpty()) {
		source.content = content;
		const auto data = source.content.constData();
		source.bytesWrap = ReadBytesWrap{
			.size = int64(source.content.size()),
			.data = reinterpret_cast<const uchar*>(data),
		};
		source.format = MakeFormatPointer(
			&source.bytesWrap,
			&ReadBytesWrap::Read,
			nullptr,
			&ReadBytesWrap::Seek);
	} else {
		source.fileWrap.file.setFileName(path);
		if (!source.fileWrap.file.open(QIODevice::ReadOnly)) {
			return false;
		}
		source.format = MakeFormatPointer(
			&source.fileWrap,
			&ReadFileWrap::Read,
			nullptr,
			&ReadFileWrap::Seek);
	}
	if (!source.format) {
		return false;
	}
	const auto error = AvErrorWrap(
		avformat_find_stream_info(source.format.get(), nullptr));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return false;
	}
	source.videoId = av_find_best_stream(
		source.format.get(),
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (source.videoId < 0) {
		return false;
	}
	source.videoStream = source.format->streams[source.videoId];
	if (source.videoStream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
		return false;
	}
	source.rotation = ReadRotationFromMetadata(source.videoStream);
	if (withDecoder) {
		source.codec = MakeCodecPointer({
			.stream = source.videoStream,
			.videoMaxArea = kMaxDecodeArea,
		});
		if (!source.codec) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] crl::time SourceDuration(const Source &source) {
	const auto stream = source.videoStream;
	if (stream->duration > 0) {
		return PtsToTimeCeil(stream->duration, stream->time_base);
	} else if (source.format->duration > 0) {
		return PtsToTimeCeil(source.format->duration, kUniversalTimeBase);
	}
	return 0;
}

[[nodiscard]] crl::time FramePosition(
		not_null<AVFrame*> frame,
		AVRational timeBase) {
	const auto pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
		? frame->best_effort_timestamp
		: frame->pts;
	return (pts != AV_NOPTS_VALUE) ? PtsToTime(pts, timeBase) : -1;
}

[[nodiscard]] QImage ConvertFrame(
		not_null<AVFrame*> frame,
		int rotation,
		QSize box,
		bool cover,
		SwscalePointer &scale) {
	const auto srcSize = QSize(frame->width, frame->height);
	if (srcSize.isEmpty()) {
		return {};
	}
	const auto displaySize = TransposeSizeByRotation(srcSize, rotation);
	auto scaledDisplay = displaySize;
	if (!box.isEmpty()) {
		scaledDisplay = displaySize.scaled(
			box,
			cover
				? Qt::KeepAspectRatioByExpanding
				: Qt::KeepAspectRatio);
	}
	auto dstSize = TransposeSizeByRotation(scaledDisplay, rotation);
	dstSize = QSize(
		std::max(dstSize.width(), 1),
		std::max(dstSize.height(), 1));

	auto storage = CreateFrameStorage(dstSize);
	if (storage.isNull()) {
		return {};
	}
	const auto srcFormat = (frame->format == AV_PIX_FMT_NONE)
		? AV_PIX_FMT_YUV420P
		: AVPixelFormat(frame->format);
	scale = MakeSwscalePointer(
		srcSize,
		srcFormat,
		dstSize,
		AV_PIX_FMT_BGRA,
		&scale);
	if (!scale) {
		return {};
	}
	uint8_t *dstData[AV_NUM_DATA_POINTERS] = { storage.bits(), nullptr };
	int dstLinesize[AV_NUM_DATA_POINTERS] = {
		int(storage.bytesPerLine()),
		0,
	};
	sws_scale(
		scale.get(),
		frame->data,
		frame->linesize,
		0,
		frame->height,
		dstData,
		dstLinesize);

	if (srcFormat == AV_PIX_FMT_BGRA || srcFormat == AV_PIX_FMT_YUVA420P) {
		PremultiplyInplace(storage);
	}

	auto result = storage;
	if (rotation) {
		auto transform = QTransform();
		transform.rotate(rotation);
		result = result.transformed(transform);
	}
	if (cover && !box.isEmpty() && result.size() != box) {
		const auto x = (result.width() - box.width()) / 2;
		const auto y = (result.height() - box.height()) / 2;
		result = result.copy(QRect(
			std::max(x, 0),
			std::max(y, 0),
			std::min(box.width(), result.width()),
			std::min(box.height(), result.height())));
	}
	return result;
}

class Extractor final {
public:
	Extractor(Source &source, const ExtractRequest &request);
	~Extractor();

	[[nodiscard]] bool valid() const;
	[[nodiscard]] QImage take(crl::time position);

private:
	[[nodiscard]] bool receiveInto(not_null<AVFrame*> frame);
	void seekTo(crl::time position);

	Source &_source;
	const ExtractRequest &_request;
	FramePointer _frame;
	FramePointer _kept;
	SwscalePointer _scale;
	AVPacket *_packet = nullptr;
	crl::time _decodedPosition = -1;
	bool _finished = false;

};

Extractor::Extractor(Source &source, const ExtractRequest &request)
: _source(source)
, _request(request)
, _frame(MakeFramePointer())
, _kept(MakeFramePointer())
, _packet(av_packet_alloc()) {
}

Extractor::~Extractor() {
	if (_packet) {
		av_packet_free(&_packet);
	}
}

bool Extractor::valid() const {
	return _frame && _kept && _packet && _source.codec;
}

void Extractor::seekTo(crl::time position) {
	const auto stream = _source.videoStream;
	const auto target = TimeToPts(
		std::max(position, crl::time(0)),
		stream->time_base);
	const auto seeked = av_seek_frame(
		_source.format.get(),
		_source.videoId,
		target,
		AVSEEK_FLAG_BACKWARD);
	if (seeked < 0) {
		av_seek_frame(
			_source.format.get(),
			_source.videoId,
			0,
			AVSEEK_FLAG_BACKWARD);
	}
	avcodec_flush_buffers(_source.codec.get());
	_decodedPosition = -1;
	_finished = false;
}

bool Extractor::receiveInto(not_null<AVFrame*> frame) {
	while (true) {
		auto got = AvErrorWrap(avcodec_receive_frame(
			_source.codec.get(),
			frame));
		if (!got) {
			return true;
		} else if (got.code() != AVERROR(EAGAIN)) {
			if (got.code() != AVERROR_EOF) {
				LogError(u"avcodec_receive_frame"_q, got);
			}
			return false;
		} else if (_finished) {
			return false;
		}
		auto read = AvErrorWrap(av_read_frame(
			_source.format.get(),
			_packet));
		if (read.code() == AVERROR_EOF) {
			_finished = true;
			avcodec_send_packet(_source.codec.get(), nullptr);
			continue;
		} else if (read) {
			LogError(u"av_read_frame"_q, read);
			return false;
		}
		const auto unref = gsl::finally([&] {
			av_packet_unref(_packet);
		});
		if (_packet->stream_index != _source.videoId) {
			continue;
		}
		auto sent = AvErrorWrap(avcodec_send_packet(
			_source.codec.get(),
			_packet));
		if (sent && sent.code() != AVERROR(EAGAIN)) {
			LogError(u"avcodec_send_packet"_q, sent);
			return false;
		}
	}
}

QImage Extractor::take(crl::time position) {
	const auto timeBase = _source.videoStream->time_base;
	const auto behind = (_decodedPosition < 0)
		|| (position < _decodedPosition);
	const auto tooFarAhead = !behind
		&& (position - _decodedPosition > kForwardDecodeWindow);
	if (behind || tooFarAhead) {
		seekTo(position);
	}
	auto has = false;
	auto firstAt = crl::time(-1);
	while (true) {
		if (!receiveInto(_frame.get())) {
			break;
		}
		const auto at = FramePosition(_frame.get(), timeBase);
		if (at >= 0) {
			_decodedPosition = at;
			if (firstAt < 0) {
				firstAt = at;
			}
		}
		av_frame_unref(_kept.get());
		av_frame_ref(_kept.get(), _frame.get());
		has = true;
		av_frame_unref(_frame.get());
		if (at < 0 || at >= position) {
			break;
		} else if (firstAt >= 0 && at - firstAt > kMaxDecodeSpan) {
			break;
		}
	}
	const auto seeked = (behind || tooFarAhead);
	if ((!has && seeked) || !FrameHasData(_kept.get())) {
		return {};
	}
	return ConvertFrame(
		_kept.get(),
		_source.rotation,
		_request.box,
		_request.cover,
		_scale);
}

} // namespace

FileInfo ReadFileInfo(const QString &path, const QByteArray &content) {
	auto source = Source();
	if (!OpenSource(source, path, content, true)) {
		return {};
	}
	const auto stream = source.videoStream;
	const auto original = QSize(
		stream->codecpar->width,
		stream->codecpar->height);
	return {
		.dimensions = TransposeSizeByRotation(original, source.rotation),
		.duration = SourceDuration(source),
	};
}

void ExtractFrames(
		const QString &path,
		const QByteArray &content,
		const ExtractRequest &request,
		ExtractCallback callback) {
	Expects(callback != nullptr);

	if (request.positions.empty()) {
		return;
	}
	auto source = Source();
	if (!OpenSource(source, path, content, true)) {
		return;
	}
	auto extractor = Extractor(source, request);
	if (!extractor.valid()) {
		return;
	}
	for (auto i = 0, count = int(request.positions.size()); i != count; ++i) {
		auto frame = extractor.take(request.positions[i]);
		if (!callback(i, std::move(frame))) {
			return;
		}
	}
}

QImage ExtractFrame(
		const QString &path,
		const QByteArray &content,
		crl::time position,
		QSize box) {
	auto result = QImage();
	ExtractFrames(path, content, {
		.positions = { position },
		.box = box,
	}, [&](int index, QImage &&frame) {
		result = std::move(frame);
		return false;
	});
	return result;
}

} // namespace Media::Video
