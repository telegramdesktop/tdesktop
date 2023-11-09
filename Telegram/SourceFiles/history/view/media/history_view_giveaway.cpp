/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_giveaway.h"

#include "base/unixtime.h"
#include "boxes/gift_premium_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "countries/countries_instance.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "dialogs/ui/dialogs_stories_content.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/tooltip.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] QSize CountOptimalTextSize(
		const Ui::Text::String &text,
		int minWidth,
		int maxWidth) {
	if (text.maxWidth() <= maxWidth) {
		return { text.maxWidth(), text.minHeight() };
	}
	const auto height = text.countHeight(maxWidth);
	return { Ui::FindNiceTooltipWidth(minWidth, maxWidth, [&](int width) {
		return text.countHeight(width);
	}), height };
}

} // namespace

Giveaway::Giveaway(
	not_null<Element*> parent,
	not_null<Data::Giveaway*> giveaway)
: Media(parent)
, _prizesTitle(st::msgMinWidth)
, _prizes(st::msgMinWidth)
, _participantsTitle(st::msgMinWidth)
, _participants(st::msgMinWidth)
, _countries(st::msgMinWidth)
, _winnersTitle(st::msgMinWidth)
, _winners(st::msgMinWidth) {
	fillFromData(giveaway);
}

Giveaway::~Giveaway() {
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

void Giveaway::fillFromData(not_null<Data::Giveaway*> giveaway) {
	_months = giveaway->months;
	_quantity = giveaway->quantity;

	_prizesTitle.setText(
		st::semiboldTextStyle,
		tr::lng_prizes_title(tr::now, lt_count, _quantity),
		kDefaultTextOptions);

	_prizes.setMarkedText(
		st::defaultTextStyle,
		tr::lng_prizes_about(
			tr::now,
			lt_count,
			_quantity,
			lt_duration,
			Ui::Text::Bold(GiftDuration(_months)),
			Ui::Text::RichLangValue),
		kDefaultTextOptions);
	_participantsTitle.setText(
		st::semiboldTextStyle,
		tr::lng_prizes_participants(tr::now),
		kDefaultTextOptions);

	for (const auto &channel : giveaway->channels) {
		_channels.push_back({
			.name = Ui::Text::String(
				st::semiboldTextStyle,
				channel->name(),
				kDefaultTextOptions,
				st::msgMinWidth),
			.thumbnail = Dialogs::Stories::MakeUserpicThumbnail(channel),
			.link = channel->openLink(),
			.colorIndex = channel->colorIndex(),
		});
	}
	const auto channels = int(_channels.size());

	const auto &instance = Countries::Instance(); ;
	auto countries = QStringList();
	for (const auto &country : giveaway->countries) {
		const auto name = instance.countryNameByISO2(country);
		const auto flag = instance.flagEmojiByISO2(country);
		countries.push_back(flag + QChar(0xA0) + name);
	}
	if (const auto count = countries.size()) {
		auto united = countries.front();
		for (auto i = 1; i != count; ++i) {
			united = ((i + 1 == count)
				? tr::lng_prizes_countries_and_last
				: tr::lng_prizes_countries_and_one)(
					tr::now,
					lt_countries,
					united,
					lt_country,
					countries[i]);
		}
		_countries.setText(
			st::defaultTextStyle,
			tr::lng_prizes_countries(tr::now, lt_countries, united),
			kDefaultTextOptions);
	} else {
		_countries.clear();
	}

	_participants.setText(
		st::defaultTextStyle,
		(giveaway->all
			? tr::lng_prizes_participants_all
			: tr::lng_prizes_participants_new)(tr::now, lt_count, channels),
		kDefaultTextOptions);
	_winnersTitle.setText(
		st::semiboldTextStyle,
		tr::lng_prizes_date(tr::now),
		kDefaultTextOptions);
	_winners.setText(
		st::defaultTextStyle,
		langDateTime(base::unixtime::parse(giveaway->untilDate)),
		kDefaultTextOptions);

	ensureStickerCreated();
}

QSize Giveaway::countOptimalSize() {
	const auto maxWidth = st::chatGiveawayWidth;
	const auto padding = inBubblePadding();
	const auto available = maxWidth - padding.left() - padding.right();

	_stickerTop = st::chatGiveawayStickerTop;
	_prizesTitleTop = _stickerTop
		+ st::msgServiceGiftBoxStickerSize.height()
		+ st::chatGiveawayPrizesTop;
	_prizesTop = _prizesTitleTop
		+ _prizesTitle.countHeight(available)
		+ st::chatGiveawayPrizesSkip;
	const auto prizesSize = CountOptimalTextSize(
		_prizes,
		st::msgMinWidth,
		available);
	_prizesWidth = prizesSize.width();
	_participantsTitleTop = _prizesTop
		+ prizesSize.height()
		+ st::chatGiveawayParticipantsTop;
	_participantsTop = _participantsTitleTop
		+ _participantsTitle.countHeight(available)
		+ st::chatGiveawayParticipantsSkip;
	const auto participantsSize = CountOptimalTextSize(
		_participants,
		st::msgMinWidth,
		available);
	_participantsWidth = participantsSize.width();
	const auto channelsTop = _participantsTop
		+ participantsSize.height()
		+ st::chatGiveawayChannelTop;
	const auto channelsBottom = layoutChannels(
		padding.left(),
		channelsTop,
		available);
	_countriesTop = channelsBottom;
	if (_countries.isEmpty()) {
		_winnersTitleTop = _countriesTop + st::chatGiveawayDateTop;
	} else {
		const auto countriesSize = CountOptimalTextSize(
			_countries,
			st::msgMinWidth,
			available);
		_countriesWidth = countriesSize.width();
		_winnersTitleTop = _countriesTop
			+ _countries.countHeight(available)
			+ st::chatGiveawayCountriesSkip;
	}
	_winnersTop = _winnersTitleTop
		+ _winnersTitle.countHeight(available)
		+ st::chatGiveawayDateSkip;
	const auto height = _winnersTop
		+ _winners.countHeight(available)
		+ st::chatGiveawayBottomSkip;
	return { maxWidth, height };
}

int Giveaway::layoutChannels(int x, int y, int available) {
	const auto size = st::chatGiveawayChannelSize;
	const auto skip = st::chatGiveawayChannelSkip;
	const auto padding = st::chatGiveawayChannelPadding;
	auto left = available;
	const auto shiftRow = [&](int i, int top, int shift) {
		for (auto j = i; j != 0; --j) {
			auto &geometry = _channels[j - 1].geometry;
			if (geometry.top() != top) {
				break;
			}
			geometry.moveLeft(geometry.x() + shift);
		}
	};
	const auto count = int(_channels.size());
	for (auto i = 0; i != count; ++i) {
		const auto desired = size
			+ padding.left()
			+ _channels[i].name.maxWidth()
			+ padding.right();
		const auto width = std::min(desired, available);
		if (left < width) {
			shiftRow(i, y, (left + skip) / 2);
			left = available;
			y += size + skip;
		}
		_channels[i].geometry = { x + available - left, y, width, size };
		left -= width + skip;
	}
	shiftRow(count, y, (left + skip) / 2);
	return y + size + skip;
}

QSize Giveaway::countCurrentSize(int newWidth) {
	return { maxWidth(), minHeight()};
}

void Giveaway::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	const auto stm = context.messageStyle();

	auto padding = inBubblePadding();

	const auto outer = width();
	const auto paintw = outer - padding.left() - padding.right();
	const auto stickerSize = st::msgServiceGiftBoxStickerSize;
	const auto sticker = QRect(
		(outer - stickerSize.width()) / 2,
		_stickerTop,
		stickerSize.width(),
		stickerSize.height());

	if (_sticker) {
		_sticker->draw(p, context, sticker);
		paintBadge(p, context);
	} else {
		ensureStickerCreated();
	}
	const auto paintText = [&](
			const Ui::Text::String &text,
			int top,
			int width) {
		p.setPen(stm->historyTextFg);
		text.draw(p, {
			.position = { padding.left() + (paintw - width) / 2, top},
			.outerWidth = outer,
			.availableWidth = width,
			.align = style::al_top,
			.palette = &stm->textPalette,
			.now = context.now,
		});
	};
	paintText(_prizesTitle, _prizesTitleTop, paintw);
	paintText(_prizes, _prizesTop, _prizesWidth);
	paintText(_participantsTitle, _participantsTitleTop, paintw);
	paintText(_participants, _participantsTop, _participantsWidth);
	if (!_countries.isEmpty()) {
		paintText(_countries, _countriesTop, _countriesWidth);
	}
	paintText(_winnersTitle, _winnersTitleTop, paintw);
	paintText(_winners, _winnersTop, paintw);
	paintChannels(p, context);
}

void Giveaway::paintBadge(Painter &p, const PaintContext &context) const {
	validateBadge(context);

	const auto badge = _badge.size() / _badge.devicePixelRatio();
	const auto left = (width() - badge.width()) / 2;
	const auto top = st::chatGiveawayBadgeTop;
	const auto rect = QRect(left, top, badge.width(), badge.height());
	const auto paintContent = [&](QPainter &q) {
		q.drawImage(rect.topLeft(), _badge);
	};

	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(context.messageStyle()->msgFileBg);
		const auto half = st::chatGiveawayBadgeStroke / 2.;
		const auto inner = QRectF(rect).marginsRemoved(
			{ half, half, half, half });
		const auto radius = inner.height() / 2.;
		p.drawRoundedRect(inner, radius, radius);
	}

	if (!usesBubblePattern(context)) {
		paintContent(p);
	} else {
		Ui::PaintPatternBubblePart(
			p,
			context.viewport,
			context.bubblesPattern->pixmap,
			rect,
			paintContent,
			_badgeCache);
	}
}

void Giveaway::paintChannels(
		Painter &p,
		const PaintContext &context) const {
	if (_channels.empty()) {
		return;
	}

	const auto size = _channels[0].geometry.height();
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto selected = context.selected();
	const auto padding = st::chatGiveawayChannelPadding;
	for (const auto &channel : _channels) {
		const auto &thumbnail = channel.thumbnail;
		const auto &geometry = channel.geometry;
		if (!_subscribedToThumbnails) {
			thumbnail->subscribeToUpdates([view = parent()] {
				view->history()->owner().requestViewRepaint(view);
			});
		}

		const auto colorIndex = channel.colorIndex;
		const auto cache = context.outbg
			? stm->replyCache[st->colorPatternIndex(colorIndex)].get()
			: st->coloredReplyCache(selected, colorIndex).get();
		if (channel.corners[0].isNull() || channel.bg != cache->bg) {
			channel.bg = cache->bg;
			channel.corners = Images::CornersMask(size / 2);
			for (auto &image : channel.corners) {
				style::colorizeImage(image, cache->bg, &image);
			}
		}
		p.setPen(cache->icon);
		Ui::DrawRoundedRect(p, geometry, channel.bg, channel.corners);
		if (channel.ripple) {
			channel.ripple->paint(
				p,
				geometry.x(),
				geometry.y(),
				width(),
				&cache->bg);
			if (channel.ripple->empty()) {
				channel.ripple = nullptr;
			}
		}

		p.drawImage(geometry.topLeft(), thumbnail->image(size));
		const auto left = size + padding.left();
		const auto top = padding.top();
		const auto available = geometry.width() - left - padding.right();
		channel.name.draw(p, {
			.position = { geometry.left() + left, geometry.top() + top },
			.outerWidth = width(),
			.availableWidth = available,
			.align = style::al_left,
			.palette = &stm->textPalette,
			.now = context.now,
			.elisionLines = 1,
			.elisionBreakEverywhere = true,
		});
	}
	_subscribedToThumbnails = 1;
}

void Giveaway::ensureStickerCreated() const {
	if (_sticker) {
		return;
	}
	const auto &session = _parent->history()->session();
	auto &packs = session.giftBoxStickersPacks();
	if (const auto document = packs.lookup(_months)) {
		if (const auto sticker = document->sticker()) {
			const auto skipPremiumEffect = false;
			_sticker.emplace(_parent, document, skipPremiumEffect, _parent);
			_sticker->setDiceIndex(sticker->alt, 1);
			_sticker->setGiftBoxSticker(true);
			_sticker->initSize();
		}
	}
}

void Giveaway::validateBadge(const PaintContext &context) const {
	const auto stm = context.messageStyle();
	const auto &badgeFg = stm->historyFileRadialFg->c;
	const auto &badgeBorder = stm->msgBg->c;
	if (!_badge.isNull()
		&& _badgeFg == badgeFg
		&& _badgeBorder == badgeBorder) {
		return;
	}
	const auto &font = st::chatGiveawayBadgeFont;
	_badgeFg = badgeFg;
	_badgeBorder = badgeBorder;
	const auto text = tr::lng_prizes_badge(
		tr::now,
		lt_amount,
		QString::number(_quantity));
	const auto width = font->width(text);
	const auto inner = QRect(0, 0, width, font->height);
	const auto rect = inner.marginsAdded(st::chatGiveawayBadgePadding);
	const auto size = rect.size();
	const auto ratio = style::DevicePixelRatio();
	_badge = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	_badge.setDevicePixelRatio(ratio);
	_badge.fill(Qt::transparent);

	auto p = QPainter(&_badge);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(_badgeBorder, st::chatGiveawayBadgeStroke * 1.));
	p.setBrush(Qt::NoBrush);
	const auto half = st::chatGiveawayBadgeStroke / 2.;
	const auto smaller = QRectF(
		rect.translated(-rect.topLeft())
	).marginsRemoved({ half, half, half, half });
	const auto radius = smaller.height() / 2.;
	p.drawRoundedRect(smaller, radius, radius);
	p.setPen(_badgeFg);
	p.setFont(font);
	p.drawText(
		st::chatGiveawayBadgePadding.left(),
		st::chatGiveawayBadgePadding.top() + font->ascent,
		text);
}

TextState Giveaway::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	for (const auto &channel : _channels) {
		if (channel.geometry.contains(point)) {
			result.link = channel.link;
			_lastPoint = point;
			return result;
		}
	}
	return result;
}

void Giveaway::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
}

void Giveaway::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (auto &channel : _channels) {
		if (channel.link != p) {
			continue;
		}
		if (pressed) {
			if (!channel.ripple) {
				const auto owner = &parent()->history()->owner();
				channel.ripple = std::make_unique<Ui::RippleAnimation>(
					st::defaultRippleAnimation,
					Ui::RippleAnimation::RoundRectMask(
						channel.geometry.size(),
						channel.geometry.height() / 2),
					[=] { owner->requestViewRepaint(parent()); });
			}
			channel.ripple->add(_lastPoint - channel.geometry.topLeft());
		} else if (channel.ripple) {
			channel.ripple->lastStop();
		}
		break;
	}
}

bool Giveaway::hideFromName() const {
	return !parent()->data()->Has<HistoryMessageForwarded>();
}

bool Giveaway::hasHeavyPart() const {
	return _subscribedToThumbnails;
}

void Giveaway::unloadHeavyPart() {
	if (_subscribedToThumbnails) {
		_subscribedToThumbnails = 0;
		for (const auto &channel : _channels) {
			channel.thumbnail->subscribeToUpdates(nullptr);
		}
	}
}

QMargins Giveaway::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.top() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.bottom() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

} // namespace HistoryView
