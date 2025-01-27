/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_common.h"

#include "boxes/star_gift_box.h"
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
#include "ui/effects/premium_graphics.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Info::PeerGifts {
namespace {

constexpr auto kGiftsPerRow = 3;

} // namespace

std::strong_ordering operator<=>(const GiftBadge &a, const GiftBadge &b) {
	const auto result1 = (a.text <=> b.text);
	if (result1 != std::strong_ordering::equal) {
		return result1;
	}
	const auto result2 = (a.bg1.rgb() <=> b.bg1.rgb());
	if (result2 != std::strong_ordering::equal) {
		return result2;
	}
	const auto result3 = (a.bg2.rgb() <=> b.bg2.rgb());
	if (result3 != std::strong_ordering::equal) {
		return result3;
	}
	const auto result4 = (a.fg.rgb() <=> b.fg.rgb());
	if (result4 != std::strong_ordering::equal) {
		return result4;
	}
	return a.gradient <=> b.gradient;
}

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

void GiftButton::setDescriptor(const GiftDescriptor &descriptor, Mode mode) {
	if (_descriptor == descriptor) {
		return;
	}
	auto player = base::take(_player);
	const auto starsType = Ui::Premium::MiniStars::Type::SlowStars;
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
		if (!_stars) {
			_stars.emplace(this, true, starsType);
		}
		_stars->setColorOverride(QGradientStops{
			{ 0., anim::with_alpha(st::windowActiveTextFg->c, .3) },
			{ 1., st::windowActiveTextFg->c },
		});
	}, [&](const GiftTypeStars &data) {
		const auto unique = data.info.unique.get();
		const auto soldOut = data.info.limitedCount
			&& !data.userpic
			&& !data.info.limitedLeft;
		_userpic = !data.userpic
			? nullptr
			: data.from
			? Ui::MakeUserpicThumbnail(data.from)
			: Ui::MakeHiddenAuthorThumbnail();
		if (mode == Mode::Minimal) {
			_price = {};
			_stars.reset();
			return;
		}
		_price.setMarkedText(
			st::semiboldTextStyle,
			(unique
				? tr::lng_gift_price_unique(tr::now, Ui::Text::WithEntities)
				: _delegate->star().append(
					' ' + QString::number(data.info.stars))),
			kMarkupTextOptions,
			_delegate->textContext());
		if (!_stars) {
			_stars.emplace(this, true, starsType);
		}
		if (unique) {
			const auto white = QColor(255, 255, 255);
			_stars->setColorOverride(QGradientStops{
				{ 0., anim::with_alpha(white, .3) },
				{ 1., white },
			});
		} else if (soldOut) {
			_stars.reset();
		} else {
			_stars->setColorOverride(
				Ui::Premium::CreditsIconGradientStops());
		}
	});
	_delegate->sticker(
		descriptor
	) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		setDocument(document);
	}, lifetime());
	_patterned = false;
	_uniqueBackgroundCache = QImage();
	_uniquePatternEmoji = nullptr;
	_uniquePatternCache.clear();

	if (mode != Mode::Full) {
		_button = QRect();
		_small = true;
		return;
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
	if (_stars) {
		const auto padding = _button.height() / 2;
		_stars->setCenter(_button - QMargins(padding, 0, padding, 0));
	}
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
		if (_stars) {
			const auto padding = _button.height() / 2;
			_stars->setCenter(_button - QMargins(padding, 0, padding, 0));
		}
	}
}

void GiftButton::cacheUniqueBackground(
		not_null<Data::UniqueGift*> unique,
		int width,
		int height) {
	if (!_uniquePatternEmoji) {
		_uniquePatternEmoji = _delegate->buttonPatternEmoji(unique, [=] {
			update();
		});
		[[maybe_unused]] const auto preload = _uniquePatternEmoji->ready();
	}
	const auto outer = QRect(0, 0, width, height);
	const auto inner = outer.marginsRemoved(
		_extend
	).translated(-_extend.left(), -_extend.top());
	const auto ratio = style::DevicePixelRatio();
	if (_uniqueBackgroundCache.size() != inner.size() * ratio) {
		_uniqueBackgroundCache = QImage(
			inner.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_uniqueBackgroundCache.fill(Qt::transparent);
		_uniqueBackgroundCache.setDevicePixelRatio(ratio);

		const auto radius = st::giftBoxGiftRadius;
		auto p = QPainter(&_uniqueBackgroundCache);
		auto hq = PainterHighQualityEnabler(p);
		auto gradient = QRadialGradient(inner.center(), inner.width() / 2);
		gradient.setStops({
			{ 0., unique->backdrop.centerColor },
			{ 1., unique->backdrop.edgeColor },
		});
		p.setBrush(gradient);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(inner, radius, radius);
		_patterned = false;
	}
	if (!_patterned && _uniquePatternEmoji->ready()) {
		_patterned = true;
		auto p = QPainter(&_uniqueBackgroundCache);
		p.setClipRect(inner);
		const auto skip = inner.width() / 3;
		Ui::PaintPoints(
			p,
			Ui::PatternPointsSmall(),
			_uniquePatternCache,
			_uniquePatternEmoji.get(),
			*unique,
			QRect(-skip, 0, inner.width() + 2 * skip, inner.height()));
	}
}

void GiftButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto unique = v::is<GiftTypeStars>(_descriptor)
		? v::get<GiftTypeStars>(_descriptor).info.unique.get()
		: nullptr;
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
	if (unique) {
		cacheUniqueBackground(unique, width, background.height() / dpr);
		p.drawImage(_extend.left(), _extend.top(), _uniqueBackgroundCache);
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
				(_small
					? st::giftBoxSmallStickerTop
					: _text.isEmpty()
					? st::giftBoxStickerStarTop
					: st::giftBoxStickerTop),
				size.width(),
				size.height()),
			frame);
	}
	if (hidden) {
		const auto topleft = QPoint(
			(width - st::giftBoxStickerSize.width()) / 2,
			(_small
				? st::giftBoxSmallStickerTop
				: _text.isEmpty()
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

	const auto badge = v::match(_descriptor, [&](GiftTypePremium data) {
		if (data.discountPercent > 0) {
			p.setBrush(st::attentionButtonFg);
			const auto kMinus = QChar(0x2212);
			return GiftBadge{
				.text = kMinus + QString::number(data.discountPercent) + '%',
				.bg1 = st::premiumButtonBg3->c,
				.bg2 = st::premiumButtonBg2->c,
				.fg = st::windowBg->c,
				.gradient = true,
				.small = true,
			};
		}
		return GiftBadge();
	}, [&](const GiftTypeStars &data) {
		if (const auto count = data.info.limitedCount) {
			const auto soldOut = !data.userpic && !data.info.limitedLeft;
			return GiftBadge{
				.text = (soldOut
					? tr::lng_gift_stars_sold_out(tr::now)
					: (!data.userpic && !data.info.unique)
					? tr::lng_gift_stars_limited(tr::now)
					: (count == 1)
					? tr::lng_gift_limited_of_one(tr::now)
					: tr::lng_gift_limited_of_count(
						tr::now,
						lt_amount,
						(((count % 1000) && (count < 10'000))
							? Lang::FormatCountDecimal(count)
							: Lang::FormatCountToShort(count).string))),
				.bg1 = (unique
					? unique->backdrop.edgeColor
					: soldOut
					? st::attentionButtonFg->c
					: st::windowActiveTextFg->c),
				.bg2 = (unique
					? unique->backdrop.patternColor
					: QColor(0, 0, 0, 0)),
				.fg = unique ? QColor(255, 255, 255) : st::windowBg->c,
				.small = true,
			};
		}
		return GiftBadge();
	});

	if (badge) {
		const auto rubberOut = st::lineWidth;
		const auto inner = rect().marginsRemoved(_extend);
		p.setClipRect(inner.marginsAdded(
			{ rubberOut, rubberOut, rubberOut, rubberOut }));

		const auto cached = _delegate->cachedBadge(badge);
		const auto width = cached.width() / cached.devicePixelRatio();
		p.drawImage(
			position.x() + singlew + rubberOut - width,
			position.y() - rubberOut,
			cached);
	}
	if (!_button.isEmpty()) {
		p.setBrush(unique
			? QBrush(QColor(255, 255, 255, .2 * 255))
			: premium
			? st::lightButtonBgOver
			: st::creditsBg3);
		p.setPen(Qt::NoPen);
		if (!unique && !premium) {
			p.setOpacity(0.12);
		}
		const auto geometry = _button;
		const auto radius = geometry.height() / 2.;
		p.drawRoundedRect(geometry, radius, radius);
		if (!premium) {
			p.setOpacity(1.);
		}
		if (_stars) {
			if (unique) {
				_stars->paint(p);
			} else {
				auto clipPath = QPainterPath();
				clipPath.addRoundedRect(geometry, radius, radius);
				p.setClipPath(clipPath);
				_stars->paint(p);
				p.setClipping(false);
			}
		}
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

	if (!_button.isEmpty()) {
		const auto padding = st::giftBoxButtonPadding;
		p.setPen(unique
			? QPen(QColor(255, 255, 255))
			: premium
			? st::windowActiveTextFg
			: st::creditsFg);
		_price.draw(p, {
			.position = (_button.topLeft()
				+ QPoint(padding.left(), padding.top())),
			.availableWidth = _price.maxWidth(),
		});
	}
}

Delegate::Delegate(
	not_null<Window::SessionController*> window,
	GiftButtonMode mode)
: _window(window)
, _hiddenMark(std::make_unique<StickerPremiumMark>(
	&window->session(),
	st::giftBoxHiddenMark,
	RectPart::Center))
, _mode(mode) {
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
	const auto minimal = (_mode == GiftButtonMode::Minimal);
	_single = QSize(
		singlew,
		minimal ? st::giftBoxGiftSmall : st::giftBoxGiftHeight);
	return _single;
}

QMargins Delegate::buttonExtend() {
	return st::defaultDropdownMenu.wrap.shadow.extend;
}

auto Delegate::buttonPatternEmoji(
	not_null<Data::UniqueGift*> unique,
	Fn<void()> repaint)
-> std::unique_ptr<Ui::Text::CustomEmoji> {
	return _window->session().data().customEmojiManager().create(
		unique->pattern.document,
		repaint,
		Data::CustomEmojiSizeTag::Large);
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

rpl::producer<not_null<DocumentData*>> Delegate::sticker(
		const GiftDescriptor &descriptor) {
	return GiftStickerValue(&_window->session(), descriptor);
}

not_null<StickerPremiumMark*> Delegate::hiddenMark() {
	return _hiddenMark.get();
}

QImage Delegate::cachedBadge(const GiftBadge &badge) {
	auto &image = _badges[badge];
	if (image.isNull()) {
		const auto &extend = buttonExtend();
		image = ValidateRotatedBadge(badge, extend.top());
	}
	return image;
}

DocumentData *LookupGiftSticker(
		not_null<Main::Session*> session,
		const GiftDescriptor &descriptor) {
	return v::match(descriptor, [&](GiftTypePremium data) {
		auto &packs = session->giftBoxStickersPacks();
		packs.load();
		return packs.lookup(data.months);
	}, [&](GiftTypeStars data) {
		return data.info.document.get();
	});
}

rpl::producer<not_null<DocumentData*>> GiftStickerValue(
		not_null<Main::Session*> session,
		const GiftDescriptor &descriptor) {
	return v::match(descriptor, [&](GiftTypePremium data) {
		const auto months = data.months;
		auto &packs = session->giftBoxStickersPacks();
		packs.load();
		if (const auto result = packs.lookup(months)) {
			return result->sticker()
				? (rpl::single(not_null(result)) | rpl::type_erased())
				: rpl::never<not_null<DocumentData*>>();
		}
		return packs.updated(
		) | rpl::map([=] {
			return session->giftBoxStickersPacks().lookup(data.months);
		}) | rpl::filter([](DocumentData *document) {
			return document && document->sticker();
		}) | rpl::take(1) | rpl::map([=](DocumentData *document) {
			return not_null(document);
		}) | rpl::type_erased();
	}, [&](GiftTypeStars data) {
		return rpl::single(data.info.document) | rpl::type_erased();
	});
}

QImage ValidateRotatedBadge(const GiftBadge &badge, int added) {
	const auto &font = badge.small
		? st::giftBoxGiftBadgeFont
		: st::semiboldFont;
	const auto twidth = font->width(badge.text) + 2 * added;
	const auto skip = int(std::ceil(twidth / M_SQRT2));
	const auto ratio = style::DevicePixelRatio();
	const auto multiplier = ratio * 3;
	const auto size = (twidth + font->height * 2);
	const auto textpos = QPoint(size - skip, added);
	auto image = QImage(
		QSize(size, size) * multiplier,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(multiplier);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.translate(textpos);
		p.rotate(45.);
		p.setFont(font);
		p.setPen(badge.fg);
		p.drawText(QPoint(added, font->ascent), badge.text);
	}

	auto scaled = image.scaled(
		QSize(size, size) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	scaled.setDevicePixelRatio(ratio);

	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);

		p.save();
		p.translate(textpos);
		p.rotate(45.);
		const auto rect = QRect(-5 * twidth, 0, twidth * 12, font->height);
		if (badge.gradient) {
			const auto skip = font->height / M_SQRT2;
			auto gradient = QLinearGradient(
				QPointF(-twidth - skip, 0),
				QPointF(twidth + skip, 0));
			gradient.setStops({
				{ 0., badge.bg1 },
				{ 1., badge.bg2 },
			});
			p.setBrush(gradient);
			p.drawRect(rect);
		} else {
			p.setBrush(badge.bg1);
			p.drawRect(rect);
			if (badge.bg2.alpha() > 0) {
				p.setOpacity(0.5);
				p.setBrush(badge.bg2);
				p.drawRect(rect);
				p.setOpacity(1.);
			}
		}
		p.restore();

		p.drawImage(0, 0, scaled);
	}
	return result;
}

} // namespace Info::PeerGifts
