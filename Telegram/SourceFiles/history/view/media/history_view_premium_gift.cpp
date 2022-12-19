/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_premium_gift.h"

#include "chat_helpers/stickers_gift_box_pack.h"
#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_document.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h" // Settings::ShowGiftPremium
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] QString FormatGiftMonths(int months) {
	return (months < 12)
		? tr::lng_premium_gift_duration_months(tr::now, lt_count, months)
		: tr::lng_premium_gift_duration_years(
			tr::now,
			lt_count,
			std::round(months / 12.));
}

} // namespace

PremiumGift::PremiumGift(
	not_null<Element*> parent,
	not_null<Data::MediaGiftBox*> gift)
: _parent(parent)
, _gift(gift) {
}

PremiumGift::~PremiumGift() = default;

int PremiumGift::top() {
	return st::msgServiceGiftBoxStickerTop;
}

QSize PremiumGift::size() {
	return st::msgServiceGiftBoxStickerSize;
}

QString PremiumGift::title() {
	return tr::lng_premium_summary_title(tr::now);
}

QString PremiumGift::subtitle() {
	return FormatGiftMonths(_gift->months());
}

QString PremiumGift::button() {
	return tr::lng_sticker_premium_view(tr::now);
}

ClickHandlerPtr PremiumGift::createViewLink() {
	const auto from = _gift->from();
	const auto to = _parent->history()->peer;
	const auto months = _gift->months();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto me = (from->id == controller->session().userPeerId());
			const auto peer = me ? to : from;
			Settings::ShowGiftPremium(controller, peer, months, me);
		}
	});
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

void PremiumGift::ensureStickerCreated() const {
	if (_sticker) {
		return;
	}
	const auto &session = _parent->history()->session();
	auto &packs = session.giftBoxStickersPacks();
	if (const auto document = packs.lookup(_gift->months())) {
		if (const auto sticker = document->sticker()) {
			const auto skipPremiumEffect = false;
			_sticker.emplace(_parent, document, skipPremiumEffect, _parent);
			_sticker->setDiceIndex(sticker->alt, 1);
			_sticker->setGiftBoxSticker(true);
			_sticker->initSize();
		}
	}
}

} // namespace HistoryView
