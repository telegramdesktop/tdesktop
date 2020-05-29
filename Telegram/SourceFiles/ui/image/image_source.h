/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image.h"

namespace Images {

class ImageSource : public Source {
public:
	explicit ImageSource(const QString &path);
	explicit ImageSource(const QByteArray &content);
	explicit ImageSource(QImage &&data);

	void load() override;
	QImage takeLoaded() override;

	int width() override;
	int height() override;

private:
	QImage _data;

};

} // namespace Images
