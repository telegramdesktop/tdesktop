/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef Q_OS_MAC

#include <QtCore/QSize>

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

class QRhi;
class QRhiTexture;

namespace Media::View {

class MetalTextureCache final {
public:
	MetalTextureCache();
	~MetalTextureCache();

	bool createTexturesFromPixelBuffer(
		QRhi *rhi,
		void *cvPixelBuffer,
		QRhiTexture **yTexture,
		QRhiTexture **uvTexture,
		QSize *lumaSize,
		QSize *chromaSize);

	void flush();

private:
	struct Private;
	std::unique_ptr<Private> _private;

};

} // namespace Media::View

#endif // Qt >= 6.7

#endif // Q_OS_MAC
