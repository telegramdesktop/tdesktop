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
: ItemAnimated(std::move(data))
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

Media::Encode::AnimatedEntity::Kind ItemSticker::entityKind() const {
	const auto data = _document->sticker();
	return (data && data->isWebm())
		? Media::Encode::AnimatedEntity::Kind::Webm
		: Media::Encode::AnimatedEntity::Kind::Lottie;
}

bool ItemSticker::hasContent() const {
	return !content().isEmpty();
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
	const auto live = (_lottie.player && _lottie.player->ready())
		|| (_webm && _webm->started());
	paintFrame(p, currentFrame(), live, _webm.valid() && flipped());
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
