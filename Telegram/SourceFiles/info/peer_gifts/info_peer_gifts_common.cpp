/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_common.h"

#include "api/api_global_privacy.h"
#include "api/api_premium.h"
#include "base/unixtime.h"
#include "boxes/send_credits_box.h" // SetButtonMarkedLabel
#include "boxes/star_gift_box.h"
#include "boxes/sticker_set_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_credits.h" // CreditsHistoryEntry
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "main/main_session.h"
#include "overview/overview_checkbox.h"
#include "settings/settings_credits_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/premium_graphics.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_overview.h"
#include "styles/style_premium.h"

namespace Info::PeerGifts {
namespace {

constexpr auto kGiftsPerRow = 3;

[[nodiscard]] bool AllowedToSend(
		const GiftTypeStars &gift,
		not_null<PeerData*> peer) {
	using Type = Api::DisallowedGiftType;
	const auto user = peer->asUser();
	if (!user || user->isSelf()) {
		return true;
	}
	const auto disallowedTypes = user ? user->disallowedGiftTypes() : Type();
	const auto allowLimited = !(disallowedTypes & Type::Limited);
	const auto allowUnlimited = !(disallowedTypes & Type::Unlimited);
	const auto allowUnique = !(disallowedTypes & Type::Unique);
	if (gift.resale) {
		return allowUnique;
	} else if (!gift.info.limitedCount) {
		return allowUnlimited;
	}
	return allowLimited || (gift.info.starsToUpgrade && allowUnique);
}

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
	const auto result4 = (a.border.rgb() <=> b.border.rgb());
	if (result4 != std::strong_ordering::equal) {
		return result4;
	}
	const auto result5 = (a.fg.rgb() <=> b.fg.rgb());
	if (result5 != std::strong_ordering::equal) {
		return result5;
	}
	return a.gradient <=> b.gradient;
}

rpl::producer<std::vector<GiftTypeStars>> GiftsStars(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer) {
	struct Session {
		std::vector<GiftTypeStars> last;
	};
	static auto Map = base::flat_map<not_null<Main::Session*>, Session>();

	const auto filtered = [=](std::vector<GiftTypeStars> list) {
		list.erase(ranges::remove_if(list, [&](const GiftTypeStars &gift) {
			return !AllowedToSend(gift, peer);
		}), end(list));
		return list;
	};
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		auto i = Map.find(session);
		if (i == end(Map)) {
			i = Map.emplace(session, Session()).first;
			session->lifetime().add([=] { Map.remove(session); });
		}
		if (!i->second.last.empty()) {
			consumer.put_next(filtered(i->second.last));
		}

		using namespace Api;
		const auto api = lifetime.make_state<PremiumGiftCodeOptions>(peer);
		api->requestStarGifts(
		) | rpl::start_with_error_done([=](QString error) {
			consumer.put_next({});
		}, [=] {
			auto list = std::vector<GiftTypeStars>();
			const auto &gifts = api->starGifts();
			list.reserve(gifts.size());
			for (auto &gift : gifts) {
				list.push_back({ .info = gift });
				if (gift.resellCount > 0) {
					list.push_back({ .info = gift, .resale = true });
				}
			}
			ranges::stable_sort(list, [](const auto &a, const auto &b) {
				const auto soldOut = [](const auto &gift) {
					return gift.info.soldOut && !gift.resale;
				};
				return soldOut(a) < soldOut(b);
			});

			auto &map = Map[session];
			if (map.last != list || list.empty()) {
				map.last = list;
				consumer.put_next(filtered(std::move(list)));
			}
		}, lifetime);

		return lifetime;
	};
}

GiftButton::GiftButton(
	QWidget *parent,
	not_null<GiftButtonDelegate*> delegate)
: AbstractButton(parent)
, _delegate(delegate)
, _lockedTimer([=] { refreshLocked(); }) {
}

GiftButton::~GiftButton() {
	unsubscribe();
}

void GiftButton::onStateChanged(State was, StateChangeSource source) {
	if (_check) {
		const auto diff = state() ^ was;
		if (diff & State::Enum::Over) {
			_check->setActive(state() & State::Enum::Over);
		}
		if (diff & State::Enum::Down) {
			_check->setPressed(state() & State::Enum::Down);
		}
	}
}

void GiftButton::unsubscribe() {
	if (_subscribed) {
		_subscribed = false;
		_userpic->subscribeToUpdates(nullptr);
	}
}

void GiftButton::setDescriptor(const GiftDescriptor &descriptor, Mode mode) {
	_mode = mode;

	const auto unique = v::is<GiftTypeStars>(descriptor)
		? v::get<GiftTypeStars>(descriptor).info.unique.get()
		: nullptr;
	const auto resalePrice = unique ? unique->starsForResale : 0;
	if (_descriptor == descriptor && _resalePrice == resalePrice) {
		return;
	}
	const auto starsType = Ui::Premium::MiniStarsType::SlowStars;
	unsubscribe();
	update();

	const auto format = [=](int64 number) {
		const auto onlyK = (number < 100'000'000);
		return (number >= 1'000'000)
			? Lang::FormatCountToShort(number, onlyK).string
			: Lang::FormatCountDecimal(number);
	};

	_descriptor = descriptor;
	_resalePrice = resalePrice;
	const auto resale = (_resalePrice > 0);
	v::match(descriptor, [&](const GiftTypePremium &data) {
		const auto months = data.months;
		_text = Ui::Text::String(st::giftBoxGiftHeight / 4);
		_text.setMarkedText(
			st::defaultTextStyle,
			Ui::Text::Bold(
				tr::lng_months(tr::now, lt_count, months)
			).append('\n').append(
				tr::lng_gift_premium_label(tr::now)
			));
		_price.setText(
			st::semiboldTextStyle,
			Ui::FillAmountAndCurrency(
				data.cost,
				data.currency,
				true));
		if (const auto stars = data.stars) {
			const auto starsText = Lang::FormatCountDecimal(stars);
			_byStars.setMarkedText(
				st::giftBoxByStarsStyle,
				tr::lng_gift_premium_by_stars(
					tr::now,
					lt_amount,
					_delegate->ministar().append(' ' + starsText),
					Ui::Text::WithEntities),
				kMarkupTextOptions,
				_delegate->textContext());
		}
		_userpic = nullptr;
		if (!_stars) {
			_stars.emplace(this, true, starsType);
		}
		_stars->setColorOverride(QGradientStops{
			{ 0., anim::with_alpha(st::windowActiveTextFg->c, .3) },
			{ 1., st::windowActiveTextFg->c },
		});
		_lockedUntilDate = 0;
	}, [&](const GiftTypeStars &data) {
		const auto soldOut = data.info.limitedCount
			&& !data.userpic
			&& !data.info.limitedLeft;
		_userpic = (!data.userpic || _mode == GiftButtonMode::Selection)
			? nullptr
			: data.from
			? Ui::MakeUserpicThumbnail(data.from)
			: Ui::MakeHiddenAuthorThumbnail();
		if (small() && !resale) {
			_price = {};
			_stars.reset();
			return;
		}
		_price.setMarkedText(
			st::semiboldTextStyle,
			(data.resale
				? ((unique && data.forceTon)
					? Data::FormatGiftResaleTon(*unique)
					: (unique
					? _delegate->monostar()
						: _delegate->star()).append(' ').append(
							format(unique
								? unique->starsForResale
								: data.info.starsResellMin)
						).append(data.info.resellCount > 1 ? "+" : ""))
				: (small() && unique && unique->starsForResale)
				? Data::FormatGiftResaleAsked(*unique)
				: unique
				? tr::lng_gift_transfer_button(
					tr::now,
					Ui::Text::WithEntities)
				: _delegate->star().append(' ' + format(data.info.stars))),
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
		} else if (data.resale) {
			_stars->setColorOverride(
				Ui::Premium::CreditsIconGradientStops());
		} else if (soldOut) {
			_stars.reset();
		} else {
			_stars->setColorOverride(
				Ui::Premium::CreditsIconGradientStops());
		}
		_lockedUntilDate = data.info.lockedUntilDate;
	});

	refreshLocked();

	_resolvedDocument = nullptr;
	_documentLifetime = _delegate->sticker(
		descriptor
	) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		_documentLifetime.destroy();
		setDocument(document);
	});
	if (_resolvedDocument) {
		_documentLifetime.destroy();
	}

	_patterned = false;
	_uniqueBackgroundCache = QImage();
	_uniquePatternEmoji = nullptr;
	_uniquePatternCache.clear();

	if (small() && !resale) {
		_button = QRect();
		return;
	}
	const auto buttonw = _price.maxWidth();
	const auto buttonh = st::semiboldFont->height;
	const auto inner = QRect(
		QPoint(),
		QSize(buttonw, buttonh)
	).marginsAdded(st::giftBoxButtonPadding);
	const auto skipy = _delegate->buttonSize().height()
		- (small()
			? st::giftBoxButtonBottomSmall
			: _byStars.isEmpty()
			? st::giftBoxButtonBottom
			: st::giftBoxButtonBottomByStars)
		- inner.height();
	const auto skipx = (width() - inner.width()) / 2;
	const auto outer = (width() - 2 * skipx);
	_button = QRect(skipx, skipy, outer, inner.height());
	if (_stars) {
		const auto padding = _button.height() / 2;
		_stars->setCenter(_button - QMargins(padding, 0, padding, 0));
	}
}

void GiftButton::refreshLocked() {
	_lockedTimer.cancel();

	const auto lockedFor = _lockedUntilDate
		? std::max(_lockedUntilDate - base::unixtime::now(), TimeId())
		: TimeId();
	const auto locked = (lockedFor > 0);
	if (locked) {
		_lockedTimer.callOnce(std::min(lockedFor, 86'400) * crl::time(1000));
	}
	if (_locked != locked) {
		_locked = locked;
		update();
	}
}

void GiftButton::setDocument(not_null<DocumentData*> document) {
	_resolvedDocument = document;
	if (_playerDocument == document) {
		return;
	}

	const auto media = document->createMediaView();
	media->checkStickerLarge();
	media->goodThumbnailWanted();

	const auto destroyed = base::take(_player);
	_playerDocument = nullptr;
	_mediaLifetime = rpl::single() | rpl::then(
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
		_playerDocument = media->owner();
		_player = std::move(result);
		update();
	});
	if (_playerDocument) {
		_mediaLifetime.destroy();
	}
}

void GiftButton::setGeometry(QRect inner, QMargins extend) {
	_extend = extend;
	AbstractButton::setGeometry(inner.marginsAdded(extend));
}

QMargins GiftButton::currentExtend() const {
	const auto progress = (_selectionMode == GiftSelectionMode::Border)
		? _selectedAnimation.value(_selected ? 1. : 0.)
		: 0.;
	const auto added = anim::interpolate(0, st::giftBoxSelectSkip, progress);
	return _extend + QMargins(added, added, added, added);
}

bool GiftButton::small() const {
	return _mode != GiftButtonMode::Full;
}

void GiftButton::toggleSelected(
		bool selected,
		GiftSelectionMode selectionMode,
		anim::type animated) {
	_selectionMode = selectionMode;
	if (_selectionMode != GiftSelectionMode::Check) {
		_check = nullptr;
	} else if (!_check) {
		_check = std::make_unique<Overview::Layout::Checkbox>(
			[=] { update(); },
			st::overviewSmallCheck);
	}
	if (_selected == selected) {
		if (animated == anim::type::instant) {
			_selectedAnimation.stop();
		}
		return;
	}

	const auto duration = st::defaultRoundCheckbox.duration;
	_selected = selected;
	if (animated == anim::type::instant) {
		if (_check) {
			_check->finishAnimating();
		}
		_selectedAnimation.stop();
		return;
	} else if (_check) {
		_check->setChecked(selected, animated);
		return;
	}
	_selectedAnimation.start([=] {
		update();
	}, selected ? 0. : 1., selected ? 1. : 0., duration, anim::easeOutCirc);
}

void GiftButton::paintBackground(QPainter &p, const QImage &background) {
	const auto removed = currentExtend() - _extend;
	const auto x = removed.left();
	const auto y = removed.top();
	const auto width = this->width() - x - removed.right();
	const auto height = this->height() - y - removed.bottom();
	const auto dpr = int(background.devicePixelRatio());
	const auto bwidth = background.width() / dpr;
	const auto bheight = background.height() / dpr;
	const auto fillRow = [&](int yfrom, int ytill, int bfrom) {
		const auto fill = [&](int xto, int wto, int xfrom, int wfrom = 0) {
			const auto fheight = ytill - yfrom;
			p.drawImage(
				QRect(x + xto, y + yfrom, wto, fheight),
				background,
				QRect(
					QPoint(xfrom, bfrom) * dpr,
					QSize((wfrom ? wfrom : wto), fheight) * dpr));
		};
		if (width < bwidth) {
			const auto xhalf = width / 2;
			fill(0, xhalf, 0);
			fill(xhalf, width - xhalf, bwidth - (width - xhalf));
		} else if (width == bwidth) {
			fill(0, width, 0);
		} else {
			const auto half = bwidth / (2 * dpr);
			fill(0, half, 0);
			fill(width - half, half, bwidth - half);
			fill(half, width - 2 * half, half, 1);
		}
	};
	if (height < bheight) {
		fillRow(0, height / 2, 0);
		fillRow(height / 2, height, bheight - (height - (height / 2)));
	} else {
		fillRow(0, height, 0);
	}

	auto hq = PainterHighQualityEnabler(p);
	const auto progress = (_selectionMode == GiftSelectionMode::Border)
		? _selectedAnimation.value(_selected ? 1. : 0.)
		: 0.;
	if (progress < 0.01) {
		return;
	}
	const auto pwidth = progress * st::defaultRoundCheckbox.width;
	p.setPen(QPen(st::defaultRoundCheckbox.bgActive->c, pwidth));
	p.setBrush(Qt::NoBrush);
	const auto rounded = rect().marginsRemoved(_extend);
	const auto phalf = pwidth / 2.;
	const auto extended = QRectF(rounded).marginsRemoved(
		{ phalf, phalf, phalf, phalf });
	const auto xradius = removed.left() + st::giftBoxGiftRadius - phalf;
	const auto yradius = removed.top() + st::giftBoxGiftRadius - phalf;
	p.drawRoundedRect(extended, xradius, yradius);
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

void GiftButton::contextMenuEvent(QContextMenuEvent *e) {
	_contextMenuRequests.fire_copy((e->reason() == QContextMenuEvent::Mouse)
		? e->globalPos()
		: QCursor::pos());
}

void GiftButton::mousePressEvent(QMouseEvent *e) {
	if (_mouseEventsAreListening) {
		if (e->button() != Qt::LeftButton) {
			return;
		}
		_mouseEvents.fire_copy(e);
	} else {
		AbstractButton::mousePressEvent(e);
	}
}

void GiftButton::mouseMoveEvent(QMouseEvent *e) {
	if (_mouseEventsAreListening) {
		if (e->button() != Qt::LeftButton) {
			return;
		}
		_mouseEvents.fire_copy(e);
	} else {
		AbstractButton::mouseMoveEvent(e);
	}
}

void GiftButton::mouseReleaseEvent(QMouseEvent *e) {
	if (_mouseEventsAreListening) {
		if (e->button() != Qt::LeftButton) {
			return;
		}
		_mouseEvents.fire_copy(e);
	} else {
		AbstractButton::mouseReleaseEvent(e);
	}
}

rpl::producer<QPoint> GiftButton::contextMenuRequests() const {
	return _contextMenuRequests.events();
}

rpl::producer<QMouseEvent*> GiftButton::mouseEvents() {
	_mouseEventsAreListening = true;
	return _mouseEvents.events();
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
	const auto extend = currentExtend();
	const auto inner = outer.marginsRemoved(
		extend
	).translated(-extend.left(), -extend.top());
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
	const auto onsale = unique && unique->starsForResale && small();
	const auto requirePremium = v::is<GiftTypeStars>(_descriptor)
		&& !v::get<GiftTypeStars>(_descriptor).userpic
		&& !v::get<GiftTypeStars>(_descriptor).info.unique
		&& v::get<GiftTypeStars>(_descriptor).info.requirePremium;
	const auto hidden = v::is<GiftTypeStars>(_descriptor)
		&& v::get<GiftTypeStars>(_descriptor).hidden;
	const auto extend = currentExtend();
	const auto position = QPoint(extend.left(), extend.top());
	const auto background = _delegate->background();
	const auto width = this->width();
	const auto dpr = int(background.devicePixelRatio());
	paintBackground(p, background);
	if (unique) {
		cacheUniqueBackground(unique, width, background.height() / dpr);
		p.drawImage(extend.left(), extend.top(), _uniqueBackgroundCache);
	} else if (requirePremium) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::creditsFg->p;
		pen.setWidth(style::ConvertScaleExact(2.));
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto outer = QRect(0, 0, width, background.height() / dpr);
		const auto extend = currentExtend();
		const auto radius = st::giftBoxGiftRadius;
		p.drawRoundedRect(outer.marginsRemoved(extend), radius, radius);
	}
	auto inset = 0;
	if (_selectionMode == GiftSelectionMode::Inset) {
		const auto progress = _selectedAnimation.value(_selected ? 1. : 0.);
		if (progress > 0) {
			auto hq = PainterHighQualityEnabler(p);
			auto pen = st::boxBg->p;
			const auto thickness = style::ConvertScaleExact(2.);
			pen.setWidthF(progress * thickness);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			const auto height = background.height() / dpr;
			const auto outer = QRectF(0, 0, width, height);
			const auto shift = progress * thickness * 2;
			const auto extend = QMarginsF(currentExtend())
				+ QMarginsF(shift, shift, shift, shift);
			const auto radius = st::giftBoxGiftRadius - shift;
			p.drawRoundedRect(outer.marginsRemoved(extend), radius, radius);
			inset = int(std::ceil(
				progress * (thickness * 2 + st::giftBoxUserpicSkip)));
		}
	}
	if (_locked) {
		st::giftBoxLockIcon.paint(
			p,
			position + st::giftBoxLockIconPosition,
			width);
	}

	if (_userpic) {
		if (!_subscribed) {
			_subscribed = true;
			_userpic->subscribeToUpdates([=] { update(); });
		}
		const auto image = _userpic->image(st::giftBoxUserpicSize);
		const auto skip = st::giftBoxUserpicSkip;
		p.drawImage(extend.left() + skip, extend.top() + skip, image);
	} else if (_check) {
		const auto skip = st::giftBoxUserpicSkip;
		_check->paint(
			p,
			QPoint(extend.left() + skip, extend.top() + skip),
			width,
			_selected,
			true);
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
				(small()
					? st::giftBoxSmallStickerTop
					: _text.isEmpty()
					? st::giftBoxStickerStarTop
					: _byStars.isEmpty()
					? st::giftBoxStickerTop
					: st::giftBoxStickerTopByStars),
				size.width(),
				size.height()),
			frame);
	}
	if (hidden) {
		const auto topleft = QPoint(
			(width - st::giftBoxStickerSize.width()) / 2,
			(small()
				? st::giftBoxSmallStickerTop
				: _text.isEmpty()
				? st::giftBoxStickerStarTop
				: _byStars.isEmpty()
				? st::giftBoxStickerTop
				: st::giftBoxStickerTopByStars));
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
	const auto singlew = width - extend.left() - extend.right();
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
		const auto count = data.info.limitedCount;
		const auto pinned = data.pinned || data.pinnedSelection;
		if (count || pinned) {
			const auto soldOut = !pinned
				&& !unique
				&& !data.userpic
				&& !data.info.limitedLeft;
			const auto yourLeft = data.info.perUserTotal
				? (data.info.perUserRemains
					? tr::lng_gift_stars_your_left(
						tr::now,
						lt_count,
						data.info.perUserRemains)
					: tr::lng_gift_stars_your_finished(tr::now))
				: QString();
			return GiftBadge{
				.text = (onsale
					? tr::lng_gift_stars_on_sale(tr::now)
					: (unique && (data.resale || pinned))
					? ('#' + QString::number(unique->number))
					: data.resale
					? tr::lng_gift_stars_resale(tr::now)
					: soldOut
					? tr::lng_gift_stars_sold_out(tr::now)
					: (!data.userpic
						&& !data.info.unique
						&& data.info.requirePremium)
					? ((yourLeft.isEmpty() || !_delegate->amPremium())
						? tr::lng_gift_stars_premium(tr::now)
						: yourLeft)
					: (!data.userpic && !data.info.unique)
					? (yourLeft.isEmpty()
						? tr::lng_gift_stars_limited(tr::now)
						: yourLeft)
					: (count == 1)
					? tr::lng_gift_limited_of_one(tr::now)
					: tr::lng_gift_limited_of_count(
						tr::now,
						lt_amount,
						(((count % 1000) && (count < 10'000))
							? Lang::FormatCountDecimal(count)
							: Lang::FormatCountToShort(count).string))),
				.bg1 = (onsale
					? st::boxTextFgGood->c
					: unique
					? unique->backdrop.edgeColor
					: data.resale
					? st::boxTextFgGood->c
					: soldOut
					? st::attentionButtonFg->c
					: (!data.userpic && data.info.requirePremium)
					? st::creditsFg->c
					: st::windowActiveTextFg->c),
				.bg2 = (onsale
					? QColor(0, 0, 0, 0)
					: unique
					? unique->backdrop.patternColor
					: QColor(0, 0, 0, 0)),
				.border = (onsale
					? QColor(255, 255, 255)
					: QColor(0, 0, 0, 0)),
				.fg = (onsale
					? st::windowBg->c
					: unique
					? QColor(255, 255, 255)
					: st::windowBg->c),
				.small = true,
			};
		}
		return GiftBadge();
	});

	if (badge) {
		const auto rubberOut = st::lineWidth;
		const auto inner = rect().marginsRemoved(extend);
		p.setClipRect(inner.marginsAdded(
			{ rubberOut, rubberOut, rubberOut, rubberOut }));

		const auto cached = _delegate->cachedBadge(badge);
		const auto width = cached.width() / cached.devicePixelRatio();
		p.drawImage(
			position.x() + singlew + rubberOut - width,
			position.y() - rubberOut,
			cached);
	}

	v::match(_descriptor, [](const GiftTypePremium &) {
	}, [&](const GiftTypeStars &data) {
		if (!unique) {
		} else if (data.pinned && _mode != GiftButtonMode::Selection) {
			auto hq = PainterHighQualityEnabler(p);
			const auto &icon = st::giftBoxPinIcon;
			const auto skip = st::giftBoxUserpicSkip;
			const auto add = (st::giftBoxUserpicSize - icon.width()) / 2;
			p.setPen(Qt::NoPen);
			p.setBrush(unique->backdrop.patternColor);
			const auto rect = QRect(
				QPoint(extend.left() + skip, extend.top() + skip),
				QSize(icon.width() + 2 * add, icon.height() + 2 * add));
			p.drawEllipse(rect);
			icon.paintInCenter(p, rect);
		} else if (!data.forceTon
			&& unique->nanoTonForResale
			&& unique->onlyAcceptTon) {
			if (_tonIcon.isNull()) {
				_tonIcon = st::tonIconEmojiLarge.icon.instance(
					QColor(255, 255, 255));
			}
			const auto size = _tonIcon.size() / _tonIcon.devicePixelRatio();
			const auto skip = st::giftBoxUserpicSkip + inset;
			const auto add = (st::giftBoxUserpicSize - size.width()) / 2;
			p.setPen(Qt::NoPen);
			p.setBrush(unique->backdrop.patternColor);
			const auto rect = QRect(
				QPoint(extend.left() + skip, extend.top() + skip),
				QSize(size.width() + 2 * add, size.height() + 2 * add));
			p.drawEllipse(rect);
			p.drawImage(
				extend.left() + skip + add,
				extend.top() + skip + add,
				_tonIcon);
		}
	});

	if (!_button.isEmpty()) {
		p.setBrush(onsale
			? QBrush(unique->backdrop.patternColor)
			: unique
			? QBrush(QColor(255, 255, 255, .2 * 255))
			: premium
			? st::lightButtonBgOver
			: st::creditsBg3);
		p.setPen(Qt::NoPen);
		if (!unique && !premium) {
			p.setOpacity(0.12);
		} else if (onsale) {
			p.setOpacity(0.8);
		}
		const auto geometry = _button;
		const auto radius = geometry.height() / 2.;
		p.drawRoundedRect(geometry, radius, radius);
		if (!premium || onsale) {
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
			.position = (position + QPoint(0, _byStars.isEmpty()
				? st::giftBoxPremiumTextTop
				: st::giftBoxPremiumTextTopByStars)),
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

		if (!_byStars.isEmpty()) {
			p.setPen(st::creditsFg);
			_byStars.draw(p, {
				.position = QPoint(
					position.x(),
					_button.y() + _button.height() + st::giftBoxByStarsSkip),
				.availableWidth = singlew,
				.align = style::al_top,
			});
		}
	}
}

Delegate::Delegate(not_null<Main::Session*> session, GiftButtonMode mode)
: _session(session)
, _hiddenMark(std::make_unique<StickerPremiumMark>(
	_session,
	st::giftBoxHiddenMark,
	RectPart::Center))
, _mode(mode) {
	_ministarEmoji = _emojiHelper.paletteDependent(
		Ui::Earn::IconCreditsEmojiSmall());
	_starEmoji = _emojiHelper.paletteDependent(
		Ui::Earn::IconCreditsEmoji());
}

Delegate::Delegate(Delegate &&other) = default;

Delegate::~Delegate() = default;

TextWithEntities Delegate::star() {
	return _starEmoji;
}

TextWithEntities Delegate::monostar() {
	return Ui::Text::IconEmoji(&st::starIconEmoji);
}

TextWithEntities Delegate::monoton() {
	return Ui::Text::IconEmoji(&st::tonIconEmoji);
}

TextWithEntities Delegate::ministar() {
	return _ministarEmoji;
}

Ui::Text::MarkedContext Delegate::textContext() {
	return _emojiHelper.context();
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
	const auto minimal = (_mode != GiftButtonMode::Full);
	_single = QSize(
		singlew,
		minimal ? st::giftBoxGiftSmall : st::giftBoxGiftHeight);
	return _single;
}

QMargins Delegate::buttonExtend() const {
	return st::defaultDropdownMenu.wrap.shadow.extend;
}

auto Delegate::buttonPatternEmoji(
	not_null<Data::UniqueGift*> unique,
	Fn<void()> repaint)
-> std::unique_ptr<Ui::Text::CustomEmoji> {
	return _session->data().customEmojiManager().create(
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
	return GiftStickerValue(_session, descriptor);
}

not_null<StickerPremiumMark*> Delegate::hiddenMark() {
	return _hiddenMark.get();
}

QImage Delegate::cachedBadge(const GiftBadge &badge) {
	auto &image = _badges[badge];
	if (image.isNull()) {
		const auto &extend = buttonExtend();
		const auto line = st::lineWidth;
		const auto padding = QMargins(extend.top(), 0, extend.top(), line);
		image = ValidateRotatedBadge(badge, padding);
	}
	return image;
}

bool Delegate::amPremium() {
	return _session->premium();
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

QImage ValidateRotatedBadge(const GiftBadge &badge, QMargins padding) {
	const auto &font = badge.small
		? st::giftBoxGiftBadgeFont
		: st::msgServiceGiftBoxBadgeFont;
	const auto twidth = font->width(badge.text)
		+ padding.left()
		+ padding.right();
	const auto skip = int(std::ceil(twidth / M_SQRT2));
	const auto ratio = style::DevicePixelRatio();
	const auto multiplier = ratio * 3;
	const auto size = (twidth + font->height * 2);
	const auto height = padding.top() + font->height + padding.bottom();
	const auto textpos = QPoint(size - skip, padding.top());
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
		p.drawText(
			QPoint(padding.left(), padding.top() + font->ascent),
			badge.text);
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

		p.save();
		p.translate(textpos);
		p.rotate(45.);
		const auto rect = QRect(-5 * twidth, 0, twidth * 12, height);
		if (badge.border.alpha() > 0) {
			p.setPen(badge.border);
		} else {
			p.setPen(Qt::NoPen);
		}
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

void SelectGiftToUnpin(
		std::shared_ptr<ChatHelpers::Show> show,
		const std::vector<Data::CreditsHistoryEntry> &pinned,
		Fn<void(Data::SavedStarGiftId)> chosen) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			explicit State(not_null<Main::Session*> session)
			: delegate(session, GiftButtonMode::Minimal) {
			}

			Delegate delegate;
			rpl::variable<int> selected = -1;
			std::vector<not_null<GiftButton*>> buttons;
		};
		const auto session = &show->session();
		const auto state = box->lifetime().make_state<State>(session);

		box->setStyle(st::giftTooManyPinnedBox);
		box->setWidth(st::boxWideWidth);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_gift_many_pinned_title(),
				st::giftBoxSubtitle),
			st::giftBoxSubtitleMargin,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_gift_many_pinned_choose(),
				st::giftTooManyPinnedChoose),
			st::giftBoxAboutMargin,
			style::al_top);

		const auto gifts = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			QMargins(
				st::giftBoxPadding.left(),
				st::giftTooManyPinnedBox.buttonPadding.top(),
				st::giftBoxPadding.right(),
				0));
		for (const auto &entry : pinned) {
			const auto index = int(state->buttons.size());
			state->buttons.push_back(
				Ui::CreateChild<GiftButton>(gifts, &state->delegate));
			const auto button = state->buttons.back();
			button->setDescriptor(GiftTypeStars{
				.info = {
					.id = entry.stargiftId,
					.unique = entry.uniqueGift,
					.document = entry.uniqueGift->model.document,
				},
				.pinnedSelection = true,
			}, GiftButton::Mode::Minimal);
			button->setClickedCallback([=] {
				const auto now = state->selected.current();
				state->selected = (now == index) ? -1 : index;
			});
		}

		state->selected.value(
		) | rpl::combine_previous(
		) | rpl::start_with_next([=](int old, int now) {
			if (old >= 0) state->buttons[old]->toggleSelected(false);
			if (now >= 0) state->buttons[now]->toggleSelected(true);
		}, gifts->lifetime());

		gifts->widthValue() | rpl::start_with_next([=](int width) {
			const auto singleMin = state->delegate.buttonSize();
			if (width < singleMin.width()) {
				return;
			}
			const auto count = int(state->buttons.size());
			const auto skipw = st::giftBoxGiftSkip.x();
			const auto skiph = st::giftBoxGiftSkip.y();
			const auto perRow = std::min(
				(width + skipw) / (singleMin.width() + skipw),
				std::max(count, 1));
			if (perRow <= 0) {
				return;
			}
			const auto single = (width - (perRow - 1) * skipw) / perRow;
			const auto height = singleMin.height();
			const auto rows = (count + perRow - 1) / perRow;
			for (auto row = 0; row != rows; ++row) {
				const auto y = row * (height + skiph);
				for (auto column = 0; column != perRow; ++column) {
					const auto index = row * perRow + column;
					if (index >= count) {
						break;
					}
					const auto &button = state->buttons[index];
					const auto x = column * (single + skipw);
					button->setGeometry(
						QRect(x, y, single, height),
						state->delegate.buttonExtend());
				}
			}
			gifts->resize(width, rows * (height + skiph) - skiph);
		}, gifts->lifetime());

		const auto button = box->addButton(rpl::single(QString()), [=] {
			const auto index = state->selected.current();
			if (index < 0) {
				return;
			}
			Assert(index < int(pinned.size()));
			const auto &entry = pinned[index];
			const auto weak = base::make_weak(box);
			chosen(::Settings::EntryToSavedStarGiftId(session, entry));
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		});
		const auto label = Ui::SetButtonMarkedLabel(
			button,
			tr::lng_context_unpin_from_top(Ui::Text::WithEntities),
			&show->session(),
			st::creditsBoxButtonLabel,
			&st::giftTooManyPinnedBox.button.textFg);

		state->selected.value() | rpl::start_with_next([=](int value) {
			const auto has = (value >= 0);
			label->setOpacity(has ? 1. : 0.5);
			button->setAttribute(Qt::WA_TransparentForMouseEvents, !has);
		}, box->lifetime());

		const auto buttonPadding = st::giftTooManyPinnedBox.buttonPadding;
		const auto buttonWidth = st::boxWideWidth
			- buttonPadding.left()
			- buttonPadding.right();
		button->resizeToWidth(buttonWidth);
		button->widthValue() | rpl::start_with_next([=](int width) {
			if (width != buttonWidth) {
				button->resizeToWidth(buttonWidth);
			}
		}, button->lifetime());
	}));
}

} // namespace Info::PeerGifts
