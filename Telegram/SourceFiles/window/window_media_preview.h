/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/clip/media_clip_reader.h"
#include "ui/effects/animations.h"
#include "data/data_file_origin.h"
#include "ui/rp_widget.h"

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace Window {

class SessionController;

class MediaPreviewWidget : public Ui::RpWidget, private base::Subscriber {
public:
	MediaPreviewWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void showPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document);
	void showPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo);
	void hidePreview();

	~MediaPreviewWidget();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	QSize currentDimensions() const;
	QPixmap currentImage() const;
	void setupLottie();
	void startShow();
	void fillEmojiString();
	void resetGifAndCache();
	[[nodiscard]] QRect updateArea() const;

	not_null<Window::SessionController*> _controller;

	Ui::Animations::Simple _a_shown;
	bool _hiding = false;
	Data::FileOrigin _origin;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	Media::Clip::ReaderPointer _gif;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;

	int _emojiSize;
	std::vector<not_null<EmojiPtr>> _emojiList;

	void clipCallback(Media::Clip::Notification notification);

	enum CacheStatus {
		CacheNotLoaded,
		CacheThumbLoaded,
		CacheLoaded,
	};
	mutable CacheStatus _cacheStatus = CacheNotLoaded;
	mutable QPixmap _cache;
	mutable QSize _cachedSize;

};

} // namespace Window
