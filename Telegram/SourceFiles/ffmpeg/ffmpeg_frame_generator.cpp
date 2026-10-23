/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ffmpeg/ffmpeg_frame_generator.h"

#include "ffmpeg/ffmpeg_utility.h"
#include "media/media_common.h"
#include "base/debug_log.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace FFmpeg {
namespace {

constexpr auto kMaxArea = 1920 * 1080 * 4;

using ::Media::ValidFrameSize;

[[nodiscard]] bool ValidSourcePlanes(not_null<AVFrame*> frame, int format) {
	const auto pixelFormat = AVPixelFormat(format);
	const auto desc = av_pix_fmt_desc_get(pixelFormat);
	auto visible = std::array<int, 4>{};
	if (!desc
		|| av_image_fill_linesizes(
			visible.data(),
			pixelFormat,
			frame->width) < 0) {
		return false;
	}
	const auto palette = (desc->flags & AV_PIX_FMT_FLAG_PAL) != 0;
	const auto maxPlaneBytes = int64_t(std::numeric_limits<int>::max())
		/ kPixelBytesSize;
	for (auto plane = 0; plane != int(visible.size()); ++plane) {
		const auto data = frame->data[plane];
		const auto stride = frame->linesize[plane];
		if (palette && plane == 1) {
			if (!data) {
				return false;
			}
			continue;
		} else if (!visible[plane]) {
			continue;
		} else if (!data
			|| !stride
			|| stride == std::numeric_limits<int>::min()
			|| std::abs(stride) < visible[plane]) {
			return false;
		}
		const auto shift = (plane == 1 || plane == 2)
			? desc->log2_chroma_h
			: 0;
		const auto rows = AV_CEIL_RSHIFT(frame->height, shift);
		if (int64_t(rows) * std::abs(stride) > maxPlaneBytes) {
			return false;
		}
	}
	return true;
}

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
	QImage _rotationStorage;

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

	if (!_format) {
		return;
	}
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
	_codec = MakeCodecPointer({
		.stream = info,
		.hwAllowed = false,
		.videoMaxArea = kMaxArea,
	});
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
	if (width <= 0 || height <= 0 || size.isEmpty()) {
		LOG(("Webm Error: Bad frame size %1x%2 -> %3x%4"
			).arg(width
			).arg(height
			).arg(size.width()
			).arg(size.height()));
		return {};
	}

	if (rotationSwapWidthHeight()) {
		size.transpose();
	}
	const auto srcSize = QSize(width, height);
	// Qt narrows the expanding side to int, so check it wide before scaling.
	const auto expanding = (mode == Qt::KeepAspectRatioByExpanding)
		&& ((int64_t(size.height()) * width / height > size.width())
			|| (int64_t(size.width()) * height / width > size.height()));
	const auto scaled = expanding ? QSize() : srcSize.scaled(size, mode);
	if (scaled.isEmpty()
		|| scaled.width() > size.width()
		|| scaled.height() > size.height()) {
		LOG(("Webm Error: Bad scaled frame size %1x%2 -> %3x%4"
			).arg(width
			).arg(height
			).arg(scaled.width()
			).arg(scaled.height()));
		return {};
	}
	const auto srcFormat = (frame->format == AV_PIX_FMT_NONE)
		? _codec->pix_fmt
		: frame->format;
	if (!ValidSourcePlanes(frame, srcFormat)) {
		LOG(("Webm Error: Bad frame planes, format %1.").arg(srcFormat));
		return {};
	}
	const auto bgra = (srcFormat == AV_PIX_FMT_BGRA);
	const auto bgr0 = (srcFormat == AV_PIX_FMT_BGR0);
	const auto withAlpha = bgra || (srcFormat == AV_PIX_FMT_YUVA420P);
	const auto native = (bgra || bgr0) && (srcSize == scaled);
	const auto dx = (size.width() - scaled.width()) / 2;
	const auto dy = (size.height() - scaled.height()) / 2;
	const auto aligned = [](int64_t bytes) {
		return (bytes + kAlignImageBy - 1) / kAlignImageBy * kAlignImageBy;
	};
	const auto canvasBytes = int64_t(size.width()) * kPixelBytesSize;
	const auto visibleBytes = int64_t(scaled.width()) * kPixelBytesSize;
	const auto shiftBytes = int64_t(dx) * kPixelBytesSize;
	// WHY: swscale's BGRA kernels round each row's write up to kAlignImageBy
	// bytes from the shifted start, so a centered row needs that rounded span
	// after the shift, or the last row runs past the storage.
	const auto neededPitch = native
		? aligned(canvasBytes)
		: std::max(
			aligned(canvasBytes),
			aligned(shiftBytes + aligned(visibleBytes)));
	if (neededPitch > std::numeric_limits<int>::max()) {
		LOG(("Webm Error: Bad frame pitch %1.").arg(neededPitch));
		return {};
	}
	const auto minPitch = int(neededPitch);

	const auto rotated = (_rotation != 0);
	auto working = rotated
		? base::take(_rotationStorage)
		: std::move(storage);
	if (!GoodStorageForFrame(working, size, minPitch)) {
		working = CreateFrameStorage(size, minPitch);
	}
	auto bits = working.bits();
	if (bits && !GoodStorageForFrame(working, size, minPitch)) {
		working = CreateFrameStorage(size, minPitch);
		bits = working.bits();
	}
	if (!bits || !GoodStorageForFrame(working, size, minPitch)) {
		LOG(("Webm Error: Bad frame storage %1x%2."
			).arg(size.width()
			).arg(size.height()));
		return {};
	}
	const auto pitch = int(working.bytesPerLine());
	const auto line = int(visibleBytes);
	const auto first = bits + (int64_t(dy) * pitch + shiftBytes);
	const auto src = frame->data[0];
	const auto srcStride = frame->linesize[0];
	if (native && bgra) {
		for (auto y = 0; y != height; ++y) {
			memcpy(
				first + int64_t(y) * pitch,
				src + int64_t(y) * srcStride,
				line);
		}
	} else if (native) {
		static_assert(sizeof(uint32) == kPixelBytesSize);
		for (auto y = 0; y != height; ++y) {
			auto to = reinterpret_cast<uint32*>(first + int64_t(y) * pitch);
			auto from = reinterpret_cast<const uint32*>(
				src + int64_t(y) * srcStride);
			for (auto x = 0; x != width; ++x) {
				*to++ = 0xFF000000U | *from++;
			}
		}
	} else {
		_scale = MakeSwscalePointer(
			srcSize,
			srcFormat,
			scaled,
			AV_PIX_FMT_BGRA,
			&_scale);
		if (!_scale) {
			LOG(("Webm Error: Unable to create sws context."));
			return {};
		}

		// AV_NUM_DATA_POINTERS defined in AVFrame struct
		uint8_t *dstData[AV_NUM_DATA_POINTERS] = { first, nullptr };
		int dstLinesize[AV_NUM_DATA_POINTERS] = { pitch, 0 };
		const auto converted = sws_scale(
			_scale.get(),
			frame->data,
			frame->linesize,
			0,
			height,
			dstData,
			dstLinesize);
		if (converted != scaled.height()) {
			LOG(("Webm Error: Bad sws_scale result %1.").arg(converted));
			return {};
		}
	}
	const auto gap = pitch - line;
	const auto lastRow = first + int64_t(scaled.height() - 1) * pitch;
	const auto end = bits + int64_t(size.height()) * pitch;
	memset(bits, 0, first - bits);
	if (gap > 0) {
		for (auto row = first; row != lastRow; row += pitch) {
			memset(row + line, 0, gap);
		}
	}
	memset(lastRow + line, 0, end - (lastRow + line));
	if (withAlpha) {
		PremultiplyInplace(working);
	}

	const auto duration = _next.frame
		? (_next.position - _current.position)
		: _current.duration;
	if (!rotated) {
		return {
			.duration = duration,
			.image = std::move(working),
			.last = !_next.frame,
		};
	}
	auto transform = QTransform();
	transform.rotate(_rotation);
	// Qt's rotated output has an ordinary pitch, so the canvas stays private.
	_rotationStorage = std::move(working);
	auto image = _rotationStorage.transformed(transform);
	if (image.isNull()) {
		LOG(("Webm Error: Unable to rotate frame %1x%2."
			).arg(size.width()
			).arg(size.height()));
		return {};
	}
	return {
		.duration = duration,
		.image = std::move(image),
		.last = !_next.frame,
	};
}

FrameGenerator::Frame FrameGenerator::Impl::renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	if (!_codec) {
		return {};
	} else if (!_current.frame) {
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
	if (!_codec) {
		return;
	}
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
	const auto duration = _next.frame->duration;
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
			if (!ValidFrameSize(frame->width, frame->height, kMaxArea)) {
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
