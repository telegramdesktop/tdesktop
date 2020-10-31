/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_media_preview.h"

#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "ui/image/image.h"
#include "ui/emoji_config.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"

namespace Window {
namespace {

constexpr int kStickerPreviewEmojiLimit = 10;

} // namespace

MediaPreviewWidget::MediaPreviewWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _emojiSize(Ui::Emoji::GetSizeLarge() / cIntRetinaFactor()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());
}

QRect MediaPreviewWidget::updateArea() const {
	const auto size = currentDimensions();
	return QRect(
		QPoint((width() - size.width()) / 2, (height() - size.height()) / 2),
		size);
}

void MediaPreviewWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());

	const auto image = [&] {
		if (!_lottie || !_lottie->ready()) {
			return QImage();
		}
		_lottie->markFrameShown();
		return _lottie->frame();
	}();
	const auto pixmap = image.isNull() ? currentImage() : QPixmap();
	const auto size = image.isNull() ? pixmap.size() : image.size();
	int w = size.width() / cIntRetinaFactor(), h = size.height() / cIntRetinaFactor();
	auto shown = _a_shown.value(_hiding ? 0. : 1.);
	if (!_a_shown.animating()) {
		if (_hiding) {
			hide();
			_controller->disableGifPauseReason(Window::GifPauseReason::MediaPreview);
			return;
		}
	} else {
		p.setOpacity(shown);
//		w = qMax(qRound(w * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(w % 2), 1);
//		h = qMax(qRound(h * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(h % 2), 1);
	}
	p.fillRect(r, st::stickerPreviewBg);
	if (image.isNull()) {
		p.drawPixmap((width() - w) / 2, (height() - h) / 2, pixmap);
	} else {
		p.drawImage(
			QRect((width() - w) / 2, (height() - h) / 2, w, h),
			image);
	}
	if (!_emojiList.empty()) {
		const auto emojiCount = _emojiList.size();
		const auto emojiWidth = (emojiCount * _emojiSize) + (emojiCount - 1) * st::stickerEmojiSkip;
		auto emojiLeft = (width() - emojiWidth) / 2;
		const auto esize = Ui::Emoji::GetSizeLarge();
		for (const auto emoji : _emojiList) {
			Ui::Emoji::Draw(
				p,
				emoji,
				esize,
				emojiLeft,
				(height() - h) / 2 - (_emojiSize * 2));
			emojiLeft += _emojiSize + st::stickerEmojiSkip;
		}
	}
}

void MediaPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

void MediaPreviewWidget::showPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) {
	if (!document
		|| (!document->isAnimation() && !document->sticker())
		|| document->isVideoMessage()) {
		hidePreview();
		return;
	}

	startShow();
	_origin = origin;
	_photo = nullptr;
	_photoMedia = nullptr;
	_document = document;
	_documentMedia = _document->createMediaView();
	_documentMedia->thumbnailWanted(_origin);
	_documentMedia->videoThumbnailWanted(_origin);
	_documentMedia->automaticLoad(_origin, nullptr);
	fillEmojiString();
	resetGifAndCache();
}

void MediaPreviewWidget::showPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) {
	startShow();
	_origin = origin;
	_document = nullptr;
	_documentMedia = nullptr;
	_photo = photo;
	_photoMedia = _photo->createMediaView();
	fillEmojiString();
	resetGifAndCache();
}

void MediaPreviewWidget::startShow() {
	_cache = QPixmap();
	if (isHidden() || _a_shown.animating()) {
		if (isHidden()) {
			show();
			_controller->enableGifPauseReason(Window::GifPauseReason::MediaPreview);
		}
		_hiding = false;
		_a_shown.start([=] { update(); }, 0., 1., st::stickerPreviewDuration);
	} else {
		update();
	}
}

void MediaPreviewWidget::hidePreview() {
	if (isHidden()) {
		return;
	}
	if (_gif || _gifThumbnail) {
		_cache = currentImage();
	}
	_hiding = true;
	_a_shown.start([=] { update(); }, 1., 0., st::stickerPreviewDuration);
	_photo = nullptr;
	_photoMedia = nullptr;
	_document = nullptr;
	_documentMedia = nullptr;
	resetGifAndCache();
}

void MediaPreviewWidget::fillEmojiString() {
	_emojiList.clear();
	if (_photo) {
		return;
	}
	if (auto sticker = _document->sticker()) {
		if (auto list = _document->owner().stickers().getEmojiListFromSet(_document)) {
			_emojiList = std::move(*list);
			while (_emojiList.size() > kStickerPreviewEmojiLimit) {
				_emojiList.pop_back();
			}
		} else if (const auto emoji = Ui::Emoji::Find(sticker->alt)) {
			_emojiList.emplace_back(emoji);
		}
	}
}

void MediaPreviewWidget::resetGifAndCache() {
	_lottie = nullptr;
	_gif.reset();
	_gifThumbnail.reset();
	_gifLastPosition = 0;
	_cacheStatus = CacheNotLoaded;
	_cachedSize = QSize();
}

QSize MediaPreviewWidget::currentDimensions() const {
	if (!_cachedSize.isEmpty()) {
		return _cachedSize;
	}
	if (!_document && !_photo) {
		_cachedSize = QSize(_cache.width() / cIntRetinaFactor(), _cache.height() / cIntRetinaFactor());
		return _cachedSize;
	}

	QSize result, box;
	if (_photo) {
		result = QSize(_photo->width(), _photo->height());
		box = QSize(width() - 2 * st::boxVerticalMargin, height() - 2 * st::boxVerticalMargin);
	} else {
		result = _document->dimensions;
		if (result.isEmpty()) {
			const auto &gif = (_gif && _gif->ready()) ? _gif : _gifThumbnail;
			if (gif && gif->ready()) {
				result = QSize(gif->width(), gif->height());
			}
		}
		if (_document->sticker()) {
			box = QSize(st::maxStickerSize, st::maxStickerSize);
		} else {
			box = QSize(2 * st::maxStickerSize, 2 * st::maxStickerSize);
		}
	}
	result = QSize(qMax(style::ConvertScale(result.width()), 1), qMax(style::ConvertScale(result.height()), 1));
	if (result.width() > box.width()) {
		result.setHeight(qMax((box.width() * result.height()) / result.width(), 1));
		result.setWidth(box.width());
	}
	if (result.height() > box.height()) {
		result.setWidth(qMax((box.height() * result.width()) / result.height(), 1));
		result.setHeight(box.height());
	}
	if (_photo) {
		_cachedSize = result;
	}
	return result;
}

void MediaPreviewWidget::setupLottie() {
	Expects(_document != nullptr);

	_lottie = std::make_unique<Lottie::SinglePlayer>(
		Lottie::ReadContent(_documentMedia->bytes(), _document->filepath()),
		Lottie::FrameRequest{ currentDimensions() * cIntRetinaFactor() },
		Lottie::Quality::High);

	_lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &) {
			this->update();
		}, [&](const Lottie::DisplayFrameRequest &) {
			this->update(updateArea());
		});
	}, lifetime());
}

QPixmap MediaPreviewWidget::currentImage() const {
	if (_document) {
		if (const auto sticker = _document->sticker()) {
			if (_cacheStatus != CacheLoaded) {
				if (sticker->animated && !_lottie && _documentMedia->loaded()) {
					const_cast<MediaPreviewWidget*>(this)->setupLottie();
				}
				if (_lottie && _lottie->ready()) {
					return QPixmap();
				} else if (const auto image = _documentMedia->getStickerLarge()) {
					QSize s = currentDimensions();
					_cache = image->pix(s.width(), s.height());
					_cacheStatus = CacheLoaded;
				} else if (_cacheStatus != CacheThumbLoaded
					&& _document->hasThumbnail()
					&& _documentMedia->thumbnail()) {
					QSize s = currentDimensions();
					_cache = _documentMedia->thumbnail()->pixBlurred(s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				}
			}
		} else {
			const_cast<MediaPreviewWidget*>(this)->validateGifAnimation();
			const auto &gif = (_gif && _gif->started())
				? _gif
				: _gifThumbnail;
			if (gif && gif->started()) {
				auto s = currentDimensions();
				auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::MediaPreview);
				return gif->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : crl::now());
			}
			if (_cacheStatus != CacheThumbLoaded
				&& _document->hasThumbnail()) {
				QSize s = currentDimensions();
				const auto thumbnail = _documentMedia->thumbnail();
				if (thumbnail) {
					_cache = thumbnail->pixBlurred(s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				} else if (const auto blurred = _documentMedia->thumbnailInline()) {
					_cache = blurred->pixBlurred(s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				}
			}
		}
	} else if (_photo) {
		if (_cacheStatus != CacheLoaded) {
			if (_photoMedia->loaded()) {
				QSize s = currentDimensions();
				_cache = _photoMedia->image(Data::PhotoSize::Large)->pix(s.width(), s.height());
				_cacheStatus = CacheLoaded;
			} else {
				_photo->load(_origin);
				if (_cacheStatus != CacheThumbLoaded) {
					QSize s = currentDimensions();
					if (const auto thumbnail = _photoMedia->image(
							Data::PhotoSize::Thumbnail)) {
						_cache = thumbnail->pixBlurred(s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					} else if (const auto small = _photoMedia->image(
							Data::PhotoSize::Small)) {
						_cache = small->pixBlurred(s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					} else if (const auto blurred = _photoMedia->thumbnailInline()) {
						_cache = blurred->pixBlurred(s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					} else {
						_photoMedia->wanted(Data::PhotoSize::Small, _origin);
					}
				}
			}
		}
	}
	return _cache;
}

void MediaPreviewWidget::startGifAnimation(
		const Media::Clip::ReaderPointer &gif) {
	const auto s = currentDimensions();
	gif->start(
		s.width(),
		s.height(),
		s.width(),
		s.height(),
		ImageRoundRadius::None,
		RectPart::None);
}

void MediaPreviewWidget::validateGifAnimation() {
	Expects(_documentMedia != nullptr);

	if (_gifThumbnail && _gifThumbnail->started()) {
		const auto position = _gifThumbnail->getPositionMs();
		if (_gif
			&& _gif->ready()
			&& !_gif->started()
			&& (_gifLastPosition > position)) {
			startGifAnimation(_gif);
			_gifThumbnail.reset();
			_gifLastPosition = 0;
			return;
		} else {
			_gifLastPosition = position;
		}
	} else if (_gif || _gif.isBad()) {
		return;
	}

	const auto contentLoaded = _documentMedia->loaded();
	const auto thumbContent = _documentMedia->videoThumbnailContent();
	const auto thumbLoaded = !thumbContent.isEmpty();
	if (!contentLoaded
		&& (_gifThumbnail || _gifThumbnail.isBad() | !thumbLoaded)) {
		return;
	}
	const auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification);
	};
	if (contentLoaded) {
		_gif = Media::Clip::MakeReader(
			_documentMedia->owner()->location(),
			_documentMedia->bytes(),
			std::move(callback));
	} else {
		_gifThumbnail = Media::Clip::MakeReader(
			thumbContent,
			std::move(callback));
	}
}

void MediaPreviewWidget::clipCallback(
		Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifThumbnail && _gifThumbnail->state() == State::Error) {
			_gifThumbnail.setBad();
		}
		if (_gif && _gif->state() == State::Error) {
			_gif.setBad();
		}

		if (_gif
			&& _gif->ready()
			&& !_gif->started()
			&& (!_gifThumbnail || !_gifThumbnail->started())) {
			startGifAnimation(_gif);
		} else if (!_gif
			&& _gifThumbnail
			&& _gifThumbnail->ready()
			&& !_gifThumbnail->started()) {
			startGifAnimation(_gifThumbnail);
		}
		update();
	} break;

	case NotificationRepaint: {
		if ((_gif && _gif->started() && !_gif->currentDisplayed())
			|| (_gifThumbnail
				&& _gifThumbnail->started()
				&& !_gifThumbnail->currentDisplayed())) {
			update(updateArea());
		}
	} break;
	}
}

MediaPreviewWidget::~MediaPreviewWidget() {
}

} // namespace Window
