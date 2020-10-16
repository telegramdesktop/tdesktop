/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

struct PreparedFile;

class SingleFilePreview final : public RpWidget {
public:
	SingleFilePreview(
		QWidget *parent,
		const PreparedFile &file);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void preparePreview(const PreparedFile &file);
	void prepareThumb(const QImage &preview);

	QPixmap _fileThumb;
	Text::String _nameText;
	bool _fileIsAudio = false;
	bool _fileIsImage = false;
	QString _statusText;
	int _statusWidth = 0;

};

} // namespace Ui
