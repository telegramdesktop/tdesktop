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
#include "styles/style_editor.h"

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
			if (_pixmap.isNull()) {
				setAspectRatio(1.);
			}
		});
		if (stickerData->isLottie()) {
			_lottie.player = ChatHelpers::LottiePlayerFromDocument(
				_mediaView.get(),
				ChatHelpers::StickerLottieSize::MessageHistory,
				QSize(kStickerSideSize, kStickerSideSize)
					* cIntRetinaFactor(),
				Lottie::Quality::High);
			_lottie.player->updates(
			) | rpl::start_with_next([=] {
				updatePixmap(Ui::PixmapFromImage(
					_lottie.player->frame()));
				_lottie.player = nullptr;
				_lottie.lifetime.destroy();
				update();
			}, _lottie.lifetime);
			return true;
		} else if (stickerData->isWebm()
			&& !_document->dimensions.isEmpty()) {
			const auto callback = [=](::Media::Clip::Notification) {
				const auto size = _document->dimensions;
				if (_webm && _webm->ready() && !_webm->started()) {
					_webm->start({ .frame = size, .keepAlpha = true });
				}
				if (_webm && _webm->started()) {
					updatePixmap(_webm->current(
						{ .frame = size, .keepAlpha = true },
						0));
					_webm = nullptr;
				}
			};
			_webm = ::Media::Clip::MakeReader(
				_mediaView->owner()->location(),
				_mediaView->bytes(),
				callback);
			return true;
		}
		const auto sticker = _mediaView->getStickerLarge();
		if (!sticker) {
			return false;
		}
		const auto ratio = style::DevicePixelRatio();
		auto pixmap = sticker->pixNoCache(sticker->size() * ratio);
		pixmap.setDevicePixelRatio(ratio);
		updatePixmap(std::move(pixmap));
		return true;
	};
	if (!updateThumbnail()) {
		_document->owner().session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			if (updateThumbnail()) {
				_loadingLifetime.destroy();
				update();
			}
		}, _loadingLifetime);
	}
}

void ItemSticker::updatePixmap(QPixmap &&pixmap) {
	_pixmap = std::move(pixmap);
	if (flipped()) {
		performFlip();
	} else {
		update();
	}
	if (!_pixmap.isNull()) {
		setAspectRatio(_pixmap.height() / float64(_pixmap.width()));
	}
}

void ItemSticker::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	p->drawPixmap(contentRect().toRect(), _pixmap);
	ItemBase::paint(p, option, w);
}

not_null<DocumentData*> ItemSticker::sticker() const {
	return _document;
}

int ItemSticker::type() const {
	return Type;
}

void ItemSticker::performFlip() {
	_pixmap = _pixmap.transformed(QTransform().scale(-1, 1));
	update();
}

std::shared_ptr<ItemBase> ItemSticker::duplicate(ItemBase::Data data) const {
	return std::make_shared<ItemSticker>(_document, std::move(data));
}

} // namespace Editor
