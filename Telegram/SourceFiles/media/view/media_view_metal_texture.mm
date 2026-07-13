/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_metal_texture.h"

#ifdef Q_OS_MAC
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include <CoreVideo/CoreVideo.h>
#include <Metal/Metal.h>
#include <rhi/qrhi.h>

namespace Media::View {

struct MetalTextureCache::Private {
	CVMetalTextureCacheRef cache = nullptr;
	CVMetalTextureRef yTextureRef = nullptr;
	CVMetalTextureRef uvTextureRef = nullptr;
	id<MTLDevice> device = nil;
};

MetalTextureCache::MetalTextureCache()
: _private(std::make_unique<Private>()) {
}

MetalTextureCache::~MetalTextureCache() {
	flush();
	if (_private->cache) {
		CFRelease(_private->cache);
	}
}

bool MetalTextureCache::createTexturesFromPixelBuffer(
		QRhi *rhi,
		void *cvPixelBuffer,
		QRhiTexture **yTexture,
		QRhiTexture **uvTexture,
		QSize *lumaSize,
		QSize *chromaSize) {
	if (!cvPixelBuffer || !rhi) {
		return false;
	}

	auto pixelBuffer = static_cast<CVPixelBufferRef>(cvPixelBuffer);
	const auto format = CVPixelBufferGetPixelFormatType(pixelBuffer);
	if (format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
		&& format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
		return false;
	}

	const auto nativeHandles = rhi->nativeHandles();
	if (!nativeHandles) {
		return false;
	}
	auto *mtlDevice = static_cast<id<MTLDevice>>(
		static_cast<const QRhiMetalNativeHandles*>(nativeHandles)->dev);
	if (!mtlDevice) {
		return false;
	}

	if (_private->device != mtlDevice) {
		if (_private->cache) {
			CFRelease(_private->cache);
			_private->cache = nullptr;
		}
		_private->device = mtlDevice;
	}
	if (!_private->cache) {
		auto status = CVMetalTextureCacheCreate(
			kCFAllocatorDefault,
			nil,
			mtlDevice,
			nil,
			&_private->cache);
		if (status != kCVReturnSuccess || !_private->cache) {
			return false;
		}
	}

	flush();

	const auto width = CVPixelBufferGetWidth(pixelBuffer);
	const auto height = CVPixelBufferGetHeight(pixelBuffer);

	CVReturn status = CVMetalTextureCacheCreateTextureFromImage(
		kCFAllocatorDefault,
		_private->cache,
		pixelBuffer,
		nil,
		MTLPixelFormatR8Unorm,
		width,
		height,
		0,
		&_private->yTextureRef);
	if (status != kCVReturnSuccess || !_private->yTextureRef) {
		return false;
	}

	const auto uvWidth = (width + 1) / 2;
	const auto uvHeight = (height + 1) / 2;
	status = CVMetalTextureCacheCreateTextureFromImage(
		kCFAllocatorDefault,
		_private->cache,
		pixelBuffer,
		nil,
		MTLPixelFormatRG8Unorm,
		uvWidth,
		uvHeight,
		1,
		&_private->uvTextureRef);
	if (status != kCVReturnSuccess || !_private->uvTextureRef) {
		CFRelease(_private->yTextureRef);
		_private->yTextureRef = nullptr;
		return false;
	}

	auto *yMtlTexture = CVMetalTextureGetTexture(_private->yTextureRef);
	auto *uvMtlTexture = CVMetalTextureGetTexture(_private->uvTextureRef);
	if (!yMtlTexture || !uvMtlTexture) {
		flush();
		return false;
	}

	*lumaSize = QSize(width, height);
	*chromaSize = QSize(uvWidth, uvHeight);

	if (!*yTexture || (*yTexture)->pixelSize() != *lumaSize) {
		delete *yTexture;
		*yTexture = rhi->newTexture(QRhiTexture::R8, *lumaSize);
	}
	if (!*uvTexture || (*uvTexture)->pixelSize() != *chromaSize) {
		delete *uvTexture;
		*uvTexture = rhi->newTexture(QRhiTexture::RG8, *chromaSize);
	}

	(*yTexture)->createFrom({quint64(yMtlTexture), 0});
	(*uvTexture)->createFrom({quint64(uvMtlTexture), 0});

	return true;
}

void MetalTextureCache::flush() {
	if (_private->yTextureRef) {
		CFRelease(_private->yTextureRef);
		_private->yTextureRef = nullptr;
	}
	if (_private->uvTextureRef) {
		CFRelease(_private->uvTextureRef);
		_private->uvTextureRef = nullptr;
	}
	if (_private->cache) {
		CVMetalTextureCacheFlush(_private->cache, 0);
	}
}

} // namespace Media::View

#endif // Qt >= 6.7
#endif // Q_OS_MAC
