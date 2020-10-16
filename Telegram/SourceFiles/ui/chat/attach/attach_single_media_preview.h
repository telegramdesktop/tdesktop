/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "media/clip/media_clip_reader.h"

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace Ui {

struct PreparedFile;

class SingleMediaPreview : public RpWidget {
public:
	static SingleMediaPreview *Create(
		QWidget *parent,
		Fn<bool()> gifPaused,
		const PreparedFile &file);

	SingleMediaPreview(
		QWidget *parent,
		Fn<bool()> gifPaused,
		QImage preview,
		bool animated,
		bool sticker,
		const QString &animatedPreviewPath);
	~SingleMediaPreview();

	bool canSendAsPhoto() const {
		return _canSendAsPhoto;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void preparePreview(
		QImage preview,
		const QString &animatedPreviewPath);
	void prepareAnimatedPreview(const QString &animatedPreviewPath);
	void clipCallback(Media::Clip::Notification notification);

	Fn<bool()> _gifPaused;
	bool _animated = false;
	bool _sticker = false;
	bool _canSendAsPhoto = false;
	QPixmap _preview;
	int _previewLeft = 0;
	int _previewWidth = 0;
	int _previewHeight = 0;
	Media::Clip::ReaderPointer _gifPreview;
	std::unique_ptr<Lottie::SinglePlayer> _lottiePreview;

};

} // namespace Ui
