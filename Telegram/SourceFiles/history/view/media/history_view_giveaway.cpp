/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_giveaway.h"

#include "base/unixtime.h"
#include "chat_helpers/stickers_gift_box_pack.h"
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
#include "ui/text/text_utilities.h"
#include "ui/widgets/tooltip.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kChannelBgAlpha = 32;

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
, _winnersTitle(st::msgMinWidth)
, _winners(st::msgMinWidth) {
	fillFromData(giveaway);

	if (!parent->data()->Has<HistoryMessageForwarded>()
		&& ranges::contains(
			giveaway->channels,
			parent->data()->history()->peer)) {
		parent->setServicePreMessage({
			tr::lng_action_giveaway_started(
				tr::now,
				lt_from,
				parent->data()->history()->peer->name()),
		});
	}
}

Giveaway::~Giveaway() {
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

void Giveaway::fillFromData(not_null<Data::Giveaway*> giveaway) {
	_months = giveaway->months;

	_prizesTitle.setText(
		st::semiboldTextStyle,
		tr::lng_prizes_title(tr::now, lt_count, giveaway->quantity),
		kDefaultTextOptions);

	const auto duration = (giveaway->months < 12)
		? tr::lng_months(tr::now, lt_count, giveaway->months)
		: tr::lng_years(tr::now, lt_count, giveaway->months / 12);
	_prizes.setMarkedText(
		st::defaultTextStyle,
		tr::lng_prizes_about(
			tr::now,
			lt_count,
			giveaway->quantity,
			lt_duration,
			Ui::Text::Bold(duration),
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
		});
	}
	const auto channels = int(_channels.size());

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
	_winnersTitleTop = channelsBottom + st::chatGiveawayDateTop;
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

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	auto &semibold = stm->msgServiceFg;

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
	paintText(_winnersTitle, _winnersTitleTop, paintw);
	paintText(_winners, _winnersTop, paintw);
	paintChannels(p, context);
}

void Giveaway::paintChannels(
		Painter &p,
		const PaintContext &context) const {
	if (_channels.empty()) {
		return;
	}

	const auto size = _channels[0].geometry.height();
	const auto ratio = style::DevicePixelRatio();
	const auto stm = context.messageStyle();
	auto bg = stm->msgReplyBarColor->c;
	bg.setAlpha(kChannelBgAlpha);
	if (_channelCorners[0].isNull() || _channelBg != bg) {
		_channelBg = bg;
		_channelCorners = Images::CornersMask(size / 2);
		for (auto &image : _channelCorners) {
			style::colorizeImage(image, bg, &image);
		}
	}
	p.setPen(stm->msgReplyBarColor);
	const auto padding = st::chatGiveawayChannelPadding;
	for (const auto &channel : _channels) {
		const auto &thumbnail = channel.thumbnail;
		const auto &geometry = channel.geometry;
		if (!_subscribedToThumbnails) {
			thumbnail->subscribeToUpdates([view = parent()] {
				view->history()->owner().requestViewRepaint(view);
			});
		}
		Ui::DrawRoundedRect(p, geometry, _channelBg, _channelCorners);
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
			.elisionOneLine = true,
			.elisionBreakEverywhere = true,
		});
	}
	_subscribedToThumbnails = true;
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

TextState Giveaway::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	for (const auto &channel : _channels) {
		if (channel.geometry.contains(point)) {
			result.link = channel.link;
			return result;
		}
	}
	return result;
}

bool Giveaway::hideFromName() const {
	return !parent()->data()->Has<HistoryMessageForwarded>();
}

bool Giveaway::hasHeavyPart() const {
	return _subscribedToThumbnails;
}

void Giveaway::unloadHeavyPart() {
	if (base::take(_subscribedToThumbnails)) {
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
