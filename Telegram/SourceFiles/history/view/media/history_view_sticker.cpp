/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_sticker.h"

#include "layout.h"
#include "boxes/sticker_set_box.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/image/image.h"
#include "ui/emoji_config.h"
#include "main/main_session.h"
#include "main/main_app_config.h"
#include "mainwindow.h" // App::wnd()->sessionController.
#include "window/window_session_controller.h" // isGifPausedAtLeastFor.
#include "data/data_session.h"
#include "data/data_document.h"
#include "lottie/lottie_single_player.h"
#include "styles/style_history.h"

namespace HistoryView {
namespace {

double GetEmojiStickerZoom(not_null<Main::Session*> session) {
	return session->appConfig().get<double>("emojies_animated_zoom", 0.625);
}

} // namespace

Sticker::Sticker(
	not_null<Element*> parent,
	not_null<DocumentData*> document,
	const Lottie::ColorReplacements *replacements)
: _parent(parent)
, _document(document)
, _replacements(replacements) {
	_document->loadThumbnail(parent->data()->fullId());
}

Sticker::~Sticker() {
	unloadLottie();
}

bool Sticker::isEmojiSticker() const {
	return (_parent->data()->media() == nullptr);
}

QSize Sticker::size() {
	_size = _document->dimensions;
	if (isEmojiSticker()) {
		constexpr auto kIdealStickerSize = 512;
		const auto zoom = GetEmojiStickerZoom(&_document->session());
		const auto convert = [&](int size) {
			return int(size * st::maxStickerSize * zoom / kIdealStickerSize);
		};
		_size = QSize(convert(_size.width()), convert(_size.height()));
	} else {
		_size = DownscaledSize(
			_size,
			{ st::maxStickerSize, st::maxStickerSize });
	}
	return _size;
}

void Sticker::draw(Painter &p, const QRect &r, bool selected) {
	const auto sticker = _document->sticker();
	if (!sticker) {
		return;
	}

	_document->checkStickerLarge();
	const auto loaded = _document->loaded();
	if (sticker->animated && !_lottie && loaded) {
		setupLottie();
	}

	if (_lottie && _lottie->ready()) {
		paintLottie(p, r, selected);
	} else if (!sticker->animated || !_replacements) {
		paintPixmap(p, r, selected);
	}
}

void Sticker::paintLottie(Painter &p, const QRect &r, bool selected) {
	auto request = Lottie::FrameRequest();
	request.box = _size * cIntRetinaFactor();
	if (selected) {
		request.colored = st::msgStickerOverlay->c;
	}
	const auto frame = _lottie->frameInfo(request);
	const auto size = frame.image.size() / cIntRetinaFactor();
	p.drawImage(
		QRect(
			QPoint(
				r.x() + (r.width() - size.width()) / 2,
				r.y() + (r.height() - size.height()) / 2),
			size),
		frame.image);

	const auto paused = App::wnd()->sessionController()->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
	const auto playOnce = isEmojiSticker()
		|| !_document->session().settings().loopAnimatedStickers();
	if (!paused
		&& (!playOnce || frame.index != 0 || !_lottieOncePlayed)
		&& _lottie->markFrameShown()
		&& playOnce
		&& !_lottieOncePlayed) {
		_lottieOncePlayed = true;
		_parent->delegate()->elementStartStickerLoop(_parent);
	}
}

void Sticker::paintPixmap(Painter &p, const QRect &r, bool selected) {
	const auto pixmap = paintedPixmap(selected);
	if (!pixmap.isNull()) {
		p.drawPixmap(
			QPoint(
				r.x() + (r.width() - _size.width()) / 2,
				r.y() + (r.height() - _size.height()) / 2),
			pixmap);
	}
}

QPixmap Sticker::paintedPixmap(bool selected) const {
	const auto o = _parent->data()->fullId();
	const auto w = _size.width();
	const auto h = _size.height();
	const auto &c = st::msgStickerOverlay;
	const auto good = _document->goodThumbnail();
	if (good && !good->loaded()) {
		good->load({});
	}
	if (const auto image = _document->getStickerLarge()) {
		return selected
			? image->pixColored(o, c, w, h)
			: image->pix(o, w, h);
	//
	// Inline thumbnails can't have alpha channel.
	//
	//} else if (const auto blurred = _document->thumbnailInline()) {
	//	return selected
	//		? blurred->pixBlurredColored(o, c, w, h)
	//		: blurred->pixBlurred(o, w, h);
	} else if (good && good->loaded()) {
		return selected
			? good->pixColored(o, c, w, h)
			: good->pix(o, w, h);
	} else if (const auto thumbnail = _document->thumbnail()) {
		return selected
			? thumbnail->pixBlurredColored(o, c, w, h)
			: thumbnail->pixBlurred(o, w, h);
	}
	return QPixmap();
}

void Sticker::refreshLink() {
	if (_link) {
		return;
	}
	const auto sticker = _document->sticker();
	if (isEmojiSticker()) {
		const auto weak = base::make_weak(this);
		_link = std::make_shared<LambdaClickHandler>([weak] {
			const auto that = weak.get();
			if (!that) {
				return;
			}
			that->_lottieOncePlayed = false;
			that->_parent->data()->history()->owner().requestViewRepaint(
				that->_parent);
		});
	} else if (sticker && sticker->set.type() != mtpc_inputStickerSetEmpty) {
		_link = std::make_shared<LambdaClickHandler>([document = _document] {
			StickerSetBox::Show(App::wnd()->sessionController(), document);
		});
	}
}

void Sticker::setupLottie() {
	_lottie = Stickers::LottiePlayerFromDocument(
		_document,
		_replacements,
		Stickers::LottieSize::MessageHistory,
		_size * cIntRetinaFactor(),
		Lottie::Quality::High);
	_parent->data()->history()->owner().registerHeavyViewPart(_parent);

	_lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		update.data.match([&](const Lottie::Information &information) {
			_parent->data()->history()->owner().requestViewResize(_parent);
		}, [&](const Lottie::DisplayFrameRequest &request) {
			_parent->data()->history()->owner().requestViewRepaint(_parent);
		});
	}, _lifetime);
}

void Sticker::unloadLottie() {
	if (!_lottie) {
		return;
	}
	_lottie = nullptr;
	_parent->data()->history()->owner().unregisterHeavyViewPart(_parent);
}

} // namespace HistoryView
