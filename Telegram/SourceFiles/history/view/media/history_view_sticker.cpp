/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_sticker.h"

#include "boxes/sticker_set_box.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "ui/image/image.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/emoji_config.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "window/window_session_controller.h" // isGifPausedAtLeastFor.
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "lottie/lottie_single_player.h"
#include "chat_helpers/stickers_lottie.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxSizeFixed = 512;
constexpr auto kMaxEmojiSizeFixed = 256;

[[nodiscard]] QImage CacheDiceImage(
		const QString &emoji,
		int index,
		const QImage &image) {
	static auto Cache = base::flat_map<std::pair<QString, int>, QImage>();
	const auto key = std::make_pair(emoji, index);
	const auto i = Cache.find(key);
	if (i != end(Cache) && i->second.size() == image.size()) {
		return i->second;
	}
	Cache[key] = image;
	return image;
}

} // namespace

Sticker::Sticker(
	not_null<Element*> parent,
	not_null<DocumentData*> data,
	Element *replacing,
	const Lottie::ColorReplacements *replacements)
: _parent(parent)
, _data(data)
, _replacements(replacements) {
	if ((_dataMedia = _data->activeMediaView())) {
		dataMediaCreated();
	} else {
		_data->loadThumbnail(parent->data()->fullId());
	}
	if (const auto media = replacing ? replacing->media() : nullptr) {
		_lottie = media->stickerTakeLottie(_data, _replacements);
		if (_lottie) {
			lottieCreated();
		}
	}
}

Sticker::~Sticker() {
	if (_lottie || _dataMedia) {
		if (_lottie) {
			unloadLottie();
		}
		if (_dataMedia) {
			_data->owner().keepAlive(base::take(_dataMedia));
			_parent->checkHeavyPart();
		}
	}
}

bool Sticker::isEmojiSticker() const {
	return (_parent->data()->media() == nullptr);
}

void Sticker::initSize() {
	if (isEmojiSticker() || _diceIndex >= 0) {
		_size = Sticker::EmojiSize();
		if (_diceIndex > 0) {
			[[maybe_unused]] bool result = readyToDrawLottie();
		}
	} else {
		_size = DownscaledSize(_data->dimensions, Sticker::Size());
	}
}

QSize Sticker::size() {
	if (_size.isEmpty()) {
		initSize();
	}
	return _size;
}

bool Sticker::readyToDrawLottie() {
	if (!_lastDiceFrame.isNull()) {
		return true;
	}
	const auto sticker = _data->sticker();
	if (!sticker) {
		return false;
	}

	ensureDataMediaCreated();
	_dataMedia->checkStickerLarge();
	const auto loaded = _dataMedia->loaded();
	if (sticker->animated && !_lottie && loaded) {
		setupLottie();
	}
	return (_lottie && _lottie->ready());
}

QSize Sticker::Size() {
	const auto side = std::min(st::maxStickerSize, kMaxSizeFixed);
	return { side, side };
}

QSize Sticker::EmojiSize() {
	const auto side = std::min(st::maxAnimatedEmojiSize, kMaxEmojiSizeFixed);
	return { side, side };
}

void Sticker::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	ensureDataMediaCreated();
	if (readyToDrawLottie()) {
		paintLottie(p, context, r);
	} else if (!_data->sticker()
		|| (_data->sticker()->animated && _replacements)
		|| !paintPixmap(p, context, r)) {
		paintPath(p, context, r);
	}
}

void Sticker::paintLottie(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	auto request = Lottie::FrameRequest();
	request.box = _size * cIntRetinaFactor();
	if (context.selected() && !_nextLastDiceFrame) {
		request.colored = context.st->msgStickerOverlay()->c;
	}
	const auto frame = _lottie
		? _lottie->frameInfo(request)
		: Lottie::Animation::FrameInfo();
	if (_nextLastDiceFrame) {
		_nextLastDiceFrame = false;
		_lastDiceFrame = CacheDiceImage(_diceEmoji, _diceIndex, frame.image);
	}
	const auto &image = _lastDiceFrame.isNull()
		? frame.image
		: _lastDiceFrame;
	const auto prepared = (!_lastDiceFrame.isNull() && context.selected())
		? Images::prepareColored(context.st->msgStickerOverlay()->c, image)
		: image;
	const auto size = prepared.size() / cIntRetinaFactor();
	p.drawImage(
		QRect(
			QPoint(
				r.x() + (r.width() - size.width()) / 2,
				r.y() + (r.height() - size.height()) / 2),
			size),
		prepared);
	if (!_lastDiceFrame.isNull()) {
		return;
	}

	const auto paused = _parent->delegate()->elementIsGifPaused();
	const auto playOnce = (_diceIndex > 0)
		? true
		: (_diceIndex == 0)
		? false
		: (isEmojiSticker()
			|| !Core::App().settings().loopAnimatedStickers());
	const auto count = _lottie->information().framesCount;
	_frameIndex = frame.index;
	_framesCount = count;
	_nextLastDiceFrame = !paused
		&& (_diceIndex > 0)
		&& (frame.index + 2 == count);
	const auto lastDiceFrame = (_diceIndex > 0) && atTheEnd();
	const auto switchToNext = !playOnce
		|| (!lastDiceFrame && (frame.index != 0 || !_lottieOncePlayed));
	if (!paused
		&& switchToNext
		&& _lottie->markFrameShown()
		&& playOnce
		&& !_lottieOncePlayed) {
		_lottieOncePlayed = true;
		_parent->delegate()->elementStartStickerLoop(_parent);
	}
}

bool Sticker::paintPixmap(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	const auto pixmap = paintedPixmap(context);
	if (pixmap.isNull()) {
		return false;
	}
	p.drawPixmap(
		QPoint(
			r.x() + (r.width() - _size.width()) / 2,
			r.y() + (r.height() - _size.height()) / 2),
		pixmap);
	return true;
}

void Sticker::paintPath(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	const auto pathGradient = _parent->delegate()->elementPathShiftGradient();
	if (context.selected()) {
		pathGradient->overrideColors(
			context.st->msgServiceBgSelected(),
			context.st->msgServiceBg());
	} else {
		pathGradient->clearOverridenColors();
	}
	p.setBrush(context.imageStyle()->msgServiceBg);
	ChatHelpers::PaintStickerThumbnailPath(
		p,
		_dataMedia.get(),
		r,
		pathGradient);
}

QPixmap Sticker::paintedPixmap(const PaintContext &context) const {
	const auto w = _size.width();
	const auto h = _size.height();
	const auto &c = context.st->msgStickerOverlay();
	const auto good = _dataMedia->goodThumbnail();
	if (const auto image = _dataMedia->getStickerLarge()) {
		return context.selected()
			? image->pixColored(c, w, h)
			: image->pix(w, h);
	//
	// Inline thumbnails can't have alpha channel.
	//
	//} else if (const auto blurred = _data->thumbnailInline()) {
	//	return context.selected()
	//		? blurred->pixBlurredColored(c, w, h)
	//		: blurred->pixBlurred(w, h);
	} else if (good) {
		return context.selected()
			? good->pixColored(c, w, h)
			: good->pix(w, h);
	} else if (const auto thumbnail = _dataMedia->thumbnail()) {
		return context.selected()
			? thumbnail->pixBlurredColored(c, w, h)
			: thumbnail->pixBlurred(w, h);
	}
	return QPixmap();
}

void Sticker::refreshLink() {
	if (_link) {
		return;
	}
	const auto sticker = _data->sticker();
	if (isEmojiSticker()) {
		const auto weak = base::make_weak(this);
		_link = std::make_shared<LambdaClickHandler>([weak] {
			if (const auto that = weak.get()) {
				that->emojiStickerClicked();
			}
		});
	} else if (sticker && sticker->set) {
		_link = std::make_shared<LambdaClickHandler>([document = _data](ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto window = my.sessionWindow.get()) {
				StickerSetBox::Show(window, document);
			}
		});
	} else if (sticker
		&& (_data->dimensions.width() > kStickerSideSize
			|| _data->dimensions.height() > kStickerSideSize)
		&& !_parent->data()->isSending()
		&& !_parent->data()->hasFailed()) {
		// In case we have a .webp file that is displayed as a sticker, but
		// that doesn't fit in 512x512, we assume it may be a regular large
		// .webp image and we allow to open it in media viewer.
		_link = std::make_shared<DocumentOpenClickHandler>(
			_data,
			crl::guard(this, [=](FullMsgId id) {
				_parent->delegate()->elementOpenDocument(_data, id);
			}),
			_parent->data()->fullId());
	}
}

void Sticker::emojiStickerClicked() {
	if (_lottie) {
		_parent->delegate()->elementStartInteraction(_parent);
	}
	_lottieOncePlayed = false;
	_parent->history()->owner().requestViewRepaint(_parent);
}

void Sticker::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	dataMediaCreated();
}

void Sticker::dataMediaCreated() const {
	Expects(_dataMedia != nullptr);

	_dataMedia->goodThumbnailWanted();
	if (_dataMedia->thumbnailPath().isEmpty()) {
		_dataMedia->thumbnailWanted(_parent->data()->fullId());
	}
	_parent->history()->owner().registerHeavyViewPart(_parent);
}

void Sticker::setDiceIndex(const QString &emoji, int index) {
	_diceEmoji = emoji;
	_diceIndex = index;
}

void Sticker::setupLottie() {
	Expects(_dataMedia != nullptr);

	_lottie = ChatHelpers::LottiePlayerFromDocument(
		_dataMedia.get(),
		_replacements,
		ChatHelpers::StickerLottieSize::MessageHistory,
		size() * cIntRetinaFactor(),
		Lottie::Quality::High);
	lottieCreated();
}

void Sticker::lottieCreated() {
	Expects(_lottie != nullptr);

	_parent->history()->owner().registerHeavyViewPart(_parent);

	_lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &information) {
			_parent->history()->owner().requestViewResize(_parent);
		}, [&](const Lottie::DisplayFrameRequest &request) {
			_parent->history()->owner().requestViewRepaint(_parent);
		});
	}, _lifetime);
}

bool Sticker::hasHeavyPart() const {
	return _lottie || _dataMedia;
}

void Sticker::unloadHeavyPart() {
	unloadLottie();
	_dataMedia = nullptr;
}

void Sticker::unloadLottie() {
	if (!_lottie) {
		return;
	}
	if (_diceIndex > 0 && _lastDiceFrame.isNull()) {
		_nextLastDiceFrame = false;
		_lottieOncePlayed = false;
	}
	_lottie = nullptr;
	_parent->checkHeavyPart();
}

std::unique_ptr<Lottie::SinglePlayer> Sticker::stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return (data == _data && replacements == _replacements)
		? std::move(_lottie)
		: nullptr;
}

} // namespace HistoryView
