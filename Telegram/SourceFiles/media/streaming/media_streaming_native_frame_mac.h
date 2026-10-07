/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef Q_OS_MAC

#include <QtGui/QImage>

namespace Media::Streaming {

struct NativeFrame;
struct FrameColor;

[[nodiscard]] bool SupportedPixelBufferFormat(uint32 format);
[[nodiscard]] bool HighBitDepthPixelBufferFormat(uint32 format);
[[nodiscard]] QImage ConvertNativeFrameToARGB32(
	const NativeFrame &frame,
	const FrameColor &color);

} // namespace Media::Streaming

#endif // Q_OS_MAC
