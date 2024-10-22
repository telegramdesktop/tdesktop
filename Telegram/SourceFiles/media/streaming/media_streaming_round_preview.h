/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/dynamic_image.h"

#include "media/clip/media_clip_reader.h"

namespace Media::Streaming {

class RoundPreview final : public Ui::DynamicImage {
public:
	RoundPreview(const QByteArray &bytes, int size);

	std::shared_ptr<DynamicImage> clone() override;

	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	void clipCallback(Clip::Notification notification);

	const QByteArray _bytes;
	Clip::ReaderPointer _reader;
	Fn<void()> _repaint;
	int _size = 0;

};

} // namespace Media::Streaming
