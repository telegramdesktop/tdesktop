/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "smartglocal/smartglocal_token.h"

namespace SmartGlocal {

QString Token::tokenId() const {
	return _tokenId;
}

Card Token::card() const {
	return _card;
}

Token Token::Empty() {
	return Token(QString());
}

Token Token::DecodedObjectFromAPIResponse(QJsonObject object) {
	const auto tokenId = object.value("token").toString();
	if (tokenId.isEmpty()) {
		return Token::Empty();
	}
	auto result = Token(tokenId);
	const auto card = object.value("info");
	if (card.isObject()) {
		result._card = Card::DecodedObjectFromAPIResponse(card.toObject());
	}
	return result;
}

bool Token::empty() const {
	return _tokenId.isEmpty();
}

Token::Token(QString tokenId)
: _tokenId(std::move(tokenId)) {
}

} // namespace SmartGlocal

