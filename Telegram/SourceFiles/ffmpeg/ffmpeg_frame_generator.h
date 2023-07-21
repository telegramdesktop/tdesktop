/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/effects/frame_generator.h"

#include <QtGui/QImage>
#include <memory>

namespace FFmpeg {

class FrameGenerator final : public Ui::FrameGenerator {
public:
	explicit FrameGenerator(const QByteArray &bytes);
	~FrameGenerator();

	int count() override;
	double rate() override;
	Frame renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) override;
	Frame renderCurrent(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) override;
	void jumpToStart() override;

private:
	class Impl;

	std::unique_ptr<Impl> _impl;

};

} // namespace FFmpeg
