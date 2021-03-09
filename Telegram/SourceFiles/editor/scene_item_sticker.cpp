/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene_item_sticker.h"

#include "app.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "styles/style_editor.h"

namespace Editor {
namespace {

} // namespace

ItemSticker::ItemSticker(
	not_null<DocumentData*> document,
	std::shared_ptr<float64> zPtr,
	int size,
	int x,
	int y)
: ItemBase(std::move(zPtr), size, x, y)
, _document(document)
, _mediaView(_document->createMediaView())
, _thumbnailMargins(st::photoEditorItemStickerPadding) {
	const auto stickerData = document->sticker();
	if (!stickerData) {
		return;
	}
	const auto updateThumbnail = [=] {
		const auto guard = gsl::finally([&] {
			setAspectRatio(_pixmap.isNull()
				? 1.0
				: (_pixmap.height() / float64(_pixmap.width())));
		});
		if (stickerData->animated) {
			_lottie.player = ChatHelpers::LottiePlayerFromDocument(
				_mediaView.get(),
				ChatHelpers::StickerLottieSize::MessageHistory,
				QSize(kStickerSideSize, kStickerSideSize)
					* cIntRetinaFactor(),
				Lottie::Quality::High);
			_lottie.player->updates(
			) | rpl::start_with_next([=] {
				_pixmap = App::pixmapFromImageInPlace(
					_lottie.player->frame());
				_lottie.player = nullptr;
				_lottie.lifetime.destroy();
				update();
			}, _lottie.lifetime);
			return true;
		}
		const auto sticker = _mediaView->getStickerLarge();
		if (!sticker) {
			return false;
		}
		_pixmap = sticker->pixNoCache(
			sticker->width() * cIntRetinaFactor(),
			sticker->height() * cIntRetinaFactor(),
			Images::Option::Smooth);
		_pixmap.setDevicePixelRatio(cRetinaFactor());
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

void ItemSticker::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	p->drawPixmap((innerRect() - _thumbnailMargins).toRect(), _pixmap);
	ItemBase::paint(p, option, w);
}

MTPInputDocument ItemSticker::sticker() const {
	return _document->mtpInput();
}

int ItemSticker::type() const {
	return Type;
}

} // namespace Editor
