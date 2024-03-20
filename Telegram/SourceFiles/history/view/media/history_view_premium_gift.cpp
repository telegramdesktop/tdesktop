/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_premium_gift.h"

#include "boxes/gift_premium_box.h" // ResolveGiftCode
#include "chat_helpers/stickers_gift_box_pack.h"
#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_document.h"
#include "data/data_channel.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h" // Settings::ShowGiftPremium
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {

PremiumGift::PremiumGift(
	not_null<Element*> parent,
	not_null<Data::MediaGiftBox*> gift)
: _parent(parent)
, _gift(gift)
, _data(gift->data()) {
}

PremiumGift::~PremiumGift() = default;

int PremiumGift::top() {
	return st::msgServiceGiftBoxStickerTop;
}

QSize PremiumGift::size() {
	return QSize(
		st::msgServiceGiftBoxStickerSize,
		st::msgServiceGiftBoxStickerSize);
}

QString PremiumGift::title() {
	return gift()
		? tr::lng_premium_summary_title(tr::now)
		: _data.unclaimed
		? tr::lng_prize_unclaimed_title(tr::now)
		: tr::lng_prize_title(tr::now);
}

TextWithEntities PremiumGift::subtitle() {
	if (gift()) {
		return { GiftDuration(_data.months) };
	}
	const auto name = _data.channel ? _data.channel->name() : "channel";
	auto result = (_data.unclaimed
		? tr::lng_prize_unclaimed_about
		: _data.viaGiveaway
		? tr::lng_prize_about
		: tr::lng_prize_gift_about)(
			tr::now,
			lt_channel,
			Ui::Text::Bold(name),
			Ui::Text::RichLangValue);
	result.append("\n\n");
	result.append((_data.unclaimed
		? tr::lng_prize_unclaimed_duration
		: _data.viaGiveaway
		? tr::lng_prize_duration
		: tr::lng_prize_gift_duration)(
			tr::now,
			lt_duration,
			Ui::Text::Bold(GiftDuration(_data.months)),
			Ui::Text::RichLangValue));
	return result;
}

rpl::producer<QString> PremiumGift::button() {
	return (gift() && (outgoingGift() || !_data.unclaimed))
		? tr::lng_sticker_premium_view()
		: tr::lng_prize_open();
}

ClickHandlerPtr PremiumGift::createViewLink() {
	const auto from = _gift->from();
	const auto to = _parent->history()->peer;
	const auto data = _gift->data();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto selfId = controller->session().userPeerId();
			const auto self = (from->id == selfId);
			if (data.slug.isEmpty()) {
				const auto peer = self ? to : from;
				const auto months = data.months;
				Settings::ShowGiftPremium(controller, peer, months, self);
			} else {
				const auto fromId = from->id;
				const auto toId = self ? to->id : selfId;
				ResolveGiftCode(controller, data.slug, fromId, toId);
			}
		}
	});
}

int PremiumGift::buttonSkip() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

void PremiumGift::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	if (_sticker) {
		_sticker->draw(p, context, geometry);
	} else {
		ensureStickerCreated();
	}
}

bool PremiumGift::hideServiceText() {
	return !gift();
}

void PremiumGift::stickerClearLoopPlayed() {
	if (_sticker) {
		_sticker->stickerClearLoopPlayed();
	}
}

std::unique_ptr<StickerPlayer> PremiumGift::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _sticker
		? _sticker->stickerTakePlayer(data, replacements)
		: nullptr;
}

bool PremiumGift::hasHeavyPart() {
	return (_sticker ? _sticker->hasHeavyPart() : false);
}

void PremiumGift::unloadHeavyPart() {
	if (_sticker) {
		_sticker->unloadHeavyPart();
	}
}

bool PremiumGift::incomingGift() const {
	return gift() && !_parent->data()->out();
}

bool PremiumGift::outgoingGift() const {
	return gift() && _parent->data()->out();
}

bool PremiumGift::gift() const {
	return _data.slug.isEmpty() || !_data.channel;
}

void PremiumGift::ensureStickerCreated() const {
	if (_sticker) {
		return;
	}
	const auto &session = _parent->history()->session();
	const auto months = _gift->data().months;
	auto &packs = session.giftBoxStickersPacks();
	if (const auto document = packs.lookup(months)) {
		if (const auto sticker = document->sticker()) {
			const auto skipPremiumEffect = false;
			_sticker.emplace(_parent, document, skipPremiumEffect, _parent);
			_sticker->setDiceIndex(sticker->alt, 1);
			_sticker->initSize(st::msgServiceGiftBoxStickerSize);
		}
	}
}

} // namespace HistoryView
