/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_sticker.h"

#include "chat_helpers/stickers_lottie.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"

namespace Editor {
namespace {

} // namespace

ItemSticker::ItemSticker(
	not_null<DocumentData*> document,
	ItemBase::Data data)
: ItemBase(std::move(data))
, _document(document)
, _mediaView(_document->createMediaView()) {
	const auto stickerData = document->sticker();
	if (!stickerData) {
		return;
	}
	const auto updateThumbnail = [=] {
		const auto guard = gsl::finally([&] {
			if (_image.isNull()) {
				setAspectRatio(1.);
			}
		});
		if (createPlayer()) {
			return true;
		}
		const auto sticker = _mediaView->getStickerLarge();
		if (!sticker) {
			return false;
		}
		const auto ratio = style::DevicePixelRatio();
		auto pixmap = sticker->pixNoCache(sticker->size() * ratio);
		pixmap.setDevicePixelRatio(ratio);
		updatePixmap(pixmap.toImage());
		return true;
	};
	if (!updateThumbnail()) {
		_document->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			if (updateThumbnail()) {
				_loadingLifetime.destroy();
				update();
			}
		}, _loadingLifetime);
	}
}

bool ItemSticker::createPlayer() {
	const auto stickerData = _document->sticker();
	if (!stickerData) {
		return false;
	}
	if (stickerData->isLottie()) {
		_lottie.player = ChatHelpers::LottiePlayerFromDocument(
			_mediaView.get(),
			ChatHelpers::StickerLottieSize::MessageHistory,
			QSize(kStickerSideSize, kStickerSideSize)
				* style::DevicePixelRatio(),
			Lottie::Quality::High);
		_lottie.player->updates(
		) | rpl::on_next([=] {
			if (_image.isNull()) {
				updatePixmap(_lottie.player->frame());
			}
			update();
		}, _lottie.lifetime);
		return true;
	} else if (stickerData->isWebm()
		&& !_document->dimensions.isEmpty()) {
		const auto callback = [=](::Media::Clip::Notification value) {
			clipCallback(value);
		};
		_webm = ::Media::Clip::MakeReader(
			_mediaView->owner()->location(),
			_mediaView->bytes(),
			callback);
		return true;
	}
	return false;
}

void ItemSticker::releasePlayers() {
	if (!animated()) {
		return;
	}
	_loopDuration = loopDuration();
	_releasedAnimation = true;
	_pendingRecreate = true;
	_lottie.lifetime.destroy();
	_lottie.player = nullptr;
	_webm.reset();
}

void ItemSticker::setStatus(Status status) {
	if (status != Status::Normal) {
		releasePlayers();
	}
	ItemBase::setStatus(status);
}

void ItemSticker::updatePixmap(QImage &&image) {
	_image = std::move(image);
	if (flipped()) {
		performFlip();
	} else {
		update();
	}
	if (!_image.isNull()) {
		setAspectRatio(_image.height() / float64(_image.width()));
	}
}

void ItemSticker::clipCallback(::Media::Clip::Notification notification) {
	using namespace ::Media::Clip;
	if (notification == Notification::Reinit) {
		if (_webm && _webm->state() == State::Error) {
			_webm.setBad();
		} else if (_webm && _webm->ready() && !_webm->started()) {
			_webm->start({
				.frame = _document->dimensions,
				.keepAlpha = true,
			});
		}
	}
	if (_webm && _webm->started() && _image.isNull()) {
		updatePixmap(_webm->current(
			{ .frame = _document->dimensions, .keepAlpha = true },
			0));
	}
	update();
}

bool ItemSticker::animated() const {
	return (_lottie.player != nullptr) || _webm.valid() || _releasedAnimation;
}

Media::Encode::AnimatedEntity ItemSticker::animatedEntity(
		const QTransform &sceneToCanvas) const {
	const auto data = _document->sticker();
	const auto composed = QTransform().scale(flipped() ? -1. : 1., 1.)
		* sceneTransform()
		* sceneToCanvas;
	const auto inner = contentRect();
	const auto m11 = composed.m11();
	const auto m12 = composed.m12();
	const auto m21 = composed.m21();
	const auto m22 = composed.m22();
	const auto scale = std::hypot(m11, m12);
	const auto mirrored = ((m11 * m22 - m12 * m21) < 0);
	const auto rotation = mirrored
		? (std::atan2(-m12, m22) * 180. / M_PI)
		: (std::atan2(m12, m11) * 180. / M_PI);
	const auto size = inner.size() * scale;
	const auto center = composed.map(inner.center());
	return {
		.kind = ((data && data->isWebm())
			? Media::Encode::AnimatedEntity::Kind::Webm
			: Media::Encode::AnimatedEntity::Kind::Lottie),
		.bytes = content(),
		.geometry = QRectF(
			center - QPointF(size.width() / 2., size.height() / 2.),
			size),
		.rotation = rotation,
		.flipped = mirrored,
	};
}

QByteArray ItemSticker::content() const {
	const auto &bytes = _mediaView->bytes();
	if (!bytes.isEmpty()) {
		return QByteArray(bytes.constData(), bytes.size());
	}
	auto file = QFile(_document->filepath(true));
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

crl::time ItemSticker::loopDuration() const {
	if (_lottie.player && _lottie.player->ready()) {
		const auto information = _lottie.player->information();
		if (information.frameRate > 0) {
			return crl::time(base::SafeRound(
				information.framesCount * 1000. / information.frameRate));
		}
	}
	return _loopDuration;
}

QImage ItemSticker::currentFrame() {
	if (_lottie.player && _lottie.player->ready()) {
		auto request = Lottie::FrameRequest();
		request.box = QSize(kStickerSideSize, kStickerSideSize)
			* style::DevicePixelRatio();
		request.mirrorHorizontal = flipped();
		auto result = _lottie.player->frame(request);
		_lottie.player->markFrameShown();
		return result;
	} else if (_webm && _webm->started()) {
		auto result = _webm->current(
			{ .frame = _document->dimensions, .keepAlpha = true },
			crl::now());
		_webm->moveToNextFrame();
		if (!result.isNull()) {
			return result;
		}
	}
	return _image;
}

void ItemSticker::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	if (_pendingRecreate && w) {
		_pendingRecreate = false;
		createPlayer();
	}
	const auto rect = contentRect();
	const auto image = currentFrame();
	if (!image.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		const auto fitted = QSizeF(image.size())
			.scaled(rect.size(), Qt::KeepAspectRatio);
		const auto resultRect = QRectF(rect.topLeft(), fitted).translated(
			(rect.width() - fitted.width()) / 2.,
			(rect.height() - fitted.height()) / 2.);
		const auto live = (_lottie.player && _lottie.player->ready())
			|| (_webm && _webm->started());
		if (live) {
			p->save();
			p->setRenderHint(QPainter::SmoothPixmapTransform);
			if (_webm.valid() && flipped()) {
				p->translate(resultRect.center().x(), 0);
				p->scale(-1., 1.);
				p->translate(-resultRect.center().x(), 0);
			}
			p->drawImage(resultRect, image);
			p->restore();
		} else {
			auto pixelSize = (fitted * ratio).toSize();
			if (pixelSize.width() > image.width()) {
				pixelSize = image.size();
			}
			const auto mirror = _webm.valid() && flipped();
			if ((_preview.key != image.cacheKey())
				|| (_preview.size != pixelSize)
				|| (_preview.flipped != mirror)) {
				_preview.image = image.scaled(
					pixelSize,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
				if (mirror) {
					_preview.image = _preview.image.mirrored(true, false);
				}
				_preview.image.setDevicePixelRatio(ratio);
				_preview.key = image.cacheKey();
				_preview.size = pixelSize;
				_preview.flipped = mirror;
			}
			p->drawImage(resultRect, _preview.image);
		}
	}
	ItemBase::paint(p, option, w);
}

not_null<DocumentData*> ItemSticker::sticker() const {
	return _document;
}

int ItemSticker::type() const {
	return Type;
}

void ItemSticker::performFlip() {
	_image = _image.transformed(QTransform().scale(-1, 1));
	update();
}

std::shared_ptr<ItemBase> ItemSticker::duplicate(ItemBase::Data data) const {
	return std::make_shared<ItemSticker>(_document, std::move(data));
}

} // namespace Editor
