/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stripe/stripe_token.h"

#include "stripe/stripe_decode.h"

namespace Stripe {

QString Token::tokenId() const {
	return _tokenId;
}

bool Token::livemode() const {
	return _livemode;
}

Card Token::card() const {
	return _card;
}

Token Token::Empty() {
	return Token(QString(), false, QDateTime());
}

Token Token::DecodedObjectFromAPIResponse(QJsonObject object) {
	if (!ContainsFields(object, { u"id", u"livemode", u"created" })) {
		return Token::Empty();
	}
	const auto tokenId = object.value("id").toString();
	const auto livemode = object.value("livemode").toBool();
	const auto created = QDateTime::fromTime_t(
		object.value("created").toDouble());
	auto result = Token(tokenId, livemode, created);
	const auto card = object.value("card");
	if (card.isObject()) {
		result._card = Card::DecodedObjectFromAPIResponse(card.toObject());
	}

	// TODO incomplete, not used.
	//const auto bankAccount = object.value("bank_account");
	//if (bankAccount.isObject()) {
	//	result._bankAccount = bankAccount::DecodedObjectFromAPIResponse(
	//		bankAccount.toObject());
	//}
	//result._allResponseFields = object;

	return result;
}

bool Token::empty() const {
	return _tokenId.isEmpty();
}

Token::Token(QString tokenId, bool livemode, QDateTime created)
: _tokenId(std::move(tokenId))
, _livemode(livemode)
, _created(std::move(created)) {
}

} // namespace Stripe
