/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ffmpeg/ffmpeg_frame_generator.h"

#include "ffmpeg/ffmpeg_utility.h"
#include "base/debug_log.h"

namespace FFmpeg {
namespace {

constexpr auto kMaxArea = 1920 * 1080 * 4;

} // namespace

class FrameGenerator::Impl final {
public:
	explicit Impl(const QByteArray &bytes);

	[[nodiscard]] Frame renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode);
	[[nodiscard]] Frame renderCurrent(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode);
	void jumpToStart();

private:
	struct ReadFrame {
		FramePointer frame;
		crl::time position = 0;
		crl::time duration = 0;
	};

	void readNextFrame();
	void resolveNextFrameTiming();

	[[nodiscard]] QString wrapError(int result) const;

	bool rotationSwapWidthHeight() const {
		return (_rotation == 90) || (_rotation == 270);
	}

	[[nodiscard]] static int Read(
		void *opaque,
		uint8_t *buf,
		int buf_size);
	[[nodiscard]] static int64_t Seek(
		void *opaque,
		int64_t offset,
		int whence);
	[[nodiscard]] int read(uint8_t *buf, int buf_size);
	[[nodiscard]] int64_t seek(int64_t offset, int whence);

	const QByteArray _bytes;
	int _deviceOffset = 0;

	FormatPointer _format;
	ReadFrame _current;
	ReadFrame _next;
	CodecPointer _codec;
	SwscalePointer _scale;

	int _streamId = 0;
	int _rotation = 0;
	//AVRational _aspect = kNormalAspect;

	crl::time _framePosition = 0;
	int _nextFrameDelay = 0;
	int _currentFrameDelay = 0;

};

FrameGenerator::Impl::Impl(const QByteArray &bytes)
: _bytes(bytes) {
	_format = MakeFormatPointer(
		static_cast<void*>(this),
		&FrameGenerator::Impl::Read,
		nullptr,
		&FrameGenerator::Impl::Seek);

	auto error = 0;
	if ((error = avformat_find_stream_info(_format.get(), nullptr))) {
		return;
	}
	_streamId = av_find_best_stream(
		_format.get(),
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (_streamId < 0) {
		return;
	}

	const auto info = _format->streams[_streamId];
	_rotation = ReadRotationFromMetadata(info);
	//_aspect = ValidateAspectRatio(info->sample_aspect_ratio);
	_codec = MakeCodecPointer({ .stream = info });
}

int FrameGenerator::Impl::Read(void *opaque, uint8_t *buf, int buf_size) {
	return static_cast<Impl*>(opaque)->read(buf, buf_size);
}

int FrameGenerator::Impl::read(uint8_t *buf, int buf_size) {
	const auto available = _bytes.size() - _deviceOffset;
	if (available <= 0) {
		return AVERROR_EOF;
	}
	const auto fill = std::min(int(available), buf_size);
	memcpy(buf, _bytes.data() + _deviceOffset, fill);
	_deviceOffset += fill;
	return fill;
}

int64_t FrameGenerator::Impl::Seek(
		void *opaque,
		int64_t offset,
		int whence) {
	return static_cast<Impl*>(opaque)->seek(offset, whence);
}

int64_t FrameGenerator::Impl::seek(int64_t offset, int whence) {
	if (whence == AVSEEK_SIZE) {
		return _bytes.size();
	}
	const auto now = [&] {
		switch (whence) {
		case SEEK_SET: return offset;
		case SEEK_CUR: return _deviceOffset + offset;
		case SEEK_END: return int64_t(_bytes.size()) + offset;
		}
		return int64_t(-1);
	}();
	if (now < 0 || now > _bytes.size()) {
		return -1;
	}
	_deviceOffset = now;
	return now;
}

FrameGenerator::Frame FrameGenerator::Impl::renderCurrent(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	Expects(_current.frame != nullptr);

	const auto frame = _current.frame.get();
	const auto width = frame->width;
	const auto height = frame->height;
	if (!width || !height) {
		LOG(("Webm Error: Bad frame size: %1x%2 ").arg(width).arg(height));
		return {};
	}

	auto scaled = QSize(width, height).scaled(size, mode);
	if (!scaled.isEmpty() && rotationSwapWidthHeight()) {
		scaled.transpose();
	}
	if (!GoodStorageForFrame(storage, size)) {
		storage = CreateFrameStorage(size);
	}
	const auto dx = (size.width() - scaled.width()) / 2;
	const auto dy = (size.height() - scaled.height()) / 2;
	Assert(dx >= 0 && dy >= 0 && (!dx || !dy));

	const auto srcFormat = (frame->format == AV_PIX_FMT_NONE)
		? _codec->pix_fmt
		: frame->format;
	const auto srcSize = QSize(frame->width, frame->height);
	const auto dstFormat = AV_PIX_FMT_BGRA;
	const auto dstSize = scaled;
	const auto bgra = (srcFormat == AV_PIX_FMT_BGRA);
	const auto withAlpha = bgra || (srcFormat == AV_PIX_FMT_YUVA420P);
	const auto dstPerLine = storage.bytesPerLine();
	auto dst = storage.bits() + dx * sizeof(int32) + dy * dstPerLine;
	if (srcSize == dstSize && bgra) {
		const auto srcPerLine = frame->linesize[0];
		const auto perLine = std::min(srcPerLine, int(dstPerLine));
		auto src = frame->data[0];
		for (auto y = 0, height = srcSize.height(); y != height; ++y) {
			memcpy(dst, src, perLine);
			src += srcPerLine;
			dst += dstPerLine;
		}
	} else {
		_scale = MakeSwscalePointer(
			srcSize,
			srcFormat,
			dstSize,
			dstFormat,
			&_scale);
		Assert(_scale != nullptr);

		// AV_NUM_DATA_POINTERS defined in AVFrame struct
		uint8_t *dstData[AV_NUM_DATA_POINTERS] = { dst, nullptr };
		int dstLinesize[AV_NUM_DATA_POINTERS] = { int(dstPerLine), 0 };
		sws_scale(
			_scale.get(),
			frame->data,
			frame->linesize,
			0,
			frame->height,
			dstData,
			dstLinesize);
	}
	if (dx && size.height() > 0) {
		auto dst = storage.bits();
		const auto line = scaled.width() * sizeof(int32);
		memset(dst, 0, dx * sizeof(int32));
		dst += dx * sizeof(int32);
		for (auto y = 0; y != size.height() - 1; ++y) {
			memset(dst + line, 0, (dstPerLine - line));
			dst += dstPerLine;
		}
		dst += line;
		memset(dst, 0, (size.width() - scaled.width() - dx) * sizeof(int32));
	} else if (dy && size.width() > 0) {
		const auto dst = storage.bits();
		memset(dst, 0, dstPerLine * dy);
		memset(
			dst + dstPerLine * (dy + scaled.height()),
			0,
			dstPerLine * (size.height() - scaled.height() - dy));
	}
	if (withAlpha) {
		PremultiplyInplace(storage);
	}
	if (_rotation != 0) {
		auto transform = QTransform();
		transform.rotate(_rotation);
		storage = storage.transformed(transform);
	}

	const auto duration = _next.frame
		? (_next.position - _current.position)
		: _current.duration;
	return {
		.duration = duration,
		.image = std::move(storage),
		.last = !_next.frame,
	};
}

FrameGenerator::Frame FrameGenerator::Impl::renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	if (!_current.frame) {
		readNextFrame();
	}
	std::swap(_current, _next);
	if (!_current.frame) {
		return {};
	}
	readNextFrame();
	return renderCurrent(std::move(storage), size, mode);
}

void FrameGenerator::Impl::jumpToStart() {
	auto result = 0;
	if ((result = avformat_seek_file(_format.get(), _streamId, std::numeric_limits<int64_t>::min(), 0, std::numeric_limits<int64_t>::max(), 0)) < 0) {
		if ((result = av_seek_frame(_format.get(), _streamId, 0, AVSEEK_FLAG_BYTE)) < 0) {
			if ((result = av_seek_frame(_format.get(), _streamId, 0, AVSEEK_FLAG_FRAME)) < 0) {
				if ((result = av_seek_frame(_format.get(), _streamId, 0, 0)) < 0) {
					LOG(("Webm Error: Unable to av_seek_frame() to the start, ") + wrapError(result));
					return;
				}
			}
		}
	}
	avcodec_flush_buffers(_codec.get());
	_current = ReadFrame();
	_next = ReadFrame();
	_currentFrameDelay = _nextFrameDelay = 0;
	_framePosition = 0;
}

void FrameGenerator::Impl::resolveNextFrameTiming() {
	const auto base = _format->streams[_streamId]->time_base;
	const auto duration = _next.frame->pkt_duration;
	const auto framePts = _next.frame->pts;
	auto framePosition = (framePts * 1000LL * base.num) / base.den;
	_currentFrameDelay = _nextFrameDelay;
	if (_framePosition + _currentFrameDelay < framePosition) {
		_currentFrameDelay = int32(framePosition - _framePosition);
	} else if (framePosition < _framePosition + _currentFrameDelay) {
		framePosition = _framePosition + _currentFrameDelay;
	}

	if (duration == AV_NOPTS_VALUE) {
		_nextFrameDelay = 0;
	} else {
		_nextFrameDelay = (duration * 1000LL * base.num) / base.den;
	}
	_framePosition = framePosition;

	_next.position = _framePosition;
	_next.duration = _nextFrameDelay;
}

void FrameGenerator::Impl::readNextFrame() {
	auto frame = _next.frame ? base::take(_next.frame) : MakeFramePointer();
	while (true) {
		auto result = avcodec_receive_frame(_codec.get(), frame.get());
		if (result >= 0) {
			if (frame->width * frame->height > kMaxArea) {
				return;
			}
			_next.frame = std::move(frame);
			resolveNextFrameTiming();
			return;
		}

		if (result == AVERROR_EOF) {
			return;
		} else if (result != AVERROR(EAGAIN)) {
			LOG(("Webm Error: Unable to avcodec_receive_frame(), ")
				+ wrapError(result));
			return;
		}

		auto packet = Packet();
		auto finished = false;
		do {
			const auto result = av_read_frame(
				_format.get(),
				&packet.fields());
			if (result == AVERROR_EOF) {
				finished = true;
				break;
			} else if (result < 0) {
				LOG(("Webm Error: Unable to av_read_frame(), ")
					+ wrapError(result));
				return;
			}
		} while (packet.fields().stream_index != _streamId);

		if (finished) {
			result = avcodec_send_packet(_codec.get(), nullptr); // Drain.
		} else {
			const auto native = &packet.fields();
			const auto guard = gsl::finally([
				&,
				size = native->size,
				data = native->data
			] {
				native->size = size;
				native->data = data;
				packet = Packet();
			});
			result = avcodec_send_packet(_codec.get(), native);
		}
		if (result < 0) {
			LOG(("Webm Error: Unable to avcodec_send_packet(), ")
				+ wrapError(result));
			return;
		}
	}
}

QString FrameGenerator::Impl::wrapError(int result) const {
	auto error = std::array<char, AV_ERROR_MAX_STRING_SIZE>{};
	return u"error %1, %2"_q
		.arg(result)
		.arg(av_make_error_string(error.data(), error.size(), result));
}

FrameGenerator::FrameGenerator(const QByteArray &bytes)
: _impl(std::make_unique<Impl>(bytes)) {
}

FrameGenerator::~FrameGenerator() = default;

int FrameGenerator::count() {
	return 0;
}

double FrameGenerator::rate() {
	return 0.;
}

FrameGenerator::Frame FrameGenerator::renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	return _impl->renderNext(std::move(storage), size, mode);
}

FrameGenerator::Frame FrameGenerator::renderCurrent(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	return _impl->renderCurrent(std::move(storage), size, mode);
}

void FrameGenerator::jumpToStart() {
	_impl->jumpToStart();
}

} // namespace FFmpeg
