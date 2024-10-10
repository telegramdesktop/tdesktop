/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_common.h"

#include "boxes/sticker_set_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Info::PeerGifts {
namespace {

constexpr auto kGiftsPerRow = 3;

} // namespace

GiftButton::GiftButton(
	QWidget *parent,
	not_null<GiftButtonDelegate*> delegate)
: AbstractButton(parent)
, _delegate(delegate) {
}

GiftButton::~GiftButton() {
	unsubscribe();
}

void GiftButton::unsubscribe() {
	if (base::take(_subscribed)) {
		_userpic->subscribeToUpdates(nullptr);
	}
}

void GiftButton::setDescriptor(const GiftDescriptor &descriptor) {
	if (_descriptor == descriptor) {
		return;
	}
	auto player = base::take(_player);
	_mediaLifetime.destroy();
	_descriptor = descriptor;
	unsubscribe();
	v::match(descriptor, [&](const GiftTypePremium &data) {
		const auto months = data.months;
		const auto years = (months % 12) ? 0 : months / 12;
		_text = Ui::Text::String(st::giftBoxGiftHeight / 4);
		_text.setMarkedText(
			st::defaultTextStyle,
			Ui::Text::Bold(years
				? tr::lng_years(tr::now, lt_count, years)
				: tr::lng_months(tr::now, lt_count, months)
			).append('\n').append(
				tr::lng_gift_premium_label(tr::now)
			));
		_price.setText(
			st::semiboldTextStyle,
			Ui::FillAmountAndCurrency(
				data.cost,
				data.currency,
				true));
		_userpic = nullptr;
	}, [&](const GiftTypeStars &data) {
		_price.setMarkedText(
			st::semiboldTextStyle,
			_delegate->star().append(' ' + QString::number(data.stars)),
			kMarkupTextOptions,
			_delegate->textContext());
		_userpic = !data.userpic
			? nullptr
			: data.from
			? Ui::MakeUserpicThumbnail(data.from)
			: Ui::MakeHiddenAuthorThumbnail();
	});
	if (const auto document = _delegate->lookupSticker(descriptor)) {
		setDocument(document);
	}

	const auto buttonw = _price.maxWidth();
	const auto buttonh = st::semiboldFont->height;
	const auto inner = QRect(
		QPoint(),
		QSize(buttonw, buttonh)
	).marginsAdded(st::giftBoxButtonPadding);
	const auto skipy = _delegate->buttonSize().height()
		- st::giftBoxButtonBottom
		- inner.height();
	const auto skipx = (width() - inner.width()) / 2;
	const auto outer = (width() - 2 * skipx);
	_button = QRect(skipx, skipy, outer, inner.height());
}

bool GiftButton::documentResolved() const {
	return _player || _mediaLifetime;
}

void GiftButton::setDocument(not_null<DocumentData*> document) {
	const auto media = document->createMediaView();
	media->checkStickerLarge();
	media->goodThumbnailWanted();

	rpl::single() | rpl::then(
		document->owner().session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return media->loaded();
	}) | rpl::start_with_next([=] {
		_mediaLifetime.destroy();

		auto result = std::unique_ptr<HistoryView::StickerPlayer>();
		const auto sticker = document->sticker();
		if (sticker->isLottie()) {
			result = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					media.get(),
					ChatHelpers::StickerLottieSize::InlineResults,
					st::giftBoxStickerSize,
					Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			result = std::make_unique<HistoryView::WebmPlayer>(
				media->owner()->location(),
				media->bytes(),
				st::giftBoxStickerSize);
		} else {
			result = std::make_unique<HistoryView::StaticStickerPlayer>(
				media->owner()->location(),
				media->bytes(),
				st::giftBoxStickerSize);
		}
		result->setRepaintCallback([=] { update(); });
		_player = std::move(result);
		update();
	}, _mediaLifetime);
}

void GiftButton::setGeometry(QRect inner, QMargins extend) {
	_extend = extend;
	AbstractButton::setGeometry(inner.marginsAdded(extend));
}

void GiftButton::resizeEvent(QResizeEvent *e) {
	if (!_button.isEmpty()) {
		_button.moveLeft((width() - _button.width()) / 2);
	}
}

void GiftButton::paintEvent(QPaintEvent *e) {
	if (!documentResolved()) {
		if (const auto document = _delegate->lookupSticker(_descriptor)) {
			setDocument(document);
		}
	}

	auto p = QPainter(this);
	const auto hidden = v::is<GiftTypeStars>(_descriptor)
		&& v::get<GiftTypeStars>(_descriptor).hidden;;
	const auto position = QPoint(_extend.left(), _extend.top());
	const auto background = _delegate->background();
	const auto dpr = int(background.devicePixelRatio());
	const auto width = this->width();
	if (width * dpr <= background.width()) {
		p.drawImage(0, 0, background);
	} else {
		const auto full = background.width();
		const auto half = ((full / 2) / dpr) * dpr;
		const auto height = background.height();
		p.drawImage(
			QRect(0, 0, half / dpr, height / dpr),
			background,
			QRect(0, 0, half, height));
		p.drawImage(
			QRect(width - (half / dpr), 0, half / dpr, height / dpr),
			background,
			QRect(full - half, 0, half, height));
		p.drawImage(
			QRect(half / dpr, 0, width - 2 * (half / dpr), height / dpr),
			background,
			QRect(half, 0, 1, height));
	}

	if (_userpic) {
		if (!_subscribed) {
			_subscribed = true;
			_userpic->subscribeToUpdates([=] { update(); });
		}
		const auto image = _userpic->image(st::giftBoxUserpicSize);
		const auto skip = st::giftBoxUserpicSkip;
		p.drawImage(_extend.left() + skip, _extend.top() + skip, image);
	}

	auto frame = QImage();
	if (_player && _player->ready()) {
		const auto paused = !isOver();
		auto info = _player->frame(
			st::giftBoxStickerSize,
			QColor(0, 0, 0, 0),
			false,
			crl::now(),
			paused);
		frame = info.image;
		const auto finished = (info.index + 1 == _player->framesCount());
		if (!finished || !paused) {
			_player->markFrameShown();
		}
		const auto size = frame.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				(width - size.width()) / 2,
				(_text.isEmpty()
					? st::giftBoxStickerStarTop
					: st::giftBoxStickerTop),
				size.width(),
				size.height()),
			frame);
	}
	if (hidden) {
		const auto topleft = QPoint(
			(width - st::giftBoxStickerSize.width()) / 2,
			(_text.isEmpty()
				? st::giftBoxStickerStarTop
				: st::giftBoxStickerTop));
		_delegate->hiddenMark()->paint(
			p,
			frame,
			_hiddenBgCache,
			topleft,
			st::giftBoxStickerSize,
			width);
	}

	auto hq = PainterHighQualityEnabler(p);
	const auto premium = v::is<GiftTypePremium>(_descriptor);
	const auto singlew = width - _extend.left() - _extend.right();
	const auto font = st::semiboldFont;
	p.setFont(font);
	const auto text = v::match(_descriptor, [&](GiftTypePremium data) {
		if (data.discountPercent > 0) {
			p.setBrush(st::attentionButtonFg);
			const auto kMinus = QChar(0x2212);
			return kMinus + QString::number(data.discountPercent) + '%';
		}
		return QString();
	}, [&](const GiftTypeStars &data) {
		if (const auto count = data.limitedCount) {
			const auto soldOut = !data.userpic && !data.limitedLeft;
			p.setBrush(soldOut
				? st::attentionButtonFg
				: st::windowActiveTextFg);
			return soldOut
				? tr::lng_gift_stars_sold_out(tr::now)
				: !data.userpic
				? tr::lng_gift_stars_limited(tr::now)
				: (count == 1)
				? tr::lng_gift_limited_of_one(tr::now)
				: tr::lng_gift_limited_of_count(
					tr::now,
					lt_amount,
					((count % 1000)
						? Lang::FormatCountDecimal(count)
						: Lang::FormatCountToShort(count).string));
		}
		return QString();
	});
	if (!text.isEmpty()) {
		p.setPen(Qt::NoPen);
		const auto twidth = font->width(text);
		const auto pos = position + QPoint(singlew - twidth, font->height);
		p.save();
		const auto rubberOut = _extend.top();
		const auto inner = rect().marginsRemoved(_extend);
		p.setClipRect(inner.marginsAdded(
			{ rubberOut, rubberOut, rubberOut, rubberOut }));
		p.translate(pos);
		p.rotate(45.);
		p.translate(-pos);
		p.drawRect(-5 * twidth, position.y(), twidth * 12, font->height);
		p.setPen(st::windowBg);
		p.drawText(pos - QPoint(0, font->descent), text);
		p.restore();
	}
	p.setBrush(premium ? st::lightButtonBgOver : st::creditsBg3);
	p.setPen(Qt::NoPen);
	if (!premium) {
		p.setOpacity(0.12);
	}
	const auto geometry = _button;
	const auto radius = geometry.height() / 2.;
	p.drawRoundedRect(geometry, radius, radius);
	if (!premium) {
		p.setOpacity(1.);
	}

	if (!_text.isEmpty()) {
		p.setPen(st::windowFg);
		_text.draw(p, {
			.position = (position
				+ QPoint(0, st::giftBoxPremiumTextTop)),
			.availableWidth = singlew,
			.align = style::al_top,
		});
	}

	const auto padding = st::giftBoxButtonPadding;
	p.setPen(premium ? st::windowActiveTextFg : st::creditsFg);
	_price.draw(p, {
		.position = (geometry.topLeft()
			+ QPoint(padding.left(), padding.top())),
		.availableWidth = _price.maxWidth(),
	});
}

Delegate::Delegate(not_null<Window::SessionController*> window)
: _window(window)
, _hiddenMark(std::make_unique<StickerPremiumMark>(
	&window->session(),
	st::giftBoxHiddenMark,
	RectPart::Center)) {
}

Delegate::Delegate(Delegate &&other) = default;

Delegate::~Delegate() = default;

TextWithEntities Delegate::star() {
	const auto owner = &_window->session().data();
	return owner->customEmojiManager().creditsEmoji();
}

std::any Delegate::textContext() {
	return Core::MarkedTextContext{
		.session = &_window->session(),
		.customEmojiRepaint = [] {},
	};
}

QSize Delegate::buttonSize() {
	if (!_single.isEmpty()) {
		return _single;
	}
	const auto width = st::boxWideWidth;
	const auto padding = st::giftBoxPadding;
	const auto available = width - padding.left() - padding.right();
	const auto singlew = (available - 2 * st::giftBoxGiftSkip.x())
		/ kGiftsPerRow;
	_single = QSize(singlew, st::giftBoxGiftHeight);
	return _single;
}

QMargins Delegate::buttonExtend() {
	return st::defaultDropdownMenu.wrap.shadow.extend;
}

QImage Delegate::background() {
	if (!_bg.isNull()) {
		return _bg;
	}
	const auto single = buttonSize();
	const auto extend = buttonExtend();
	const auto bgSize = single.grownBy(extend);
	const auto ratio = style::DevicePixelRatio();
	auto bg = QImage(
		bgSize * ratio,
		QImage::Format_ARGB32_Premultiplied);
	bg.setDevicePixelRatio(ratio);
	bg.fill(Qt::transparent);

	const auto radius = st::giftBoxGiftRadius;
	const auto rect = QRect(QPoint(), bgSize).marginsRemoved(extend);

	{
		auto p = QPainter(&bg);
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(0.3);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowShadowFg);
		p.drawRoundedRect(
			QRectF(rect).translated(0, radius / 12.),
			radius,
			radius);
	}
	bg = bg.scaled(
		(bgSize * ratio) / 2,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	bg = Images::Blur(std::move(bg), true);
	bg = bg.scaled(
		bgSize * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	{
		auto p = QPainter(&bg);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(rect, radius, radius);
	}

	_bg = std::move(bg);
	return _bg;
}

DocumentData *Delegate::lookupSticker(const GiftDescriptor &descriptor) {
	return LookupGiftSticker(&_window->session(), descriptor);
}

not_null<StickerPremiumMark*> Delegate::hiddenMark() {
	return _hiddenMark.get();
}

DocumentData *LookupGiftSticker(
		not_null<Main::Session*> session,
		const GiftDescriptor &descriptor) {
	auto &packs = session->giftBoxStickersPacks();
	packs.load();
	return v::match(descriptor, [&](GiftTypePremium data) {
		return packs.lookup(data.months);
	}, [&](GiftTypeStars data) {
		return data.document
			? data.document
			: packs.lookup(packs.monthsForStars(data.stars));
	});
}

} // namespace Info::PeerGifts
