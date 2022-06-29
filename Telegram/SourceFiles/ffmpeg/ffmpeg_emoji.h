/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/frame_generator.h"

#include <QtGui/QImage>

namespace FFmpeg {

class EmojiGenerator final : public Ui::FrameGenerator {
public:
	explicit EmojiGenerator(const QByteArray &bytes);
	~EmojiGenerator();

	int count() override;
	Frame renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) override;

private:
	class Impl;

	std::unique_ptr<Impl> _impl;

};

} // namespace FFmpeg
