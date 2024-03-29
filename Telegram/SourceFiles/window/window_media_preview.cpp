/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_media_preview.h"

#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "history/view/media/history_view_sticker.h"
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

constexpr auto kStickerPreviewEmojiLimit = 10;
constexpr auto kPremiumShift = 21. / 240;
constexpr auto kPremiumMultiplier = (1 + 0.245 * 2);
constexpr auto kPremiumDownscale = 1.25;

} // namespace

MediaPreviewWidget::MediaPreviewWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _emojiSize(Ui::Emoji::GetSizeLarge() / style::DevicePixelRatio()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		if (_document && _document->emojiUsesTextColor()) {
			_cache = QPixmap();
		}
	}, lifetime());
}

QRect MediaPreviewWidget::updateArea() const {
	const auto size = currentDimensions();
	const auto position = QPoint(
		(width() - size.width()) / 2,
		(height() - size.height()) / 2);
	const auto premium = _document && _document->isPremiumSticker();
	const auto adjusted = position
		- (premium
			? QPoint(size.width() - (size.width() / 2), size.height() / 2)
			: QPoint());
	return QRect(adjusted, size * (premium ? 2 : 1));
}

void MediaPreviewWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto r = e->rect();
	const auto factor = style::DevicePixelRatio();
	const auto dimensions = currentDimensions();
	const auto frame = (_lottie && _lottie->ready())
		? _lottie->frameInfo({
			.box = dimensions * factor,
			.colored = ((_document && _document->emojiUsesTextColor())
				? st::windowFg->c
				: QColor(0, 0, 0, 0)),
		})
		: Lottie::Animation::FrameInfo();
	const auto effect = (_effect && _effect->ready())
		? _effect->frameInfo({ dimensions * kPremiumMultiplier * factor })
		: Lottie::Animation::FrameInfo();
	const auto image = frame.image;
	const auto effectImage = effect.image;
	//const auto framesCount = !image.isNull() ? _lottie->framesCount() : 1;
	//const auto effectsCount = !effectImage.isNull()
	//	? _effect->framesCount()
	//	: 1;
	const auto pixmap = image.isNull() ? currentImage() : QPixmap();
	const auto size = image.isNull() ? pixmap.size() : image.size();
	int w = size.width() / factor, h = size.height() / factor;
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
	const auto position = innerPosition({ w, h });
	if (image.isNull()) {
		p.drawPixmap(position, pixmap);
	} else {
		p.drawImage(QRect(position, QSize(w, h)), image);
	}
	if (!effectImage.isNull()) {
		p.drawImage(
			QRect(outerPosition({ w, h }), effectImage.size() / factor),
			effectImage);
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
	if (!frame.image.isNull()/*
		&& (!_effect || ((frame.index % effectsCount) <= effect.index))*/) {
		_lottie->markFrameShown();
	}
	if (!effect.image.isNull()/*
		&& ((effect.index % framesCount) <= frame.index)*/) {
		_effect->markFrameShown();
	}
}

void MediaPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

QPoint MediaPreviewWidget::innerPosition(QSize size) const {
	if (!_document || !_document->isPremiumSticker()) {
		return QPoint(
			(width() - size.width()) / 2,
			(height() - size.height()) / 2);
	}
	const auto outer = size * kPremiumMultiplier;
	const auto shift = size.width() * kPremiumShift;
	return outerPosition(size)
		+ QPoint(
			outer.width() - size.width() - shift,
			(outer.height() - size.height()) / 2);
}

QPoint MediaPreviewWidget::outerPosition(QSize size) const {
	const auto outer = size * kPremiumMultiplier;
	return QPoint(
		(width() - outer.width()) / 2,
		(height() - outer.height()) / 2);
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
	_effect = nullptr;
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
		_cachedSize = _cache.size() * style::DevicePixelRatio();
		return _cachedSize;
	}

	QSize result, box;
	if (_photo) {
		result = QSize(_photo->width(), _photo->height());
		const auto skip = st::defaultBox.margin.top();
		box = QSize(width() - 2 * skip, height() - 2 * skip);
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
			if (_document->isPremiumSticker()) {
				result = (box /= kPremiumDownscale);
			}
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

void MediaPreviewWidget::createLottieIfReady(
		not_null<DocumentData*> document) {
	const auto sticker = document->sticker();
	if (!sticker
		|| !sticker->isLottie()
		|| _lottie
		|| !_documentMedia->loaded()) {
		return;
	} else if (document->isPremiumSticker()
		&& _documentMedia->videoThumbnailContent().isEmpty()) {
		return;
	}
	const_cast<MediaPreviewWidget*>(this)->setupLottie();
}

void MediaPreviewWidget::setupLottie() {
	Expects(_document != nullptr);

	const auto factor = style::DevicePixelRatio();
	if (_document->isPremiumSticker()) {
		const auto size = HistoryView::Sticker::Size(_document);
		_cachedSize = size;
		_lottie = ChatHelpers::LottiePlayerFromDocument(
			_documentMedia.get(),
			nullptr,
			ChatHelpers::StickerLottieSize::MessageHistory,
			size * factor,
			Lottie::Quality::High);
		_effect = _document->session().emojiStickersPack().effectPlayer(
			_document,
			_documentMedia->videoThumbnailContent(),
			QString(),
			true);
	} else {
		const auto size = currentDimensions();
		_lottie = std::make_unique<Lottie::SinglePlayer>(
			Lottie::ReadContent(_documentMedia->bytes(), _document->filepath()),
			Lottie::FrameRequest{ size * factor },
			Lottie::Quality::High);
	}

	const auto handler = [=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &) {
			this->update();
		}, [&](const Lottie::DisplayFrameRequest &) {
			this->update(updateArea());
		});
	};

	_lottie->updates() | rpl::start_with_next(handler, lifetime());
	if (_effect) {
		_effect->updates() | rpl::start_with_next(handler, lifetime());
	}
}

QPixmap MediaPreviewWidget::currentImage() const {
	const auto blur = Images::PrepareArgs{ .options = Images::Option::Blur };
	if (_document) {
		const auto sticker = _document->sticker();
		const auto webm = sticker && sticker->isWebm();
		if (sticker && !webm) {
			if (_cacheStatus != CacheLoaded) {
				const_cast<MediaPreviewWidget*>(this)->createLottieIfReady(
					_document);
				if (_lottie && _lottie->ready()) {
					return QPixmap();
				} else if (const auto image = _documentMedia->getStickerLarge()) {
					QSize s = currentDimensions();
					_cache = image->pix(s);
					_cacheStatus = CacheLoaded;
				} else if (_cacheStatus != CacheThumbLoaded
					&& _document->hasThumbnail()
					&& _documentMedia->thumbnail()) {
					QSize s = currentDimensions();
					_cache = _documentMedia->thumbnail()->pix(s, blur);
					if (_document && _document->emojiUsesTextColor()) {
						_cache = Ui::PixmapFromImage(
							Images::Colored(
								_cache.toImage(),
								st::windowFg->c));
					}
					_cacheStatus = CacheThumbLoaded;
				}
			}
		} else {
			const_cast<MediaPreviewWidget*>(this)->validateGifAnimation();
			const auto &gif = (_gif && _gif->started())
				? _gif
				: _gifThumbnail;
			if (gif && gif->started()) {
				const auto paused = _controller->isGifPausedAtLeastFor(
					Window::GifPauseReason::MediaPreview);
				return QPixmap::fromImage(gif->current(
					{ .frame = currentDimensions(), .keepAlpha = webm },
					paused ? 0 : crl::now()), Qt::ColorOnly);
			}
			if (_cacheStatus != CacheThumbLoaded
				&& _document->hasThumbnail()) {
				QSize s = currentDimensions();
				const auto thumbnail = _documentMedia->thumbnail();
				if (thumbnail) {
					_cache = thumbnail->pix(s, blur);
					_cacheStatus = CacheThumbLoaded;
				} else if (const auto blurred = _documentMedia->thumbnailInline()) {
					_cache = blurred->pix(s, blur);
					_cacheStatus = CacheThumbLoaded;
				}
			}
		}
	} else if (_photo) {
		if (_cacheStatus != CacheLoaded) {
			if (_photoMedia->loaded()) {
				QSize s = currentDimensions();
				_cache = _photoMedia->image(Data::PhotoSize::Large)->pix(s);
				_cacheStatus = CacheLoaded;
			} else {
				_photo->load(_origin);
				if (_cacheStatus != CacheThumbLoaded) {
					QSize s = currentDimensions();
					if (const auto thumbnail = _photoMedia->image(
							Data::PhotoSize::Thumbnail)) {
						_cache = thumbnail->pix(s, blur);
						_cacheStatus = CacheThumbLoaded;
					} else if (const auto small = _photoMedia->image(
							Data::PhotoSize::Small)) {
						_cache = small->pix(s, blur);
						_cacheStatus = CacheThumbLoaded;
					} else if (const auto blurred = _photoMedia->thumbnailInline()) {
						_cache = blurred->pix(s, blur);
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
	gif->start({ .frame = currentDimensions(), .keepAlpha = _gifWithAlpha });
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
	_gifWithAlpha = (_documentMedia->owner()->sticker() != nullptr);
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
	case Notification::Reinit: {
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

	case Notification::Repaint: {
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
