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
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "ui/image/image.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/text/custom_emoji_instance.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/click_handler_types.h"
#include "window/window_session_controller.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "chat_helpers/stickers_lottie.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {
namespace {

constexpr auto kMaxSizeFixed = 512;
constexpr auto kMaxEmojiSizeFixed = 256;
constexpr auto kPremiumMultiplier = (1 + 0.245 * 2);
constexpr auto kEmojiMultiplier = 3;
constexpr auto kMessageEffectMultiplier = 2;

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

[[nodiscard]] QColor ComputeEmojiTextColor(const PaintContext &context) {
	const auto st = context.st;
	const auto result = st->messageStyle(false, false).historyTextFg->c;
	if (!context.selected()) {
		return result;
	}
	const auto &add = st->msgStickerOverlay()->c;

	const auto ca = add.alpha();
	const auto ra = 0x100 - ca;
	const auto aa = ca + 1;
	const auto red = (result.red() * ra + add.red() * aa) >> 8;
	const auto green = (result.green() * ra + add.green() * aa) >> 8;
	const auto blue = (result.blue() * ra + add.blue() * aa) >> 8;
	return QColor(red, green, blue, result.alpha());
}

} // namespace

Sticker::Sticker(
	not_null<Element*> parent,
	not_null<DocumentData*> data,
	bool skipPremiumEffect,
	Element *replacing,
	const Lottie::ColorReplacements *replacements)
: _parent(parent)
, _data(data)
, _replacements(replacements)
, _cachingTag(ChatHelpers::StickerLottieSize::MessageHistory)
, _skipPremiumEffect(skipPremiumEffect)
, _sensitiveBlurred(parent->data()->isMediaSensitive()) {
	if ((_dataMedia = _data->activeMediaView())) {
		dataMediaCreated();
	} else {
		_data->loadThumbnail(parent->data()->fullId());
		if (hasPremiumEffect()) {
			_data->loadVideoThumbnail(parent->data()->fullId());
		}
	}
	if (const auto media = replacing ? replacing->media() : nullptr) {
		_player = media->stickerTakePlayer(_data, _replacements);
		if (_player) {
			if (hasPremiumEffect() && !_premiumEffectPlayed) {
				_premiumEffectPlayed = true;
				if (On(PowerSaving::kStickersChat)
					&& !_premiumEffectSkipped) {
					_premiumEffectSkipped = true;
				} else {
					_parent->delegate()->elementStartPremium(
						_parent,
						replacing);
				}
			}
			playerCreated();
		}
	}
}

Sticker::~Sticker() {
	if (_player || _dataMedia) {
		if (_player) {
			unloadPlayer();
		}
		if (_dataMedia) {
			_data->owner().keepAlive(base::take(_dataMedia));
			_parent->checkHeavyPart();
		}
	}
}

bool Sticker::hasPremiumEffect() const {
	return !_skipPremiumEffect && _data->isPremiumSticker();
}

bool Sticker::customEmojiPart() const {
	return _customEmojiPart;
}

bool Sticker::emojiSticker() const {
	return _emojiSticker;
}

bool Sticker::webpagePart() const {
	return _webpagePart;
}

void Sticker::initSize(int customSize) {
	if (customSize > 0) {
		const auto original = Size(_data);
		const auto proposed = QSize{ customSize, customSize };
		_size = original.isEmpty()
			? proposed
			: DownscaledSize(original, proposed);
	} else if (emojiSticker() || _diceIndex >= 0) {
		_size = EmojiSize();
		if (_diceIndex > 0) {
			[[maybe_unused]] bool result = readyToDrawAnimationFrame();
		}
	} else {
		_size = Size(_data);
	}
	_size = DownscaledSize(_size, Size());
}

QSize Sticker::countOptimalSize() {
	if (_size.isEmpty()) {
		initSize();
	}
	return _size;
}

bool Sticker::readyToDrawAnimationFrame() {
	if (!_lastFrameCached.isNull()) {
		return true;
	}
	const auto sticker = _data->sticker();
	if (!sticker || _sensitiveBlurred) {
		return false;
	}

	ensureDataMediaCreated();
	_dataMedia->checkStickerLarge();
	const auto loaded = _dataMedia->loaded();
	const auto waitingForPremium = hasPremiumEffect()
		&& _dataMedia->videoThumbnailContent().isEmpty();
	if (!_player && loaded && !waitingForPremium && sticker->isAnimated()) {
		setupPlayer();
	}
	return ready();
}

QSize Sticker::Size() {
	const auto side = std::min(st::maxStickerSize, kMaxSizeFixed);
	return { side, side };
}

QSize Sticker::Size(not_null<DocumentData*> document) {
	return DownscaledSize(document->dimensions, Size());
}

QSize Sticker::PremiumEffectSize(not_null<DocumentData*> document) {
	return Size(document) * kPremiumMultiplier;
}

QSize Sticker::UsualPremiumEffectSize() {
	return DownscaledSize({ kMaxSizeFixed, kMaxSizeFixed }, Size())
		* kPremiumMultiplier;
}

QSize Sticker::EmojiEffectSize() {
	return EmojiSize() * kEmojiMultiplier;
}

QSize Sticker::MessageEffectSize() {
	return EmojiSize() * kMessageEffectMultiplier;
}

QSize Sticker::EmojiSize() {
	const auto side = std::min(st::maxAnimatedEmojiSize, kMaxEmojiSizeFixed);
	return { side, side };
}

void Sticker::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	if (!customEmojiPart()) {
		_parent->clearCustomEmojiRepaint();
	}

	ensureDataMediaCreated();
	if (readyToDrawAnimationFrame()) {
		paintAnimationFrame(p, context, r);
	} else if (!_data->sticker()
		|| (_data->sticker()->isLottie() && _replacements)
		|| !paintPixmap(p, context, r)) {
		paintPath(p, context, r);
	}
	if (_sensitiveBlurred) {
		paintSensitiveTag(p, context, r);
	}
}

void Sticker::paintSensitiveTag(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	auto text = Ui::Text::String();
	auto iconSkip = 0;
	text.setText(
		st::semiboldTextStyle,
		tr::lng_sensitive_tag(tr::now));
	iconSkip = st::mediaMenuIconStealth.width() * 1.4;
	const auto width = iconSkip + text.maxWidth();
	const auto inner = QRect(0, 0, width, text.minHeight());
	const auto outer = style::centerrect(
		r,
		inner.marginsAdded(st::paidTagPadding));
	const auto size = outer.size();
	const auto real = outer.marginsRemoved(st::paidTagPadding);
	const auto radius = std::min(size.width(), size.height()) / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(context.st->msgServiceBg());
	p.drawRoundedRect(outer, radius, radius);
	p.setPen(context.st->msgServiceFg());
	if (iconSkip) {
		st::mediaMenuIconStealth.paint(
			p,
			real.x(),
			(outer.y()
				+ (size.height() - st::mediaMenuIconStealth.height()) / 2),
			outer.width(),
			context.st->msgServiceFg()->c);
	}
	text.draw(p, real.x() + iconSkip, real.y(), width);
}

ClickHandlerPtr Sticker::link() {
	return _link;
}

bool Sticker::ready() const {
	return _player && _player->ready();
}

DocumentData *Sticker::document() {
	return _data;
}

void Sticker::stickerClearLoopPlayed() {
	if (!_playingOnce) {
		_oncePlayed = false;
	}
	_premiumEffectSkipped = false;
}

void Sticker::paintAnimationFrame(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	const auto colored = (customEmojiPart() && _data->emojiUsesTextColor())
		? ComputeEmojiTextColor(context)
		: (context.selected() && !_nextLastFrame)
		? context.st->msgStickerOverlay()->c
		: QColor(0, 0, 0, 0);
	const auto powerSavingFlag = (emojiSticker() || _diceIndex >= 0)
		? PowerSaving::kEmojiChat
		: PowerSaving::kStickersChat;
	const auto paused = context.paused || On(powerSavingFlag);
	const auto frame = _player
		? _player->frame(
			_size,
			colored,
			mirrorHorizontal(),
			context.now,
			paused)
		: StickerPlayer::FrameInfo();
	if (_nextLastFrame) {
		_nextLastFrame = false;
		_lastFrameCached = (_diceIndex > 0)
			? CacheDiceImage(_diceEmoji, _diceIndex, frame.image)
			: frame.image;
	}
	const auto &image = _lastFrameCached.isNull()
		? frame.image
		: _lastFrameCached;
	const auto prepared = (!_lastFrameCached.isNull() && context.selected())
		? Images::Colored(
			base::duplicate(image),
			context.st->msgStickerOverlay()->c)
		: image;
	const auto size = prepared.size() / style::DevicePixelRatio();
	p.drawImage(
		QRect(
			QPoint(
				r.x() + (r.width() - size.width()) / 2,
				r.y() + (r.height() - size.height()) / 2),
			size),
		prepared);
	if (!_lastFrameCached.isNull()) {
		return;
	}

	const auto count = _player->framesCount();
	_frameIndex = frame.index;
	_framesCount = count;
	_nextLastFrame = !paused
		&& _stopOnLastFrame
		&& (_frameIndex + 2 == count);
	const auto playOnce = _playingOnce
		? true
		: (_diceIndex == 0)
		? false
		: ((!customEmojiPart() && emojiSticker())
			|| !Core::App().settings().loopAnimatedStickers());
	const auto lastFrame = _stopOnLastFrame && atTheEnd();
	const auto switchToNext = !playOnce
		|| (!lastFrame && (_frameIndex != 0 || !_oncePlayed));
	if (!paused
		&& switchToNext
		&& _player->markFrameShown()
		&& playOnce
		&& !_oncePlayed) {
		_oncePlayed = true;
		_parent->delegate()->elementStartStickerLoop(_parent);
	}
	checkPremiumEffectStart();
}

bool Sticker::paintPixmap(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	const auto pixmap = paintedPixmap(context);
	if (pixmap.isNull()) {
		return false;
	}
	const auto size = pixmap.size() / pixmap.devicePixelRatio();
	const auto position = QPoint(
		r.x() + (r.width() - size.width()) / 2,
		r.y() + (r.height() - size.height()) / 2);
	const auto mirror = mirrorHorizontal();
	if (mirror) {
		p.save();
		const auto middle = QPointF(
			position.x() + size.width() / 2.,
			position.y() + size.height() / 2.);
		p.translate(middle);
		p.scale(-1., 1.);
		p.translate(-middle);
	}
	p.drawPixmap(position, pixmap);
	if (mirror) {
		p.restore();
	}
	return true;
}

void Sticker::paintPath(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	const auto pathGradient = _parent->delegate()->elementPathShiftGradient();
	auto helper = std::optional<style::owned_color>();
	if (customEmojiPart() && _data->emojiUsesTextColor()) {
		helper.emplace(Ui::CustomEmoji::PreviewColorFromTextColor(
			ComputeEmojiTextColor(context)));
		pathGradient->overrideColors(helper->color(), helper->color());
	} else if (webpagePart()) {
		pathGradient->overrideColors(st::shadowFg, st::shadowFg);
	} else if (context.selected()) {
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
		pathGradient,
		mirrorHorizontal());
	if (helper) {
		pathGradient->clearOverridenColors();
	}
}

QPixmap Sticker::paintedPixmap(const PaintContext &context) const {
	auto helper = std::optional<style::owned_color>();
	const auto sticker = _data->sticker();
	const auto ratio = style::DevicePixelRatio();
	const auto adjust = [&](int side) {
		return (((side * ratio) / 8) * 8) / ratio;
	};
	const auto useSize = (sticker && sticker->type == StickerType::Tgs)
		? QSize(adjust(_size.width()), adjust(_size.height()))
		: _size;
	const auto colored = (customEmojiPart() && _data->emojiUsesTextColor())
		? &helper.emplace(ComputeEmojiTextColor(context)).color()
		: context.selected()
		? &context.st->msgStickerOverlay()
		: nullptr;
	const auto good = _sensitiveBlurred
		? nullptr
		: _dataMedia->goodThumbnail();
	const auto image = _sensitiveBlurred
		? nullptr
		: _dataMedia->getStickerLarge();
	if (image) {
		return image->pix(useSize, { .colored = colored });
	//
	// Inline thumbnails can't have alpha channel.
	//
	//} else if (const auto blurred = _data->thumbnailInline()) {
	//	return blurred->pix(
	//		useSize,
	//		{ .colored = colored, .options = Images::Option::Blur });
	} else if (good) {
		return good->pix(useSize, { .colored = colored });
	} else if (const auto thumbnail = _dataMedia->thumbnail()) {
		return thumbnail->pix(
			useSize,
			{ .colored = colored, .options = Images::Option::Blur });
	}
	return QPixmap();
}

bool Sticker::mirrorHorizontal() const {
	if (!hasPremiumEffect()) {
		return false;
	}
	const auto rightAligned = _parent->hasRightLayout();
	return !rightAligned;
}

ClickHandlerPtr Sticker::ShowSetHandler(not_null<DocumentData*> document) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto window = my.sessionWindow.get()) {
			StickerSetBox::Show(window->uiShow(), document);
		}
	});
}

void Sticker::refreshLink() {
	if (_link) {
		return;
	}
	const auto sticker = _data->sticker();
	if (_sensitiveBlurred) {
		_link = MakeSensitiveMediaLink(nullptr, _parent->data());
	} else if (emojiSticker()) {
		const auto weak = base::make_weak(this);
		_link = std::make_shared<LambdaClickHandler>([weak] {
			if (const auto that = weak.get()) {
				that->emojiStickerClicked();
			}
		});
	} else if (sticker && sticker->set) {
		if (hasPremiumEffect()) {
			const auto weak = base::make_weak(this);
			_link = std::make_shared<LambdaClickHandler>([weak] {
				if (const auto that = weak.get()) {
					that->premiumStickerClicked();
				}
			});
		} else {
			_link = ShowSetHandler(_data);
		}
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
	if (_player) {
		_parent->delegate()->elementStartInteraction(_parent);
	}
	_oncePlayed = false;
	_parent->history()->owner().requestViewRepaint(_parent);
}

void Sticker::premiumStickerClicked() {
	_premiumEffectPlayed = false;

	// Remove when we start playing sticker itself on click.
	_premiumEffectSkipped = false;

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
	if (hasPremiumEffect()) {
		_data->loadVideoThumbnail(_parent->data()->fullId());
	}
	_parent->history()->owner().registerHeavyViewPart(_parent);
}

void Sticker::setDiceIndex(const QString &emoji, int index) {
	_diceEmoji = emoji;
	_diceIndex = index;
	_playingOnce = (index > 0);
	_stopOnLastFrame = (index > 0);
}

void Sticker::setPlayingOnce(bool once) {
	_playingOnce = once;
}

void Sticker::setStopOnLastFrame(bool stop) {
	_stopOnLastFrame = stop;
	_playingOnce = true;
}

void Sticker::setCustomCachingTag(ChatHelpers::StickerLottieSize tag) {
	_cachingTag = tag;
}

void Sticker::setCustomEmojiPart() {
	_customEmojiPart = true;
}

void Sticker::setEmojiSticker() {
	_emojiSticker = true;
}

void Sticker::setWebpagePart() {
	_webpagePart = true;
}

void Sticker::setupPlayer() {
	Expects(_dataMedia != nullptr);

	if (_data->sticker()->isLottie()) {
		_player = std::make_unique<LottiePlayer>(
			ChatHelpers::LottiePlayerFromDocument(
				_dataMedia.get(),
				_replacements,
				_cachingTag,
				countOptimalSize() * style::DevicePixelRatio(),
				Lottie::Quality::High));
	} else if (_data->sticker()->isWebm()) {
		_player = std::make_unique<WebmPlayer>(
			_dataMedia->owner()->location(),
			_dataMedia->bytes(),
			countOptimalSize());
	}

	checkPremiumEffectStart();
	playerCreated();
}

void Sticker::checkPremiumEffectStart() {
	if (!_premiumEffectPlayed && hasPremiumEffect()) {
		_premiumEffectPlayed = true;
		if (On(PowerSaving::kStickersChat)
			&& !_premiumEffectSkipped) {
			_premiumEffectSkipped = true;
		} else {
			_parent->delegate()->elementStartPremium(_parent, nullptr);
		}
	}
}

void Sticker::playerCreated() {
	Expects(_player != nullptr);

	_parent->history()->owner().registerHeavyViewPart(_parent);
	_player->setRepaintCallback([=] { _parent->customEmojiRepaint(); });
}

bool Sticker::hasHeavyPart() const {
	return _player || _dataMedia;
}

void Sticker::unloadHeavyPart() {
	unloadPlayer();
	_dataMedia = nullptr;
}

void Sticker::unloadPlayer() {
	if (!_player) {
		return;
	}
	if (_stopOnLastFrame && _lastFrameCached.isNull()) {
		_nextLastFrame = false;
		_oncePlayed = false;
	}
	_player = nullptr;
	if (hasPremiumEffect()) {
		_parent->delegate()->elementCancelPremium(_parent);
	}
	_parent->checkHeavyPart();
}

std::unique_ptr<StickerPlayer> Sticker::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return (data == _data && replacements == _replacements)
		? std::move(_player)
		: nullptr;
}

} // namespace HistoryView
