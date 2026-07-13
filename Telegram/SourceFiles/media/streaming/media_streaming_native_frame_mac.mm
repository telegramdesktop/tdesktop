/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_native_frame_mac.h"

#ifdef Q_OS_MAC

#include "media/streaming/media_streaming_common.h"
#include "ffmpeg/ffmpeg_utility.h"

#include <CoreVideo/CoreVideo.h>

namespace Media::Streaming {
namespace {

class PixelBufferLock final {
public:
	PixelBufferLock(CVPixelBufferRef buffer, CVPixelBufferLockFlags flags)
	: _buffer(buffer)
	, _flags(flags)
	, _locked(CVPixelBufferLockBaseAddress(buffer, flags) == kCVReturnSuccess) {
	}

	PixelBufferLock(const PixelBufferLock &) = delete;
	PixelBufferLock &operator=(const PixelBufferLock &) = delete;
	PixelBufferLock(PixelBufferLock &&) = delete;
	PixelBufferLock &operator=(PixelBufferLock &&) = delete;

	~PixelBufferLock() {
		if (_locked) {
			CVPixelBufferUnlockBaseAddress(_buffer, _flags);
		}
	}

	[[nodiscard]] bool locked() const {
		return _locked;
	}

private:
	CVPixelBufferRef _buffer;
	CVPixelBufferLockFlags _flags;
	bool _locked;

};

} // namespace

QImage ConvertNativeFrameToARGB32(const NativeFrame &frame) {
	if (!frame.pixelBuffer || frame.size.isEmpty()) {
		return QImage();
	}
	const auto pixelBuffer = static_cast<CVPixelBufferRef>(frame.pixelBuffer);
	const auto format = CVPixelBufferGetPixelFormatType(pixelBuffer);
	if (format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
		&& format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
		return QImage();
	}
	const auto lock = PixelBufferLock(
		pixelBuffer,
		kCVPixelBufferLock_ReadOnly);
	if (!lock.locked()) {
		return QImage();
	}

	const auto y = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
	const auto uv = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
	if (!y || !uv) {
		return QImage();
	}
	const auto yStride = int(
		CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0));
	const auto uvStride = int(
		CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1));
	if (yStride <= 0 || uvStride <= 0) {
		return QImage();
	}

	auto storage = FFmpeg::CreateFrameStorage(frame.size);
	if (storage.isNull() || !storage.bits()) {
		return QImage();
	}
	const auto swscale = FFmpeg::MakeSwscalePointer(
		frame.size,
		AV_PIX_FMT_NV12,
		frame.size,
		AV_PIX_FMT_BGRA);
	if (!swscale) {
		return QImage();
	}

	const uint8_t *srcData[AV_NUM_DATA_POINTERS] = {
		static_cast<const uint8_t*>(y),
		static_cast<const uint8_t*>(uv),
		nullptr,
		nullptr,
	};
	int srcLinesize[AV_NUM_DATA_POINTERS] = {
		yStride,
		uvStride,
		0,
		0,
	};
	uint8_t *dstData[AV_NUM_DATA_POINTERS] = {
		storage.bits(),
		nullptr,
	};
	int dstLinesize[AV_NUM_DATA_POINTERS] = {
		int(storage.bytesPerLine()),
		0,
	};
	sws_scale(
		swscale.get(),
		srcData,
		srcLinesize,
		0,
		frame.size.height(),
		dstData,
		dstLinesize);

	return storage;
}

} // namespace Media::Streaming

#endif // Q_OS_MAC
