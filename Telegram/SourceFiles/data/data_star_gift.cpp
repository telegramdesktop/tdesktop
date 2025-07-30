/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_star_gift.h"

#include "lang/lang_tag.h"
#include "ui/controls/ton_common.h"
#include "ui/text/text_utilities.h"
#include "styles/style_credits.h"

namespace Data {

QString UniqueGiftName(const UniqueGift &gift) {
	return gift.title + u" #"_q + QString::number(gift.number);
}

CreditsAmount UniqueGiftResaleStars(const UniqueGift &gift) {
	return CreditsAmount(gift.starsForResale);
}

CreditsAmount UniqueGiftResaleTon(const UniqueGift &gift) {
	return CreditsAmount(
		gift.nanoTonForResale / Ui::kNanosInOne,
		gift.nanoTonForResale % Ui::kNanosInOne,
		CreditsType::Ton);
}

CreditsAmount UniqueGiftResaleAsked(const UniqueGift &gift) {
	return gift.onlyAcceptTon
		? UniqueGiftResaleTon(gift)
		: UniqueGiftResaleStars(gift);
}

TextWithEntities FormatGiftResaleStars(const UniqueGift &gift) {
	return Ui::Text::IconEmoji(
		&st::starIconEmoji
	).append(Lang::FormatCountDecimal(gift.starsForResale));
}

TextWithEntities FormatGiftResaleTon(const UniqueGift &gift) {
	return Ui::Text::IconEmoji(
		&st::tonIconEmoji
	).append(Lang::FormatCreditsAmountDecimal(UniqueGiftResaleTon(gift)));
}

TextWithEntities FormatGiftResaleAsked(const UniqueGift &gift) {
	return gift.onlyAcceptTon
		? FormatGiftResaleTon(gift)
		: FormatGiftResaleStars(gift);
}

} // namespace Data
