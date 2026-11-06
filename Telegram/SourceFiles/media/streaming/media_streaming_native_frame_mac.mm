/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_native_frame_mac.h"

#ifdef Q_OS_MAC

#include "media/streaming/media_streaming_color.h"
#include "media/streaming/media_streaming_common.h"

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

bool SupportedPixelBufferFormat(uint32 format) {
	return (format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
		|| (format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
		|| HighBitDepthPixelBufferFormat(format);
}

bool HighBitDepthPixelBufferFormat(uint32 format) {
	return (format == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange)
		|| (format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange);
}

QImage ConvertNativeFrameToARGB32(
		const NativeFrame &frame,
		const FrameColor &color) {
	if (!frame.pixelBuffer || frame.size.isEmpty()) {
		return QImage();
	}
	const auto pixelBuffer = static_cast<CVPixelBufferRef>(frame.pixelBuffer);
	const auto format = CVPixelBufferGetPixelFormatType(pixelBuffer);
	if (!SupportedPixelBufferFormat(format)) {
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
	const auto yuv = FrameYUV{
		.size = frame.size,
		.chromaSize = frame.chromaSize,
		.y = { .data = y, .stride = yStride },
		.u = { .data = uv, .stride = uvStride },
		.highBitDepth = HighBitDepthPixelBufferFormat(format),
	};
	return ConvertYUVToARGB32(yuv, true, color);
}

} // namespace Media::Streaming

#endif // Q_OS_MAC
